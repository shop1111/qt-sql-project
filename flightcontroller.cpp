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
    server->route("/api/search_flights", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleSearchFlights(req);
                  });

    // [新增] 管理员添加航班
    server->route("/api/admin/add_flight", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleAddFlight(req);
                  });

    // [新增] 管理员修改航班
    server->route("/api/admin/update_flight", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleUpdateFlight(req);
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

        // // --- 价格逻辑 ---
        // // 根据前端选的舱位返回对应价格
        // int price = 0;
        // int seats = 0;
        // 简单判断字符串包含关系，兼容 "公务/头等舱" 写法
        // if (seatClass.contains("经济")) {
        //     price = query.value("economy_price").toInt();
        //     seats = query.value("economy_seats").toInt();
        // } else if (seatClass.contains("公务") || seatClass.contains("商务")) {
        //     price = query.value("business_price").toInt();
        //     seats = query.value("business_seats").toInt();
        // } else {
        //     // 默认或者头等舱
        //     price = query.value("first_class_price").toInt();
        //     seats = query.value("first_class_seats").toInt();
        // }

        flight["economy_price"] = query.value("economy_price").toInt();
        flight["economy_seats"] = query.value("economy_seats").toInt();
        flight["business_price"] = query.value("business_price").toInt();
        flight["business_seats"] = query.value("business_seats").toInt();
        flight["first_class_price"] = query.value("first_class_price").toInt();
        flight["first_class_seats"] = query.value("first_class_seats").toInt();

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


// ------------------------------------------------------------------
// 管理员功能：添加航班
// ------------------------------------------------------------------
/* { "flight_number": "MU5555", "origin": "BJS", "destination": "SHA", "departure_date": "2025-12-01", "departure_time": "08:00",
 * "landing_date": "2025-12-01", "landing_time": "10:30",
 * "airline": "东航", "aircraft_model": "737", "economy_seats": 100, "economy_price": 500 ... }
 * 返回：{ "status": "success", "flight_id": 55(航班ID) } */
QHttpServerResponse FlightController::handleAddFlight(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    // 1. 简单校验必填字段
    if (!jsonObj.contains("flight_number") || !jsonObj.contains("date") || !jsonObj.contains("origin")) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "参数缺失";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // 2. 解析参数
    QString flightNo = jsonObj["flight_number"].toString();
    // 城市处理：尝试将三字码(BJS)转为中文(北京)，保持数据库一致性
    QString origin = getCityNameByCode(jsonObj["origin"].toString());
    QString dest = getCityNameByCode(jsonObj["destination"].toString());

    // 时间处理：前端通常传 "2025-12-01" 和 "08:00"，后端需要拼成 DATETIME
    QString depDateStr = jsonObj["departure_date"].toString();
    QString arrDateStr = jsonObj["landing_date"].toString();
    QString depTimeStr = jsonObj["departure_time"].toString(); // "08:00"
    QString arrTimeStr = jsonObj["landing_time"].toString();   // "10:30"

    QString fullDepTime = depDateStr + " " + depTimeStr + ":00";
    QString fullArrTime = arrDateStr + " " + arrTimeStr + ":00"; // 暂未处理跨天逻辑，可根据需要优化

    QString airline = jsonObj["airline"].toString();
    QString model = jsonObj["aircraft_model"].toString();

    // 舱位与价格
    int ecoSeats = jsonObj["economy_seats"].toInt();
    int ecoPrice = jsonObj["economy_price"].toInt();
    int busSeats = jsonObj["business_seats"].toInt();
    int busPrice = jsonObj["business_price"].toInt();
    int firSeats = jsonObj["first_class_seats"].toInt();
    int firPrice = jsonObj["first_class_price"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);

    QSqlQuery query(db);
    QString sql = R"(
        INSERT INTO flights (
            flight_number, origin, destination, departure_time, landing_time,
            airline, aircraft_model,
            economy_seats, economy_price,
            business_seats, business_price,
            first_class_seats, first_class_price
        ) VALUES (
            ?, ?, ?, ?, ?,
            ?, ?,
            ?, ?,
            ?, ?,
            ?, ?
        )
    )";

    query.prepare(sql);
    query.addBindValue(flightNo);
    query.addBindValue(origin);
    query.addBindValue(dest);
    query.addBindValue(fullDepTime);
    query.addBindValue(fullArrTime);
    query.addBindValue(airline);
    query.addBindValue(model);
    query.addBindValue(ecoSeats); query.addBindValue(ecoPrice);
    query.addBindValue(busSeats); query.addBindValue(busPrice);
    query.addBindValue(firSeats); query.addBindValue(firPrice);

    if (!query.exec()) {
        qWarning() << "Add Flight Error:" << query.lastError().text();
        QJsonObject err; err["status"] = "failed"; err["message"] = "添加航班失败: " + query.lastError().text();
        qInfo()<<"err: 添加航班失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "航班添加成功";
    qInfo()<<"航班添加成功";
    success["flight_id"] = query.lastInsertId().toInt();
    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}

// ------------------------------------------------------------------
// 管理员功能：修改航班信息
// 需要{ "flight_id": 55, "economy_price"（待修改字段）: 600 }
// 城市传三字码或者CODE都行
// ------------------------------------------------------------------
QHttpServerResponse FlightController::handleUpdateFlight(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("flight_id")) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "必须指定 flight_id";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int flightId = jsonObj["flight_id"].toInt();
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);

    // 动态构建 UPDATE 语句，只更新前端传过来的字段
    // 这种写法比较灵活，前端只传需要修改的字段即可
    QStringList setClauses;
    QVariantList boundValues;

    if (jsonObj.contains("flight_number")) {
        setClauses << "flight_number = ?";
        boundValues << jsonObj["flight_number"].toString();
    }
    if (jsonObj.contains("airline")) {
        setClauses << "airline = ?";
        boundValues << jsonObj["airline"].toString();
    }
    if (jsonObj.contains("aircraft_model")) {
        setClauses << "aircraft_model = ?";
        boundValues << jsonObj["aircraft_model"].toString();
    }

    // 2. 城市 (注意：使用 getCityNameByCode 进行转义，防止存入三字码)
    if (jsonObj.contains("origin")) {
        setClauses << "origin = ?";
        boundValues << getCityNameByCode(jsonObj["origin"].toString());
    }
    if (jsonObj.contains("destination")) {
        setClauses << "destination = ?";
        boundValues << getCityNameByCode(jsonObj["destination"].toString());
    }

    // 3. 时间 (假设前端传过来的是标准格式字符串 "yyyy-MM-dd HH:mm:ss")
    if (jsonObj.contains("departure_time")) {
        setClauses << "departure_time = ?";
        boundValues << jsonObj["departure_time"].toString();
    }
    if (jsonObj.contains("landing_time")) {
        setClauses << "landing_time = ?";
        boundValues << jsonObj["landing_time"].toString();
    }

    // 4. 经济舱配置
    if (jsonObj.contains("economy_seats")) {
        setClauses << "economy_seats = ?";
        boundValues << jsonObj["economy_seats"].toInt();
    }
    if (jsonObj.contains("economy_price")) {
        setClauses << "economy_price = ?";
        boundValues << jsonObj["economy_price"].toInt();
    }

    // 5. 商务舱配置
    if (jsonObj.contains("business_seats")) {
        setClauses << "business_seats = ?";
        boundValues << jsonObj["business_seats"].toInt();
    }
    if (jsonObj.contains("business_price")) {
        setClauses << "business_price = ?";
        boundValues << jsonObj["business_price"].toInt();
    }

    // 6. 头等舱配置
    if (jsonObj.contains("first_class_seats")) {
        setClauses << "first_class_seats = ?";
        boundValues << jsonObj["first_class_seats"].toInt();
    }
    if (jsonObj.contains("first_class_price")) {
        setClauses << "first_class_price = ?";
        boundValues << jsonObj["first_class_price"].toInt();
    }

    // 如果没有要修改的字段
    if (setClauses.isEmpty()) {
        QJsonObject resp; resp["status"] = "success"; resp["message"] = "无数据变更";
        return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Ok);
    }

    QString sql = "UPDATE flights SET " + setClauses.join(", ") + " WHERE ID = ?";
    boundValues << flightId; // 最后一个绑定的是 ID

    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant &val : boundValues) {
        query.addBindValue(val);
    }

    if (!query.exec()) {
        qWarning() << "Update Flight Error:" << query.lastError().text();
        QJsonObject err; err["status"] = "failed"; err["message"] = "更新失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "航班信息已更新";
    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}
