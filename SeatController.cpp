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
    // 添加解锁路由
    server->route("/api/seats/unlock", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleSeatUnlock(req);
                  });
    // 获取航班座位图（包括所有座位状态）
    //server->route("/api/seats/<flightId>/map", QHttpServerRequest::Method::Post,
                  //[this](const QHttpServerRequest &req) {
                    //  return handleSeatMap(req);
                  //});
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
    // 修改查询：只返回未锁定且未支付的座位
    //超过15min自动解锁
    query.prepare("SELECT seat_number FROM orders WHERE flight_id = ? AND status = '未支付' AND (lock_time IS NULL OR TIMESTAMPDIFF(SECOND, lock_time, NOW()) > 900)");
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

    // 开始事务
    db.transaction();

    // 检查座位是否已被锁定（使用悲观锁 FOR UPDATE）
    QSqlQuery checkQuery(db);
    checkQuery.prepare("SELECT status, lock_time FROM orders WHERE flight_id = ? AND seat_number = ? FOR UPDATE");
    checkQuery.addBindValue(flightId);
    checkQuery.addBindValue(seatNumber);

    if (!checkQuery.exec() || !checkQuery.next()) {
        db.rollback();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "座位不存在";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::NotFound);
    }

    QString currentStatus = checkQuery.value("status").toString();
    QDateTime lockTime = checkQuery.value("lock_time").toDateTime();

    // 检查是否可以锁定
    if (currentStatus == "已锁定") {
        // 检查是否过期（假设锁定15分钟）
        if (lockTime.isValid() && lockTime.addSecs(15 * 60) > QDateTime::currentDateTime()) {
            db.rollback();
            QJsonObject responseObj;
            responseObj["status"] = "failed";
            responseObj["message"] = "座位已被锁定";
            return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Conflict);
        }
    }

    // 更新座位状态为已锁定
    QSqlQuery updateQuery(db);
    updateQuery.prepare("UPDATE orders SET status = '已锁定', lock_time = NOW() WHERE flight_id = ? AND seat_number = ?");
    updateQuery.addBindValue(flightId);
    updateQuery.addBindValue(seatNumber);

    if (!updateQuery.exec()) {
        db.rollback();
        qWarning() << "Seat lock SQL Error:" << updateQuery.lastError().text();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "锁定座位失败";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 检查是否实际更新了记录
    if (updateQuery.numRowsAffected() == 0) {
        db.rollback();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "未找到指定座位";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::NotFound);
    }

    db.commit();

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

QHttpServerResponse SeatController::handleSeatUnlock(const QHttpServerRequest &request) {
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "无法解析JSON";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();
    if (!reqObj.contains("flight_id") || !reqObj.contains("seat_number")) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "缺少必要字段";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QString flightId = reqObj["flight_id"].toString();
    QString seatNumber = reqObj["seat_number"].toString();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "数据库无法打开";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    db.transaction();
    QSqlQuery updateQuery(db);
    updateQuery.prepare("UPDATE orders SET status = '未支付', lock_time = NULL WHERE flight_id = ? AND seat_number = ? AND status = '已锁定'");
    updateQuery.addBindValue(flightId);
    updateQuery.addBindValue(seatNumber);

    if (!updateQuery.exec()) {
        db.rollback();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "解锁座位失败";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (updateQuery.numRowsAffected() == 0) {
        db.rollback();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "座位未锁定或不存在";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::NotFound);
    }

    db.commit();

    QJsonObject responseObj;
    responseObj["status"] = "success";
    responseObj["message"] = "座位解锁成功";
    return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
}
