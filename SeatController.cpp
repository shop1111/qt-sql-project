#include "SeatController.h"
#include "DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

SeatController::SeatController(QObject *parent) : BaseController(parent) {}

void SeatController::registerRoutes(QHttpServer *server) {//注册3个HTTP路由
    //处理航班座位的选择，使用 POST 方法调用 handleSeatSelection 处理
    server->route("/api/seats/<flightId>/available", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleSeatSelection(req);
                  });
    //POST 方法调用 handleSeatLock 处理
    server->route("/api/seats/lock", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleSeatLock(req);
                  });
    //查询指定航班座位状态，使用 POST方向handleSeatStatus 处理
    server->route("/api/seats/<flightId>/status", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleSeatStatus(req);
                  });
}
QHttpServerResponse SeatController::handleSeatSelection(const QHttpServerRequest &request) {
    QString flightId = request.url().path().section('/', 3, 3);

    if (flightId.isEmpty()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "航班ID不能为空";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "数据库无法打开";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("SELECT seat_number FROM orders WHERE flight_id = ? AND status != 'booked'");
    query.addBindValue(flightId);

    if (!query.exec()) {
        qWarning() << "Seat selection SQL Error:" << query.lastError().text();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "查询可用座位失败";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray availableSeats;
    while (query.next()) {
        availableSeats.append(query.value("seat_number").toString());
    }

    QJsonObject responseObj;
    responseObj["status"] = "success";
    responseObj["message"] = "获取可用座位成功";
    responseObj["data"] = availableSeats;

    return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse SeatController::handleSeatLock(const QHttpServerRequest &request) {
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "无法解析出JSON对象";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();

    // 验证必需字段
    if (!reqObj.contains("flight_id") || !reqObj.contains("seat_number")) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "缺少必要字段";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QString flightId = reqObj["flight_id"].toString();
    QString seatNumber = reqObj["seat_number"].toString();

    if (flightId.isEmpty() || seatNumber.isEmpty()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "航班ID或座位号不能为空";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "数据库无法打开";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("UPDATE orders SET status = 'locked' WHERE flight_id = ? AND seat_number = ?");
    query.addBindValue(flightId);
    query.addBindValue(seatNumber);

    if (!query.exec()) {
        qWarning() << "Seat lock SQL Error:" << query.lastError().text();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "锁定座位失败";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 检查是否实际更新了记录
    if (query.numRowsAffected() == 0) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "未找到指定座位";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::NotFound);
    }

    QJsonObject responseObj;
    responseObj["status"] = "success";
    responseObj["message"] = "座位锁定成功";

    return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse SeatController::handleSeatStatus(const QHttpServerRequest &request) {
    QString flightId = request.url().path().section('/', 3, 3);

    if (flightId.isEmpty()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "航班ID不能为空";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "数据库无法打开";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("SELECT seat_number, status FROM orders WHERE flight_id = ?");
    query.addBindValue(flightId);

    if (!query.exec()) {
        qWarning() << "Seat status SQL Error:" << query.lastError().text();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "查询座位状态失败";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray seatStatusList;
    while (query.next()) {
        QJsonObject seat;
        seat["seat_number"] = query.value("seat_number").toString();
        seat["status"] = query.value("status").toString();
        seatStatusList.append(seat);
    }

    QJsonObject responseObj;
    responseObj["status"] = "success";
    responseObj["message"] = "获取座位状态成功";
    responseObj["data"] = seatStatusList;

    return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
}
