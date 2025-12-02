#include "PaymentController.h"
#include "DatabaseManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

PaymentController::PaymentController(QObject *parent)
    : BaseController(parent)
{
}

//注册3个HTTP路由
void PaymentController::registerRoutes(QHttpServer *server)
{
    // 1. 支付接口：客户端提交支付请求，服务端创建支付记录
    server->route("/api/payment", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handlePostPayment(req);
                  });

    // 2. 查询支付状态接口：通过支付ID查询支付状态
    server->route("/api/payment/status/<int>", QHttpServerRequest::Method::Get,
                  [this](int paymentId) {
                      return handleGetPaymentStatus(paymentId);
                  });

    // 3. 退款接口：客户端提交退款请求，服务端更新支付状态为退款
    server->route("/api/payment/refund", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleRefundPayment(req);
                  });
}


// -------------------------------
// 1. 支付接口
// -------------------------------
QHttpServerResponse PaymentController::handlePostPayment(const QHttpServerRequest &request)
{
    // 客户端 -> 服务端：JSON格式的支付请求数据
    // 包含字段：order_id（订单ID）、amount（支付金额）

    // 解析请求体中的JSON数据
    QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "无效的JSON格式";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("order_id") || !obj.contains("amount")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "参数缺失：需要order_id和amount";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // 验证金额
    double amount = obj["amount"].toDouble();
    if (amount <= 0) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "支付金额必须大于0";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 使用事务确保数据一致性
    db.transaction();

    // 将支付信息保存到数据库的payments表中
    QSqlQuery q(db);
    q.prepare("INSERT INTO payments (order_id, amount, status, created_at) VALUES (?, ?, 'completed', CURRENT_TIMESTAMP)");
    q.addBindValue(obj["order_id"].toString());
    q.addBindValue(amount);

    // 执行数据库插入操作
    if (!q.exec()) {
        db.rollback();
        qWarning() << "Payment insert error:" << q.lastError();

        QJsonObject err;
        err["status"] = "failed";

        QString errorText = q.lastError().text();
        if (errorText.contains("foreign key", Qt::CaseInsensitive)) {
            err["message"] = "订单不存在";
        } else if (errorText.contains("duplicate", Qt::CaseInsensitive)) {
            err["message"] = "该订单已支付";
        } else {
            err["message"] = "支付失败，请稍后重试";
        }

        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    db.commit();

    // 构建响应返回给客户端（嵌套JSON格式）
    QJsonObject data;
    data["payment_id"] = q.lastInsertId().toInt();
    data["order_id"] = obj["order_id"].toString();
    data["amount"] = amount;
    data["status"] = "completed";

    QJsonObject res;
    res["status"] = "success";
    res["message"] = "支付成功";
    res["data"] = data;

    return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
}


// -------------------------------
// 2. 查询支付状态
// -------------------------------
QHttpServerResponse PaymentController::handleGetPaymentStatus(int paymentId)
{
    // URL参数：paymentId（支付ID，从URL路径中提取）
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 从payments表中查询指定支付ID的状态
    QSqlQuery q(db);
    q.prepare("SELECT status, order_id, amount, created_at FROM payments WHERE id = ?");
    q.addBindValue(paymentId);

    if (!q.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "查询失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (!q.next()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "支付记录不存在";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    // 构建响应数据（嵌套JSON格式）
    QJsonObject data;
    data["payment_id"] = paymentId;
    data["order_id"] = q.value("order_id").toString();
    data["payment_status"] = q.value("status").toString();
    data["amount"] = q.value("amount").toDouble();
    data["created_at"] = q.value("created_at").toString();

    QJsonObject res;
    res["status"] = "success";
    res["message"] = "查询成功";
    res["data"] = data;

    return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
}



// -------------------------------
// 3. 退款
// -------------------------------
QHttpServerResponse PaymentController::handleRefundPayment(const QHttpServerRequest &request)
{
    // 客户端 -> 服务端：JSON格式的退款请求
    // 必需字段：payment_id（要退款的支付ID）
    QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "无效的JSON格式";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("payment_id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "参数缺失：需要payment_id";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int paymentId = obj["payment_id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    db.transaction();

    // 先检查当前状态
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT status FROM payments WHERE id = ?");
    checkQuery.addBindValue(paymentId);

    if (!checkQuery.exec() || !checkQuery.next()) {
        db.rollback();
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "支付记录不存在";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    QString currentStatus = checkQuery.value("status").toString();
    if (currentStatus == "refunded") {
        db.rollback();
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "该支付已退款，不能重复退款";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    if (currentStatus != "completed") {
        db.rollback();
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "只有已完成的支付才能退款";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // 更新数据库中的支付状态为'refunded'
    QSqlQuery q(db);
    q.prepare("UPDATE payments SET status = 'refunded', refund_time = CURRENT_TIMESTAMP WHERE id = ?");
    q.addBindValue(paymentId);

    if (!q.exec()) {
        db.rollback();
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "退款失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (q.numRowsAffected() > 0) {
        db.commit();
        // 构建响应数据（嵌套JSON格式）
        QJsonObject data;
        data["payment_id"] = paymentId;
        data["refund_status"] = "refunded";

        QJsonObject res;
        res["status"] = "success";
        res["message"] = "退款成功";
        res["data"] = data;

        return QHttpServerResponse(res, QHttpServerResponse::StatusCode::Ok);
    } else {
        db.rollback();
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "退款失败，支付记录不存在";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }
}
