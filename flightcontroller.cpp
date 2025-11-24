#include "FlightController.h"
#include "DatabaseManager.h" // 一定要包含这个，用来连数据库

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

FlightController::FlightController(QObject *parent) : BaseController(parent)
{
}

// 核心：注册路由
void FlightController::registerRoutes(QHttpServer *server)
{
    // GET /api/flights -> 查询航班
    server->route("/api/flights", QHttpServerRequest::Method::Get,
                  [this](const QHttpServerRequest &req) {
                      return handleGetFlights(req);
                  });

    // POST /api/flights -> 创建航班
    server->route("/api/flights", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handlePostFlights(req);
                  });
}

QHttpServerResponse FlightController::handleGetFlights(const QHttpServerRequest &request)
{
    QJsonArray flightsArray;
    QSqlDatabase database = DatabaseManager::getConnection();

    if (!database.isOpen()) {
        QJsonObject errorObj;
        errorObj["error"] = "Internal server error - DB not open";
        return QHttpServerResponse(errorObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(database);
    // 注意：这里的 SQL 依然保持不变
    if (!query.exec("SELECT id, flight_number, origin, destination, DATE_FORMAT(departure_time, '%Y-%m-%dT%H:%i:%s') as departure_time FROM flights")) {
        qWarning() << "GET /flights SQL Error:" << query.lastError().text();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    while (query.next()) {
        QJsonObject flight;
        flight["id"] = query.value("id").toInt();
        flight["flight_number"] = query.value("flight_number").toString();
        flight["origin"] = query.value("origin").toString();
        flight["destination"] = query.value("destination").toString();
        flight["departure_time"] = query.value("departure_time").toString();
        flightsArray.append(flight);
    }
    return QHttpServerResponse(flightsArray);
}

// ---------------------------------------------------------
// 下面的代码直接搬运自原本的 handlePostFlights
// ---------------------------------------------------------
QHttpServerResponse FlightController::handlePostFlights(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return QHttpServerResponse("Invalid JSON", QHttpServerResponse::StatusCode::BadRequest);
    }
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("flight_number") || !jsonObj.contains("origin") || !jsonObj.contains("destination")) {
        return QHttpServerResponse("Missing required fields", QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase database = DatabaseManager::getConnection();
    if (!database.isOpen()) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(database);
    query.prepare("INSERT INTO flights (flight_number, origin, destination) VALUES (?, ?, ?)");
    query.addBindValue(jsonObj["flight_number"].toString());
    query.addBindValue(jsonObj["origin"].toString());
    query.addBindValue(jsonObj["destination"].toString());

    if (!query.exec()) {
        qWarning() << "POST /flights SQL Error:" << query.lastError().text();
        return QHttpServerResponse("Creation Failed", QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject successObj;
    successObj["message"] = "Flight created successfully";
    return QHttpServerResponse(successObj, QHttpServerResponse::StatusCode::Created);
}
