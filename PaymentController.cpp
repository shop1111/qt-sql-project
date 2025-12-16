#include "PaymentController.h"
#include "DatabaseManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDateTime>
#include <QUuid>
#include <QThread>

PaymentController::PaymentController(QObject *parent)
    : BaseController(parent)
{
    qDebug() << "PaymentController initialized (Lite Version)";
}

void PaymentController::registerRoutes(QHttpServer *server)
{
    // 1. 用户充值接口
    server->route("/api/user/recharge", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleRecharge(req);
                  });

    // 2. 订单支付接口
    server->route("/api/payment", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handlePayment(req);
                  });

    qDebug() << "PaymentController routes registered: /api/user/recharge, /api/payment";
}

// ============================================================
// 1. 处理用户充值
// ============================================================
QHttpServerResponse PaymentController::handleRecharge(const QHttpServerRequest &request)
{
    // 1. 解析请求
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject reqObj = jsonDoc.object();
    int uid = extractIntValue(reqObj, "uid");
    double amount = extractDoubleValue(reqObj, "amount");

    // 2. 验证参数
    if (uid <= 0 || amount <= 0) {
        return createErrorResponse("参数无效: 用户ID或金额不正确");
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("UPDATE users SET balance = balance + ? WHERE U_ID = ?");
    query.addBindValue(amount);
    query.addBindValue(uid);

    if (!query.exec()) {
        return createErrorResponse("充值失败: " + query.lastError().text(), QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 检查是否有行被更新（防止 UID 不存在的情况）
    if (query.numRowsAffected() == 0) {
        return createErrorResponse("用户不存在", QHttpServerResponse::StatusCode::NotFound);
    }

    return createSuccessResponse("充值成功");
}

// ============================================================
// 2. 处理订单支付 (包含原子扣款)
// ============================================================
QHttpServerResponse PaymentController::handlePayment(const QHttpServerRequest &request)
{
    // 1. 解析请求
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return createErrorResponse("Invalid JSON format");
    }

    QJsonObject reqObj = jsonDoc.object();
    int userId = extractIntValue(reqObj, "user_id");
    QString orderId = reqObj["order_id"].toVariant().toString(); // 兼容字符串ID
    // 注意：简化逻辑下，我们直接信任数据库里的订单总价，忽略前端传来的 amount
    // 也可以校验前端 amount 是否等于 totalAmount，这里选择以数据库为准
    if (userId <= 0 || orderId.isEmpty()) {
        return createErrorResponse("参数不完整 (uid, order_id)");
    }
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }
    // 2. 查询订单信息
    QSqlQuery orderQuery(db);
    orderQuery.prepare("SELECT ID, user_id, status, total_amount FROM orders WHERE ID = ?");
    orderQuery.addBindValue(orderId);
    if (!orderQuery.exec() || !orderQuery.next()) {
        return createErrorResponse("订单不存在", QHttpServerResponse::StatusCode::NotFound);
    }

    int orderUserId = orderQuery.value("user_id").toInt();
    QString currentStatus = orderQuery.value("status").toString();
    double totalAmount = orderQuery.value("total_amount").toDouble();

    if (orderUserId != userId) {
        return createErrorResponse("订单不属于该用户", QHttpServerResponse::StatusCode::Forbidden);
    }
    if (currentStatus == "已支付") {
        return createErrorResponse("订单已支付，请勿重复操作");
    }
    if (currentStatus != "未支付") {
        return createErrorResponse("当前订单状态无法支付: " + currentStatus);
    }

    db.transaction();
    try {
        // --- 核心步骤：直接在 users 表扣除全款 ---
        QSqlQuery deductQuery(db);
        deductQuery.prepare(
            "UPDATE users "
            "SET balance = balance - ? "
            "WHERE U_ID = ? AND balance >= ?"
            );
        deductQuery.addBindValue(totalAmount);
        deductQuery.addBindValue(userId);
        deductQuery.addBindValue(totalAmount);

        if (!deductQuery.exec()) {
            throw std::runtime_error("数据库执行错误: " + deductQuery.lastError().text().toStdString());
        }
        if (deductQuery.numRowsAffected() == 0) {
            throw std::runtime_error("余额不足，支付失败");
        }
        QSqlQuery updateOrder(db);
        updateOrder.prepare(
            "UPDATE orders SET status = '已支付', paid_amount = ?,payment_method = 'balance' "
            "WHERE ID = ?"
            );
        updateOrder.addBindValue(totalAmount); // 已付金额 = 总金额
        updateOrder.addBindValue(orderId);
        if (!updateOrder.exec()) {
            throw std::runtime_error("更新订单状态失败");
        }
        db.commit();
        QJsonObject response = createSuccessResponse("支付成功");
        response["data"] = QJsonObject{
            {"order_id", orderId},
            {"new_status", "已支付"},
            {"paid", totalAmount}
        };
        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);
    } catch (const std::exception &e) {
        db.rollback();
        qWarning() << "Payment Error:" << e.what();
        // 返回 200 但 status=failed 还是返回 500 取决于前端处理，这里通常算业务逻辑错误
        return createErrorResponse(e.what(), QHttpServerResponse::StatusCode::InternalServerError);
    }
}

// ============================================================
// 辅助函数
// ============================================================

int PaymentController::extractIntValue(const QJsonObject &obj, const QString &key)
{
    if (!obj.contains(key)) return 0;
    QJsonValue v = obj[key];
    if (v.isString()) return v.toString().toInt();
    if (v.isDouble()) return static_cast<int>(v.toDouble());
    return 0;
}

double PaymentController::extractDoubleValue(const QJsonObject &obj, const QString &key)
{
    if (!obj.contains(key)) return 0.0;
    QJsonValue v = obj[key];
    if (v.isString()) return v.toString().toDouble();
    if (v.isDouble()) return v.toDouble();
    return 0.0;
}

bool PaymentController::userExists(QSqlDatabase &db, int userId)
{
    QSqlQuery q(db);
    q.prepare("SELECT U_ID FROM users WHERE U_ID = ?");
    q.addBindValue(userId);
    return q.exec() && q.next();
}

QJsonObject PaymentController::createSuccessResponse(const QString &message)
{
    return QJsonObject{{"status", "success"}, {"message", message}};
}

QJsonObject PaymentController::createErrorResponse(const QString &message, QHttpServerResponse::StatusCode status)
{
    Q_UNUSED(status); // 可根据需要使用status
    return QJsonObject{{"status", "failed"}, {"message", message}};
}
