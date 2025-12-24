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

    // [新增] 管理员删除航班
    server->route("/api/admin/delete_flight", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleDeleteFlight(req);
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
    if (!jsonObj.contains("flight_number")|| !jsonObj.contains("origin")) {
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
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    }

    int flightId = jsonObj["flight_id"].toInt();
    QSqlDatabase db = DatabaseManager::getConnection();

    QStringList setClauses;
    QVariantList boundValues;

    // ----------------------------------------------------------------
    // 1. 自动合并时间 (前端分拆 -> 数据库合并)
    // ----------------------------------------------------------------
    // 只要前端传了，就直接覆盖更新
    if (jsonObj.contains("departure_date") && jsonObj.contains("departure_time")) {
        QString fullTime = jsonObj["departure_date"].toString() + " " + jsonObj["departure_time"].toString() + ":00";
        setClauses << "departure_time = ?";
        boundValues << fullTime;
    }
    if (jsonObj.contains("landing_date") && jsonObj.contains("landing_time")) {
        QString fullTime = jsonObj["landing_date"].toString() + " " + jsonObj["landing_time"].toString() + ":00";
        setClauses << "landing_time = ?";
        boundValues << fullTime;
    }

    // ----------------------------------------------------------------
    // 2. 城市转义 (前端代码 -> 数据库中文)
    // ----------------------------------------------------------------
    if (jsonObj.contains("origin")) {
        setClauses << "origin = ?";
        boundValues << getCityNameByCode(jsonObj["origin"].toString());
    }
    if (jsonObj.contains("destination")) {
        setClauses << "destination = ?";
        boundValues << getCityNameByCode(jsonObj["destination"].toString());
    }

    // ----------------------------------------------------------------
    // 3. 其他所有字段 (直接按值更新)
    // ----------------------------------------------------------------
    // 既然前端是全量打包，这里的循环会把所有字段都加进 SQL
    QMap<QString, QString> fieldMap;
    fieldMap.insert("flight_number", "flight_number");
    fieldMap.insert("airline", "airline");
    fieldMap.insert("aircraft_model", "aircraft_model");
    fieldMap.insert("economy_seats", "economy_seats");
    fieldMap.insert("economy_price", "economy_price");
    fieldMap.insert("business_seats", "business_seats");
    fieldMap.insert("business_price", "business_price");
    fieldMap.insert("first_class_seats", "first_class_seats");
    fieldMap.insert("first_class_price", "first_class_price");

    QMapIterator<QString, QString> i(fieldMap);
    while (i.hasNext()) {
        i.next();
        if (jsonObj.contains(i.key())) {
            setClauses << QString("%1 = ?").arg(i.value());
            boundValues << jsonObj[i.key()].toVariant();
        }
    }

    // ----------------------------------------------------------------
    // 4. 执行全量 UPDATE
    // ----------------------------------------------------------------
    if (setClauses.isEmpty()) return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);

    QString sql = "UPDATE flights SET " + setClauses.join(", ") + " WHERE ID = ?";
    boundValues << flightId;

    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant &val : boundValues) query.addBindValue(val);

    if (query.exec()) {
        QJsonObject success; success["status"] = "success"; success["message"] = "更新成功";
        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
    } else {
        qWarning() << "SQL Error:" << query.lastError().text();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }
}

QHttpServerResponse FlightController::handleDeleteFlight(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);

    if (jsonObj.contains("flight_id")) {
        query.prepare("DELETE FROM flights WHERE ID = ?");
        query.addBindValue(jsonObj["flight_id"].toInt());
    }else {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "参数缺失: 需要 flight_id 或 flight_number";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // 3. 执行删除
    if (!query.exec()) {
        qWarning() << "Delete Flight Error:" << query.lastError().text();
        QJsonObject err;
        err["status"] = "failed";
        // 提示：如果 flight_system.sql 里没有设置 ON DELETE CASCADE，这里可能会因为有订单关联而报错
        err["message"] = "删除失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 4. 检查是否有数据被删除
    if (query.numRowsAffected() > 0) {
        QJsonObject success;
        success["status"] = "success";
        success["message"] = "航班已删除";
        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
    } else {
        QJsonObject fail;
        fail["status"] = "failed";
        fail["message"] = "未找到该航班，删除失败";
        return QHttpServerResponse(fail, QHttpServerResponse::StatusCode::NotFound);
    }
}
