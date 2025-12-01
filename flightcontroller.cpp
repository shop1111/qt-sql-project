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
    // POST /api/flights -> 创建航班
    server->route("/api/search_flights", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleSearchFlights(req);
                  });
}

QString FlightController::getCityNameByCode(const QString &code)
{
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) return code;

    QSqlQuery query(db);
    query.prepare("SELECT city_name FROM city_codes WHERE city_code = ?");
    query.addBindValue(code);

    if (query.exec() && query.next()) {
        return query.value("city_name").toString();
    }

    // 如果查不到（可能是前端直接传了中文，或者代码错误），直接返回原字符串尝试去匹配
    return code;
}

// ------------------------------------------------------------------
// 核心：处理航班搜索
// ------------------------------------------------------------------
QHttpServerResponse FlightController::handleSearchFlights(const QHttpServerRequest &request)
{
    // 1. 解析请求体 JSON
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "Invalid JSON format";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject reqObj = jsonDoc.object();
    QString depCode = reqObj["departure_city"].toString(); // 出发城市代码
    QString arrCode = reqObj["arrival_city"].toString();   // 前往的城市代码
    QString dateStr = reqObj["departure_date"].toString(); // 2025-11-28
    QString seatClass = reqObj["seat_class"].toString();   // 经济舱 or 公务/头等舱

    qDebug() << "Search Request:" << depCode << "->" << arrCode << "on" << dateStr << "Class:" << seatClass;

    // 2. 将代码转换为数据库中的中文城市名
    QString depCity = getCityNameByCode(depCode);
    QString arrCity = getCityNameByCode(arrCode);

    qDebug() << "Converted City:" << depCity << "->" << arrCity;

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 3. 构造查询语句
    // 注意：flights 表里的 departure_time 是 DATETIME，我们需要用 DATE() 函数截取日期部分进行比对
    QSqlQuery query(db);
    QString sql = "SELECT * FROM flights WHERE origin = ? AND destination = ? AND DATE(departure_time) = ?";

    query.prepare(sql);
    query.addBindValue(depCity);
    query.addBindValue(arrCity);
    query.addBindValue(dateStr);

    if (!query.exec()) {
        qWarning() << "Search SQL Error:" << query.lastError().text();
        QJsonObject err;
        err["status"] = "error";
        err["message"] = "Database query error";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 4. 组装返回结果
    QJsonArray flightList;
    while (query.next()) {
        QJsonObject flight;

        // --- 基础信息 ---
        flight["id"] = query.value("ID").toInt();
        flight["flight_number"] = query.value("flight_number").toString(); // 前端叫 flight_no
        flight["airline"] = query.value("airline").toString(); //航空公司
        flight["aircraft_model"] = query.value("aircraft_model").toString();    // 机型

        // --- 时间处理 ---
        // 数据库取出来可能是 "2025-12-01T08:00:00.000"
        QDateTime depTime = query.value("departure_time").toDateTime();
        QDateTime arrTime = query.value("landing_time").toDateTime();

        // 前端只需要 "08:00" 这种格式显示在列表上
        flight["departure_time"] = depTime.toString("HH:mm");
        flight["landing_time"] = arrTime.toString("HH:mm");

        // // --- 机场信息 (如果数据库没有具体航站楼，就暂时用城市名代替) ---
        // flight["departure_airport"] = depCity + "机场";
        // flight["landing_airport"] = arrCity + "机场";

        // --- 价格逻辑 ---
        // 根据前端选的舱位返回对应价格
        int price = 0;
        int seats = 0;

        // 简单判断字符串包含关系，兼容 "公务/头等舱" 写法
        if (seatClass.contains("经济")) {
            price = query.value("economy_price").toInt();
            seats = query.value("economy_seats").toInt();
        } else if (seatClass.contains("公务") || seatClass.contains("商务")) {
            price = query.value("business_price").toInt();
            seats = query.value("business_seats").toInt();
        } else {
            // 默认或者头等舱
            price = query.value("first_class_price").toInt();
            seats = query.value("first_class_seats").toInt();
        }

        flight["price"] = price;
        flight["seats_left"] = seats; // 可以在前端显示余票

        flightList.append(flight);
    }

    // 5. 最终返回结构
    QJsonObject responseObj;
    responseObj["status"] = "success";
    responseObj["data"] = flightList;
    // 如果列表为空，message 可以提示一下，但 status 依然是 success
    if (flightList.isEmpty()) {
        responseObj["message"] = "未找到符合条件的航班";
    }
    responseObj["message"] = "成功返回航班";
    return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
}
