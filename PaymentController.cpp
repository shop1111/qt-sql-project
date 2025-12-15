// PaymentController.cpp - 完整版本（包含所有缺失功能）
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
#include <QCryptographicHash>

PaymentController::PaymentController(QObject *parent)
    : BaseController(parent)
{
    qDebug() << "PaymentController initialized";
}

void PaymentController::registerRoutes(QHttpServer *server)
{
    // 用户充值接口
    server->route("/api/user/recharge", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleRecharge(req);
                  });

    // 订单支付接口
    server->route("/api/payment", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handlePayment(req);
                  });

    // 【新增】获取用户信息接口
    server->route("/api/user/info", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleUserInfo(req);
                  });

    // 【新增】更新用户信息接口
    server->route("/api/user/update", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleUpdateUser(req);
                  });

    // 【新增】实名认证接口
    server->route("/api/user/verify", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleVerifyUser(req);
                  });

    // 【新增】获取订单列表接口
    server->route("/api/orders", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleGetOrders(req);
                  });

    // 【新增】删除订单接口
    server->route("/api/delete_order", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleDeleteOrder(req);
                  });

    qDebug() << "PaymentController routes registered successfully";
}

// ============================================================
// 1. 处理用户充值（已优化类型处理）
// ============================================================
QHttpServerResponse PaymentController::handleRecharge(const QHttpServerRequest &request)
{
    qDebug() << "=== Recharge Request Received ===";
    qDebug() << "Request body:" << request.body();

    // 1. 解析请求
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return createErrorResponse("Invalid JSON format", QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();

    // 【修复】更灵活的类型处理
    int uid = extractIntValue(reqObj, "uid");
    double amount = extractDoubleValue(reqObj, "amount");

    qDebug() << "Recharge request - User:" << uid << "Amount:" << amount;

    // 2. 验证参数
    if (uid <= 0 || amount <= 0) {
        return createErrorResponse(QString("参数无效: 用户ID=%1, 金额=%2").arg(uid).arg(amount),
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    // 3. 检查用户是否存在
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (!userExists(db, uid)) {
        qDebug() << "User" << uid << "does not exist";
        return createErrorResponse("用户不存在", QHttpServerResponse::StatusCode::NotFound);
    }

    qDebug() << "User" << uid << "exists, processing recharge...";

    // 4. 开始事务
    db.transaction();

    try {
        // 生成充值订单号
        QString orderNo = generateOrderNo("RECH");

        // 插入充值记录
        QSqlQuery insertQuery(db);
        insertQuery.prepare(
            "INSERT INTO recharge_records (user_id, order_no, amount, status) "
            "VALUES (?, ?, ?, 'success')"
            );
        insertQuery.addBindValue(uid);
        insertQuery.addBindValue(orderNo);
        insertQuery.addBindValue(amount);

        if (!insertQuery.exec()) {
            throw std::runtime_error("充值记录插入失败: " + insertQuery.lastError().text().toStdString());
        }

        qDebug() << "Recharge record inserted, order no:" << orderNo;

        // 更新用户账户余额
        QSqlQuery updateQuery(db);
        updateQuery.prepare(
            "INSERT INTO user_accounts (user_id, balance, total_recharge, last_recharge_time) "
            "VALUES (?, ?, ?, NOW()) "
            "ON DUPLICATE KEY UPDATE "
            "balance = balance + ?, "
            "total_recharge = total_recharge + ?, "
            "last_recharge_time = NOW()"
            );
        updateQuery.addBindValue(uid);
        updateQuery.addBindValue(amount);
        updateQuery.addBindValue(amount);
        updateQuery.addBindValue(amount);
        updateQuery.addBindValue(amount);

        if (!updateQuery.exec()) {
            throw std::runtime_error("更新账户余额失败: " + updateQuery.lastError().text().toStdString());
        }

        // 获取更新后的余额
        double newBalance = getUserBalance(db, uid);

        db.commit();

        qDebug() << "Recharge successful! New balance:" << newBalance;

        // 返回成功响应
        QJsonObject response = createSuccessResponse("充值成功");
        response["data"] = QJsonObject{
            {"order_no", orderNo},
            {"recharge_amount", amount},
            {"new_balance", newBalance},
            {"recharge_time", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}
        };

        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);

    } catch (const std::exception &e) {
        db.rollback();
        qWarning() << "Recharge error:" << e.what();
        return createErrorResponse(QString("充值失败: %1").arg(e.what()),
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

// ============================================================
// 2. 处理订单支付（修复所有数据类型问题）
// ============================================================
QHttpServerResponse PaymentController::handlePayment(const QHttpServerRequest &request)
{
    qDebug() << "=== Payment Request Received ===";
    qDebug() << "Request body:" << request.body();

    // 1. 解析请求
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return createErrorResponse("Invalid JSON format", QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();

    // 【修复】统一类型处理
    int userId = extractIntValue(reqObj, "user_id");
    QString orderId = reqObj["order_id"].toString();
    double amount = extractDoubleValue(reqObj, "amount");

    // 【修复】payment_method 参数处理，支持默认值
    QString paymentMethod = "balance"; // 默认值
    if (reqObj.contains("payment_method")) {
        paymentMethod = reqObj["payment_method"].toString("balance");
    }

    qDebug() << "Payment request - User:" << userId
             << "Order ID:" << orderId
             << "Amount:" << amount
             << "Method:" << paymentMethod;

    // 参数验证
    if (userId <= 0) {
        return createErrorResponse(QString("用户ID无效: %1").arg(userId),
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    if (orderId.isEmpty()) {
        return createErrorResponse("订单号不能为空", QHttpServerResponse::StatusCode::BadRequest);
    }

    if (amount <= 0) {
        return createErrorResponse(QString("支付金额无效: %1").arg(amount),
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 2. 查找订单
    QSqlQuery orderQuery(db);
    orderQuery.prepare(
        "SELECT ID, order_id, user_id, status, total_amount, paid_amount "
        "FROM orders WHERE order_id = ?"
        );
    orderQuery.addBindValue(orderId);

    if (!orderQuery.exec()) {
        qDebug() << "Order query error:" << orderQuery.lastError().text();
        return createErrorResponse("订单查询失败: " + orderQuery.lastError().text(),
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (!orderQuery.next()) {
        qDebug() << "Order not found:" << orderId;
        return createErrorResponse("订单不存在: " + orderId,
                                   QHttpServerResponse::StatusCode::NotFound);
    }

    int dbOrderId = orderQuery.value("ID").toInt();
    QString dbOrderNo = orderQuery.value("order_id").toString();
    int orderUserId = orderQuery.value("user_id").toInt();
    QString orderStatus = orderQuery.value("status").toString();
    double orderTotal = orderQuery.value("total_amount").toDouble();
    double orderPaid = orderQuery.value("paid_amount").toDouble();

    qDebug() << "Found order - DB ID:" << dbOrderId
             << "Order No:" << dbOrderNo
             << "User:" << orderUserId
             << "Status:" << orderStatus
             << "Total:" << orderTotal
             << "Paid:" << orderPaid;

    // 3. 验证订单
    if (orderUserId != userId) {
        qDebug() << "Order user mismatch. Order user:" << orderUserId << "Request user:" << userId;
        return createErrorResponse("订单不属于当前用户",
                                   QHttpServerResponse::StatusCode::Forbidden);
    }

    if (orderStatus != "未支付") {
        qDebug() << "Order status is not '未支付'. Current status:" << orderStatus;
        return createErrorResponse("订单状态不可支付，当前状态: " + orderStatus,
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    if (amount > orderTotal - orderPaid) {
        qDebug() << "Payment amount too high. Amount:" << amount
                 << "Remaining:" << (orderTotal - orderPaid);
        return createErrorResponse(QString("支付金额%1超过应支付金额%2").arg(amount).arg(orderTotal - orderPaid),
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    db.transaction();

    try {
        bool paymentSuccess = true;

        // 4. 余额支付
        if (paymentMethod == "balance") {
            qDebug() << "Processing balance payment...";

            // 检查余额
            double userBalance = getUserBalance(db, userId);
            qDebug() << "User balance:" << userBalance;

            if (userBalance < amount) {
                throw std::runtime_error(QString("余额不足，当前余额: ¥%1").arg(userBalance, 0, 'f', 2).toStdString());
            }

            // 扣除余额
            QSqlQuery deductQuery(db);
            deductQuery.prepare(
                "UPDATE user_accounts SET balance = balance - ? WHERE user_id = ?"
                );
            deductQuery.addBindValue(amount);
            deductQuery.addBindValue(userId);

            if (!deductQuery.exec()) {
                throw std::runtime_error("扣款失败: " + deductQuery.lastError().text().toStdString());
            }

            qDebug() << "Balance deducted successfully";
        }
        // 微信/支付宝支付（模拟）
        else if (paymentMethod == "wechat" || paymentMethod == "alipay") {
            qDebug() << "Processing" << paymentMethod << "payment (simulated)...";
            // 模拟支付成功
            QThread::msleep(50);
        }
        else {
            throw std::runtime_error("不支持的支付方式: " + paymentMethod.toStdString());
        }

        // 5. 更新订单状态
        double newPaidAmount = orderPaid + amount;
        QString newStatus = (newPaidAmount >= orderTotal) ? "已支付" : "部分支付";

        QSqlQuery updateOrderQuery(db);
        updateOrderQuery.prepare(
            "UPDATE orders SET "
            "status = ?, "
            "paid_amount = ?, "
            "payment_method = ?, "
            "payment_time = NOW() "
            "WHERE ID = ?"
            );
        updateOrderQuery.addBindValue(newStatus);
        updateOrderQuery.addBindValue(newPaidAmount);
        updateOrderQuery.addBindValue(paymentMethod);
        updateOrderQuery.addBindValue(dbOrderId);

        if (!updateOrderQuery.exec()) {
            throw std::runtime_error("更新订单失败: " + updateOrderQuery.lastError().text().toStdString());
        }

        db.commit();

        qDebug() << "Payment successful! New status:" << newStatus;

        // 6. 返回成功响应
        QJsonObject response = createSuccessResponse("支付成功");
        response["data"] = QJsonObject{
            {"order_id", orderId},
            {"payment_amount", amount},
            {"payment_method", paymentMethod},
            {"order_status", newStatus},
            {"paid_amount", newPaidAmount},
            {"total_amount", orderTotal},
            {"payment_time", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")}
        };

        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);

    } catch (const std::exception &e) {
        db.rollback();
        qWarning() << "Payment error:" << e.what();
        return createErrorResponse(QString("支付失败: %1").arg(e.what()),
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

// ============================================================
// 3. 获取用户信息接口
// ============================================================
QHttpServerResponse PaymentController::handleUserInfo(const QHttpServerRequest &request)
{
    qDebug() << "=== Get User Info Request ===";

    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return createErrorResponse("Invalid JSON format", QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();
    int uid = extractIntValue(reqObj, "uid");

    if (uid <= 0) {
        return createErrorResponse("用户ID无效", QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }

    try {
        // 获取用户基本信息
        QSqlQuery userQuery(db);
        userQuery.prepare(
            "SELECT u.username as nickname, u.truename, u.gender, u.email, u.telephone, "
            "u.id_card, u.is_verified, "
            "COALESCE(ua.balance, 0) as balance "
            "FROM users u "
            "LEFT JOIN user_accounts ua ON u.U_ID = ua.user_id "
            "WHERE u.U_ID = ?"
            );
        userQuery.addBindValue(uid);

        if (!userQuery.exec() || !userQuery.next()) {
            return createErrorResponse("用户不存在", QHttpServerResponse::StatusCode::NotFound);
        }

        // 构建用户信息对象
        QJsonObject userInfo;
        userInfo["nickname"] = userQuery.value("nickname").toString();
        userInfo["truename"] = userQuery.value("truename").toString();
        userInfo["gender"] = userQuery.value("gender").toString();
        userInfo["email"] = userQuery.value("email").toString();
        userInfo["telephone"] = userQuery.value("telephone").toString();
        userInfo["id_card"] = userQuery.value("id_card").toString();
        userInfo["is_verified"] = userQuery.value("is_verified").toBool();
        userInfo["balance"] = userQuery.value("balance").toDouble();

        QJsonObject response = createSuccessResponse("获取用户信息成功");
        response["data"] = userInfo;

        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);

    } catch (const std::exception &e) {
        qWarning() << "Get user info error:" << e.what();
        return createErrorResponse(QString("获取用户信息失败: %1").arg(e.what()),
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

// ============================================================
// 4. 更新用户信息接口
// ============================================================
QHttpServerResponse PaymentController::handleUpdateUser(const QHttpServerRequest &request)
{
    qDebug() << "=== Update User Info Request ===";

    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return createErrorResponse("Invalid JSON format", QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();
    int uid = extractIntValue(reqObj, "uid");
    QString field = reqObj["field"].toString();
    QString value = reqObj["value"].toString();

    if (uid <= 0) {
        return createErrorResponse("用户ID无效", QHttpServerResponse::StatusCode::BadRequest);
    }

    if (field.isEmpty() || value.isEmpty()) {
        return createErrorResponse("字段名或值不能为空", QHttpServerResponse::StatusCode::BadRequest);
    }

    // 验证允许更新的字段
    QStringList allowedFields = {"nickname", "email", "telephone"};
    if (!allowedFields.contains(field)) {
        return createErrorResponse("不允许更新该字段", QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }

    db.transaction();

    try {
        // 映射数据库字段名
        QString dbField;
        if (field == "nickname") dbField = "username";
        else if (field == "email") dbField = "email";
        else if (field == "telephone") dbField = "telephone";

        QSqlQuery updateQuery(db);
        updateQuery.prepare(
            QString("UPDATE users SET %1 = ? WHERE U_ID = ?").arg(dbField)
            );
        updateQuery.addBindValue(value);
        updateQuery.addBindValue(uid);

        if (!updateQuery.exec()) {
            throw std::runtime_error("更新用户信息失败: " + updateQuery.lastError().text().toStdString());
        }

        if (updateQuery.numRowsAffected() <= 0) {
            throw std::runtime_error("用户不存在或数据未变化");
        }

        db.commit();

        QJsonObject response = createSuccessResponse("更新成功");
        response["data"] = QJsonObject{
            {"field", field},
            {"new_value", value}
        };

        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);

    } catch (const std::exception &e) {
        db.rollback();
        qWarning() << "Update user error:" << e.what();
        return createErrorResponse(QString("更新失败: %1").arg(e.what()),
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

// ============================================================
// 5. 实名认证接口
// ============================================================
QHttpServerResponse PaymentController::handleVerifyUser(const QHttpServerRequest &request)
{
    qDebug() << "=== User Verification Request ===";

    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return createErrorResponse("Invalid JSON format", QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();
    int uid = extractIntValue(reqObj, "uid");
    QString truename = reqObj["truename"].toString();
    QString idCard = reqObj["id_card"].toString();

    if (uid <= 0) {
        return createErrorResponse("用户ID无效", QHttpServerResponse::StatusCode::BadRequest);
    }

    if (truename.isEmpty() || idCard.isEmpty()) {
        return createErrorResponse("姓名和身份证号不能为空", QHttpServerResponse::StatusCode::BadRequest);
    }

    // 简单的身份证号验证
    if (idCard.length() != 18) {
        return createErrorResponse("请输入18位有效的身份证号", QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }

    db.transaction();

    try {
        // 检查是否已经认证
        QSqlQuery checkQuery(db);
        checkQuery.prepare("SELECT is_verified FROM users WHERE U_ID = ?");
        checkQuery.addBindValue(uid);

        if (!checkQuery.exec() || !checkQuery.next()) {
            throw std::runtime_error("用户不存在");
        }

        if (checkQuery.value("is_verified").toBool()) {
            throw std::runtime_error("用户已经完成实名认证");
        }

        // 更新用户实名信息
        QSqlQuery updateQuery(db);
        updateQuery.prepare(
            "UPDATE users SET truename = ?, id_card = ?, is_verified = 1 WHERE U_ID = ?"
            );
        updateQuery.addBindValue(truename);
        updateQuery.addBindValue(idCard);
        updateQuery.addBindValue(uid);

        if (!updateQuery.exec()) {
            throw std::runtime_error("更新实名信息失败: " + updateQuery.lastError().text().toStdString());
        }

        db.commit();

        QJsonObject response = createSuccessResponse("实名认证成功");
        response["data"] = QJsonObject{
            {"truename", truename},
            {"id_card", idCard},
            {"is_verified", true}
        };

        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);

    } catch (const std::exception &e) {
        db.rollback();
        qWarning() << "Verification error:" << e.what();
        return createErrorResponse(QString("认证失败: %1").arg(e.what()),
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

// ============================================================
// 6. 获取订单列表接口
// ============================================================
QHttpServerResponse PaymentController::handleGetOrders(const QHttpServerRequest &request)
{
    qDebug() << "=== Get Orders Request ===";

    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return createErrorResponse("Invalid JSON format", QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();
    int userId = extractIntValue(reqObj, "user_id");

    if (userId <= 0) {
        return createErrorResponse("用户ID无效", QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }

    try {
        QSqlQuery orderQuery(db);
        orderQuery.prepare(
            "SELECT ID, order_id, flight_number, dep_city, arr_city, "
            "dep_time, arr_time, total_amount as price, "
            "CASE WHEN status = '未支付' THEN 0 ELSE 1 END as status "
            "FROM orders WHERE user_id = ? ORDER BY create_time DESC"
            );
        orderQuery.addBindValue(userId);

        if (!orderQuery.exec()) {
            throw std::runtime_error("查询订单失败: " + orderQuery.lastError().text().toStdString());
        }

        QJsonArray ordersArray;
        while (orderQuery.next()) {
            QJsonObject order;
            order["order_id"] = orderQuery.value("order_id").toString();
            order["flight_number"] = orderQuery.value("flight_number").toString();
            order["dep_city"] = orderQuery.value("dep_city").toString();
            order["arr_city"] = orderQuery.value("arr_city").toString();
            order["dep_time"] = orderQuery.value("dep_time").toString();
            order["arr_time"] = orderQuery.value("arr_time").toString();
            order["price"] = orderQuery.value("price").toDouble();
            order["status"] = orderQuery.value("status").toInt();

            ordersArray.append(order);
        }

        QJsonObject response = createSuccessResponse("获取订单列表成功");
        response["data"] = ordersArray;

        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);

    } catch (const std::exception &e) {
        qWarning() << "Get orders error:" << e.what();
        return createErrorResponse(QString("获取订单列表失败: %1").arg(e.what()),
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

// ============================================================
// 7. 删除订单接口
// ============================================================
QHttpServerResponse PaymentController::handleDeleteOrder(const QHttpServerRequest &request)
{
    qDebug() << "=== Delete Order Request ===";

    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return createErrorResponse("Invalid JSON format", QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();
    int uid = extractIntValue(reqObj, "uid");
    QString orderId = reqObj["order_id"].toString();

    if (uid <= 0) {
        return createErrorResponse("用户ID无效", QHttpServerResponse::StatusCode::BadRequest);
    }

    if (orderId.isEmpty()) {
        return createErrorResponse("订单号不能为空", QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return createErrorResponse("数据库连接失败", QHttpServerResponse::StatusCode::InternalServerError);
    }

    db.transaction();

    try {
        // 检查订单是否存在且属于该用户
        QSqlQuery checkQuery(db);
        checkQuery.prepare(
            "SELECT ID, status FROM orders WHERE order_id = ? AND user_id = ?"
            );
        checkQuery.addBindValue(orderId);
        checkQuery.addBindValue(uid);

        if (!checkQuery.exec() || !checkQuery.next()) {
            throw std::runtime_error("订单不存在或不属于当前用户");
        }

        QString orderStatus = checkQuery.value("status").toString();

        // 只允许删除已完成或已取消的订单
        if (orderStatus == "未支付" || orderStatus == "部分支付") {
            throw std::runtime_error("未支付的订单不能删除，请先支付或取消");
        }

        int orderDbId = checkQuery.value("ID").toInt();

        // 删除订单
        QSqlQuery deleteQuery(db);
        deleteQuery.prepare("DELETE FROM orders WHERE ID = ?");
        deleteQuery.addBindValue(orderDbId);

        if (!deleteQuery.exec()) {
            throw std::runtime_error("删除订单失败: " + deleteQuery.lastError().text().toStdString());
        }

        db.commit();

        QJsonObject response = createSuccessResponse("订单删除成功");
        response["data"] = QJsonObject{
            {"order_id", orderId},
            {"deleted", true}
        };

        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);

    } catch (const std::exception &e) {
        db.rollback();
        qWarning() << "Delete order error:" << e.what();
        return createErrorResponse(QString("删除失败: %1").arg(e.what()),
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

// ============================================================
// 辅助函数
// ============================================================

// 从JSON值中提取整数（支持多种类型）
int PaymentController::extractIntValue(const QJsonObject &obj, const QString &key)
{
    if (!obj.contains(key)) {
        return 0;
    }

    QJsonValue value = obj[key];
    if (value.isString()) {
        return value.toString().toInt();
    } else if (value.isDouble()) {
        return value.toInt();
    } else if (value.isBool()) {
        return value.toBool() ? 1 : 0;
    }
    return 0;
}

// 从JSON值中提取浮点数（支持多种类型）
double PaymentController::extractDoubleValue(const QJsonObject &obj, const QString &key)
{
    if (!obj.contains(key)) {
        return 0.0;
    }

    QJsonValue value = obj[key];
    if (value.isString()) {
        return value.toString().toDouble();
    } else if (value.isDouble()) {
        return value.toDouble();
    } else if (value.isBool()) {
        return value.toBool() ? 1.0 : 0.0;
    }
    return 0.0;
}

// 检查用户是否存在
bool PaymentController::userExists(QSqlDatabase &db, int userId)
{
    QSqlQuery query(db);
    query.prepare("SELECT U_ID FROM users WHERE U_ID = ?");
    query.addBindValue(userId);
    return query.exec() && query.next();
}

// 获取用户余额
double PaymentController::getUserBalance(QSqlDatabase &db, int userId)
{
    QSqlQuery query(db);
    query.prepare("SELECT balance FROM user_accounts WHERE user_id = ?");
    query.addBindValue(userId);

    if (query.exec() && query.next()) {
        return query.value("balance").toDouble();
    }
    return 0.0;
}

// 创建成功响应
QJsonObject PaymentController::createSuccessResponse(const QString &message)
{
    return QJsonObject{
        {"status", "success"},
        {"message", message}
    };
}

// 创建错误响应
QJsonObject PaymentController::createErrorResponse(const QString &message,
                                                   QHttpServerResponse::StatusCode status)
{
    Q_UNUSED(status);
    return QJsonObject{
        {"status", "failed"},
        {"message", message}
    };
}

// 生成唯一的订单号
QString PaymentController::generateOrderNo(const QString &prefix)
{
    return prefix + QDateTime::currentDateTime().toString("yyyyMMddHHmmss") +
           QUuid::createUuid().toString().mid(1, 6);
}

// 模拟支付处理
bool PaymentController::processMockPayment(int userId, const QString &orderId, double amount,
                                           const QString &method)
{
    Q_UNUSED(userId);
    Q_UNUSED(orderId);
    Q_UNUSED(amount);
    Q_UNUSED(method);

    QThread::msleep(100); // 模拟支付处理时间
    return true; // 模拟支付成功
}
