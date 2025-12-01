#include "OrderController.h"
#include "DatabaseManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>

OrderController::OrderController(QObject *parent) : BaseController(parent)
{
}

void OrderController::registerRoutes(QHttpServer *server)
{
    // --- 路由注册 (全部 POST) ---

    // 1. 下单
    server->route("/api/create_order", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleCreateOrder(req);
                  });

    // 2. 查单
    server->route("/api/get_orders", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleGetOrders(req);
                  });

    // 3. 退单
    server->route("/api/cancel_order", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleCancelOrder(req);
                  });
}

// =============================================================
// 1. 创建订单
// 请求 JSON: { "user_id": 1, "flight_id": 5, "seat_type": 0, "seat_number": "12A" }
// =============================================================
QHttpServerResponse OrderController::handleCreateOrder(const QHttpServerRequest &request)
{
    // 解析 JSON
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("user_id") || !jsonObj.contains("flight_id")) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "参数缺失";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int userId = jsonObj["user_id"].toInt();
    int flightId = jsonObj["flight_id"].toInt();
    int seatType = jsonObj["seat_type"].toInt(0);
    QString seatNumber = jsonObj["seat_number"].toString();

    if (seatNumber.isEmpty()) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "没有接受到座位号";
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()){
        QJsonObject err; err["status"] = "failed"; err["message"] = "数据库连接失败";
        return QHttpServerResponse(err,QHttpServerResponse::StatusCode::InternalServerError);
    }

    QString status = jsonObj["status"].toString("未支付");

    QSqlQuery query(db);
    query.prepare("INSERT INTO orders (user_id, flight_id, seat_type, seat_number, status, order_date) "
                  "VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)");
    query.addBindValue(userId);
    query.addBindValue(flightId);
    query.addBindValue(seatType);
    query.addBindValue(seatNumber);
    query.addBindValue(status); // <--- 插入状态

    if (!query.exec()) {
        qWarning() << "Create Order Error:" << query.lastError().text();
        QJsonObject err;
        err["status"] = "failed";
        if (query.lastError().text().contains("Duplicate")) {
            err["message"] = "该座位已被占用，请重新选择";
        } else {
            err["message"] = "系统繁忙，下单失败";
        }
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "预订成功";
    success["order_id"] = query.lastInsertId().toInt();
    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}


QHttpServerResponse OrderController::handleGetOrders(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("user_id")) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "参数缺失";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }
    int userId = jsonObj["user_id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()){
        QJsonObject err; err["status"] = "failed"; err["message"] = "数据库连接失败";
        return QHttpServerResponse(err,QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 联合查询：获取订单详情 + 航班详情
    QSqlQuery query(db);
    QString sql = R"(
        SELECT
            o.ID as order_id, o.seat_type, o.seat_number, o.order_date, o.status,
            f.flight_number, f.airline, f.origin, f.destination,
            f.departure_time, f.landing_time, f.aircraft_model
        FROM orders o
        JOIN flights f ON o.flight_id = f.ID
        WHERE o.user_id = ?
        ORDER BY o.order_date DESC
    )";

    query.prepare(sql);
    query.addBindValue(userId);

    if (!query.exec()) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "数据库查询失败";
        return QHttpServerResponse(err,QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray list;
    while (query.next()) {
        QJsonObject item;
        item["order_id"] = query.value("order_id").toInt();
        item["status"] = query.value("status").toString();
        item["flight_number"] = query.value("flight_number").toString();
        item["airline"] = query.value("airline").toString();
        item["origin"] = query.value("origin").toString();
        item["destination"] = query.value("destination").toString();
        item["aircraft_model"] = query.value("aircraft_model").toString();

        // 时间格式化
        QDateTime dep = query.value("departure_time").toDateTime();
        QDateTime arr = query.value("landing_time").toDateTime();
        item["departure_time"] = dep.toString("yyyy-MM-dd HH:mm");
        item["landing_time"] = arr.toString("HH:mm");

        item["seat_number"] = query.value("seat_number").toString();

        int type = query.value("seat_type").toInt();
        if(type == 0) item["seat_class"] = "经济舱";
        else if(type == 1) item["seat_class"] = "商务舱";
        else item["seat_class"] = "头等舱";

        list.append(item);
    }

    QJsonObject resp;
    resp["status"] = "success";
    resp["message"] = "成功返回用户订单数据";
    resp["data"] = list;
    return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Ok);
}

// =============================================================
// 3. 取消订单
// 请求 JSON: { "order_id": 1001, "user_id": 1 }
// (带上 user_id 是为了安全，防止删别人的订单)
// =============================================================
QHttpServerResponse OrderController::handleCancelOrder(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("order_id") || !jsonObj.contains("user_id")) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    }

    int orderId = jsonObj["order_id"].toInt();
    int userId = jsonObj["user_id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()){
        QJsonObject err; err["status"] = "failed"; err["message"] = "数据库连接失败";
        return QHttpServerResponse(err,QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);

    // 【核心修改】这里不再是 DELETE，而是 UPDATE
    // 只有当前状态不是 "已取消" 时才更新，防止重复操作（可选）
    query.prepare("UPDATE orders SET status = '已取消' WHERE ID = ? AND user_id = ?");

    query.addBindValue(orderId);
    query.addBindValue(userId);

    if (!query.exec()) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "数据库错误";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (query.numRowsAffected() > 0) {
        QJsonObject success; success["status"] = "success"; success["message"] = "订单已取消";
        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
    } else {
        QJsonObject fail; fail["status"] = "failed"; fail["message"] = "订单不存在或无法操作";
        return QHttpServerResponse(fail, QHttpServerResponse::StatusCode::NotFound);
    }
}
