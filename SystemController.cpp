#include "SystemController.h"
#include "DatabaseManager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

SystemController::SystemController(QObject *parent) : BaseController(parent)
{
}

void SystemController::registerRoutes(QHttpServer *server)
{
    // 航班管理
    server->route("/system/api/flight/add", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleAddFlight(req); });
    server->route("/system/api/flight/update", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleUpdateFlight(req); });
    server->route("/system/api/flight/delete", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleDeleteFlight(req); });

    // 用户管理
    server->route("/system/api/user/add", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleAddUser(req); });
    server->route("/system/api/user/update", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleUpdateUser(req); });
    server->route("/system/api/user/delete", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleDeleteUser(req); });

    // 数据统计
    server->route("/system/api/statistics/orders", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleOrderStatistics(req); });
    server->route("/system/api/statistics/flight_occupancy", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleFlightOccupancyStatistics(req); });
}

// 1. 添加航班 - 修正：添加所有必要字段
QHttpServerResponse SystemController::handleAddFlight(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "JSON格式错误";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    // 检查必要参数
    QStringList requiredFields = {"flight_number", "origin", "destination", "departure_time",
                                  "landing_time", "airline", "aircraft_model", "economy_seats",
                                  "economy_price", "business_seats", "business_price",
                                  "first_class_seats", "first_class_price"};

    for (const QString &field : requiredFields) {
        if (!jsonObj.contains(field)) {
            QJsonObject err;
            err["status"] = "failed";
            err["message"] = QString("参数缺失（需要 %1）").arg(field);
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
    }

    // 提取参数值
    QString flightNumber = jsonObj["flight_number"].toString();
    QString origin = jsonObj["origin"].toString();
    QString destination = jsonObj["destination"].toString();
    QString departureTime = jsonObj["departure_time"].toString();
    QString landingTime = jsonObj["landing_time"].toString();
    QString airline = jsonObj["airline"].toString();
    QString aircraftModel = jsonObj["aircraft_model"].toString();
    int economySeats = jsonObj["economy_seats"].toInt();
    int economyPrice = jsonObj["economy_price"].toInt();
    int businessSeats = jsonObj["business_seats"].toInt();
    int businessPrice = jsonObj["business_price"].toInt();
    int firstClassSeats = jsonObj["first_class_seats"].toInt();
    int firstClassPrice = jsonObj["first_class_price"].toInt();

    // 参数验证
    if (flightNumber.isEmpty() || origin.isEmpty() || destination.isEmpty() ||
        departureTime.isEmpty() || landingTime.isEmpty() || airline.isEmpty() ||
        aircraftModel.isEmpty()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "必要参数不能为空";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 绑定参数值 - 修正：添加所有字段
    QSqlQuery query(db);
    query.prepare("INSERT INTO flights (flight_number, origin, destination, departure_time, "
                  "landing_time, airline, aircraft_model, economy_seats, economy_price, "
                  "business_seats, business_price, first_class_seats, first_class_price) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    query.addBindValue(flightNumber);
    query.addBindValue(origin);
    query.addBindValue(destination);
    query.addBindValue(departureTime);
    query.addBindValue(landingTime);
    query.addBindValue(airline);
    query.addBindValue(aircraftModel);
    query.addBindValue(economySeats);
    query.addBindValue(economyPrice);
    query.addBindValue(businessSeats);
    query.addBindValue(businessPrice);
    query.addBindValue(firstClassSeats);
    query.addBindValue(firstClassPrice);

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        if (query.lastError().text().contains("Duplicate")) {
            err["message"] = "航班号已存在";
        } else {
            err["message"] = "添加航班失败: " + query.lastError().text();
        }
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "航班添加成功";

    QJsonObject data;
    data["flight_id"] = query.lastInsertId().toInt();
    success["data"] = data;

    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}

// 2. 更新航班 - 改进版：支持更新所有字段
QHttpServerResponse SystemController::handleUpdateFlight(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "JSON格式错误";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    // 检查必要参数
    if (!jsonObj.contains("id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "缺少航班ID参数";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int flightId = jsonObj["id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 提取所有可更新字段
    QString flightNumber = jsonObj.value("flight_number").toString();
    QString origin = jsonObj.value("origin").toString();
    QString destination = jsonObj.value("destination").toString();
    QString departureTime = jsonObj.value("departure_time").toString();
    QString landingTime = jsonObj.value("landing_time").toString();
    QString airline = jsonObj.value("airline").toString();
    QString aircraftModel = jsonObj.value("aircraft_model").toString();
    int economySeats = jsonObj.value("economy_seats").toInt();
    int economyPrice = jsonObj.value("economy_price").toInt();
    int businessSeats = jsonObj.value("business_seats").toInt();
    int businessPrice = jsonObj.value("business_price").toInt();
    int firstClassSeats = jsonObj.value("first_class_seats").toInt();
    int firstClassPrice = jsonObj.value("first_class_price").toInt();

    // 动态构建 UPDATE 语句，只更新提供的字段
    QStringList updateFields;
    QVariantList bindValues;

    if (!flightNumber.isEmpty()) {
        updateFields << "flight_number = ?";
        bindValues << flightNumber;
    }
    if (!origin.isEmpty()) {
        updateFields << "origin = ?";
        bindValues << origin;
    }
    if (!destination.isEmpty()) {
        updateFields << "destination = ?";
        bindValues << destination;
    }
    if (!departureTime.isEmpty()) {
        updateFields << "departure_time = ?";
        bindValues << departureTime;
    }
    if (!landingTime.isEmpty()) {
        updateFields << "landing_time = ?";
        bindValues << landingTime;
    }
    if (!airline.isEmpty()) {
        updateFields << "airline = ?";
        bindValues << airline;
    }
    if (!aircraftModel.isEmpty()) {
        updateFields << "aircraft_model = ?";
        bindValues << aircraftModel;
    }
    if (jsonObj.contains("economy_seats")) {
        updateFields << "economy_seats = ?";
        bindValues << economySeats;
    }
    if (jsonObj.contains("economy_price")) {
        updateFields << "economy_price = ?";
        bindValues << economyPrice;
    }
    if (jsonObj.contains("business_seats")) {
        updateFields << "business_seats = ?";
        bindValues << businessSeats;
    }
    if (jsonObj.contains("business_price")) {
        updateFields << "business_price = ?";
        bindValues << businessPrice;
    }
    if (jsonObj.contains("first_class_seats")) {
        updateFields << "first_class_seats = ?";
        bindValues << firstClassSeats;
    }
    if (jsonObj.contains("first_class_price")) {
        updateFields << "first_class_price = ?";
        bindValues << firstClassPrice;
    }

    if (updateFields.isEmpty()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "没有提供要更新的字段";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // 构建完整的 SQL 语句
    QString sql = "UPDATE flights SET " + updateFields.join(", ") + " WHERE ID = ?";
    bindValues << flightId;

    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant &value : bindValues) {
        query.addBindValue(value);
    }

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "更新航班失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (query.numRowsAffected() > 0) {
        QJsonObject success;
        success["status"] = "success";
        success["message"] = "航班更新成功";
        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
    } else {
        QJsonObject fail;
        fail["status"] = "failed";
        fail["message"] = "航班不存在";
        return QHttpServerResponse(fail, QHttpServerResponse::StatusCode::NotFound);
    }
}

// 3. 删除航班
QHttpServerResponse SystemController::handleDeleteFlight(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "JSON格式错误";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "缺少航班ID参数";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int flightId = jsonObj["id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM flights WHERE ID = ?");
    query.addBindValue(flightId);

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "删除航班失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (query.numRowsAffected() > 0) {
        QJsonObject success;
        success["status"] = "success";
        success["message"] = "航班删除成功";
        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
    } else {
        QJsonObject fail;
        fail["status"] = "failed";
        fail["message"] = "航班不存在";
        return QHttpServerResponse(fail, QHttpServerResponse::StatusCode::NotFound);
    }
}

// 4. 添加用户
QHttpServerResponse SystemController::handleAddUser(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "JSON格式错误";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    // 检查必要参数
    if (!jsonObj.contains("username") || !jsonObj.contains("password") ||
        !jsonObj.contains("telephone")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "参数缺失（需要username, password, telephone）";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QString username = jsonObj["username"].toString();
    QString password = jsonObj["password"].toString();
    QString telephone = jsonObj["telephone"].toString();
    QString trueName = jsonObj.value("true_name").toString();
    QString pid = jsonObj.value("P_ID").toString();

    // 参数验证
    if (username.isEmpty() || password.isEmpty() || telephone.isEmpty()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "用户名、密码和电话不能为空";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("INSERT INTO users (username, password, true_name, telephone, P_ID) "
                  "VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(username);
    query.addBindValue(password);
    query.addBindValue(trueName);
    query.addBindValue(telephone);
    query.addBindValue(pid);

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        QString errorText = query.lastError().text();
        if (errorText.contains("Duplicate")) {
            if (errorText.contains("unique_username")) {
                err["message"] = "用户名已存在";
            } else if (errorText.contains("unique_tele")) {
                err["message"] = "电话号码已存在";
            } else if (errorText.contains("unique_pid")) {
                err["message"] = "身份证号已存在";
            } else {
                err["message"] = "数据重复: " + errorText;
            }
        } else {
            err["message"] = "添加用户失败: " + errorText;
        }
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "用户添加成功";

    QJsonObject data;
    data["user_id"] = query.lastInsertId().toInt();
    success["data"] = data;

    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}

// 5. 更新用户 - 修正：使用 U_ID 作为主键
QHttpServerResponse SystemController::handleUpdateUser(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "JSON格式错误";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "缺少用户ID参数";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int userId = jsonObj["id"].toInt();
    QString username = jsonObj["username"].toString();
    QString password = jsonObj["password"].toString();
    QString trueName = jsonObj["true_name"].toString();
    QString telephone = jsonObj["telephone"].toString();
    QString pid = jsonObj["P_ID"].toString();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 修正：使用 U_ID 而不是 ID
    QSqlQuery query(db);
    query.prepare("UPDATE users SET username = ?, password = ?, true_name = ?, "
                  "telephone = ?, P_ID = ? WHERE U_ID = ?");
    query.addBindValue(username);
    query.addBindValue(password);
    query.addBindValue(trueName);
    query.addBindValue(telephone);
    query.addBindValue(pid);
    query.addBindValue(userId);

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "更新用户失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (query.numRowsAffected() > 0) {
        QJsonObject success;
        success["status"] = "success";
        success["message"] = "用户更新成功";
        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
    } else {
        QJsonObject fail;
        fail["status"] = "failed";
        fail["message"] = "用户不存在";
        return QHttpServerResponse(fail, QHttpServerResponse::StatusCode::NotFound);
    }
}

// 6. 删除用户 - 修正：使用 U_ID 作为主键
QHttpServerResponse SystemController::handleDeleteUser(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "JSON格式错误";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "缺少用户ID参数";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int userId = jsonObj["id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 修正：使用 U_ID 而不是 ID
    QSqlQuery query(db);
    query.prepare("DELETE FROM users WHERE U_ID = ?");
    query.addBindValue(userId);

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "删除用户失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (query.numRowsAffected() > 0) {
        QJsonObject success;
        success["status"] = "success";
        success["message"] = "用户删除成功";
        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
    } else {
        QJsonObject fail;
        fail["status"] = "failed";
        fail["message"] = "用户不存在";
        return QHttpServerResponse(fail, QHttpServerResponse::StatusCode::NotFound);
    }
}

// 7. 订单统计 - 可选改进：考虑状态过滤
QHttpServerResponse SystemController::handleOrderStatistics(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject filterParams;

    // 可选：解析过滤参数
    if (jsonDoc.isObject()) {
        filterParams = jsonDoc.object();
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 基本统计查询，可选添加状态过滤
    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) AS total_orders FROM orders");

    // 可选：根据参数添加过滤条件
    if (filterParams.contains("status")) {
        QString status = filterParams["status"].toString();
        query.prepare("SELECT COUNT(*) AS total_orders FROM orders WHERE status = ?");
        query.addBindValue(status);
    }

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "查询订单统计失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject data;
    if (query.next()) {
        data["total_orders"] = query.value("total_orders").toInt();
    }

    // 可选：添加更多统计信息
    query.prepare("SELECT status, COUNT(*) as count FROM orders GROUP BY status");
    if (query.exec()) {
        QJsonArray statusStats;
        while (query.next()) {
            QJsonObject stat;
            stat["status"] = query.value("status").toString();
            stat["count"] = query.value("count").toInt();
            statusStats.append(stat);
        }
        data["status_statistics"] = statusStats;
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "订单统计查询成功";
    success["data"] = data;

    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}

// 8. 航班上座率统计 - 修正：考虑订单状态
QHttpServerResponse SystemController::handleFlightOccupancyStatistics(const QHttpServerRequest &request)
{
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 修正：只统计已支付和已完成的订单
    QSqlQuery query(db);
    query.prepare("SELECT flight_id, COUNT(*) AS seats_booked FROM orders "
                  "WHERE status IN ('已支付', '已完成') GROUP BY flight_id");

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "查询航班上座率失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray occupancyStats;
    while (query.next()) {
        QJsonObject flightStats;
        flightStats["flight_id"] = query.value("flight_id").toInt();
        flightStats["seats_booked"] = query.value("seats_booked").toInt();

        // 可选：获取航班总座位数
        QSqlQuery flightQuery(db);
        flightQuery.prepare("SELECT flight_number, "
                            "(economy_seats + business_seats + first_class_seats) as total_seats "
                            "FROM flights WHERE ID = ?");
        flightQuery.addBindValue(flightStats["flight_id"].toInt());
        if (flightQuery.exec() && flightQuery.next()) {
            flightStats["flight_number"] = flightQuery.value("flight_number").toString();
            int totalSeats = flightQuery.value("total_seats").toInt();
            flightStats["total_seats"] = totalSeats;
            if (totalSeats > 0) {
                float occupancyRate = (flightStats["seats_booked"].toInt() * 100.0) / totalSeats;
                flightStats["occupancy_rate"] = QString::number(occupancyRate, 'f', 2);
            }
        }

        occupancyStats.append(flightStats);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "航班上座率统计查询成功";
    success["data"] = occupancyStats;

    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}
