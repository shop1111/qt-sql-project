#include "SystemController.h"
#include "DatabaseManager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

SystemController::SystemController(QObject *parent) : BaseController(parent)
{//BaseController已经正确初始化，∴此函数可以为空
}

void SystemController::registerRoutes(QHttpServer *server)
{
    // 航班管理
    //管理员添加航班
    server->route("/system/api/flight/add", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleAddFlight(req); });
    //管理员修改航班
    server->route("/system/api/flight/update", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleUpdateFlight(req); });
    //管理员删除航班
    server->route("/system/api/flight/delete", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleDeleteFlight(req); });

    // 用户管理
    //增删改用户
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

// 1. 添加航班
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
    if (!jsonObj.contains("flight_number") || !jsonObj.contains("origin") || !jsonObj.contains("destination")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "参数缺失（需要flight_number, origin, destination）";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    //提取参数值
    // 从JSON对象中提取各个字段的值，并转换为QString类型
    QString flightNumber = jsonObj["flight_number"].toString();
    QString origin = jsonObj["origin"].toString();
    QString destination = jsonObj["destination"].toString();
    // 可以添加其他参数

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 绑定参数值
    // 将前端传入的参数值绑定到SQL语句的占位符(?)上
    QSqlQuery query(db);
    query.prepare("INSERT INTO flights (flight_number, origin, destination) VALUES (?, ?, ?)");
    query.addBindValue(flightNumber);
    query.addBindValue(origin);
    query.addBindValue(destination);

     // 执行SQL插入操作
    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "添加航班失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "航班添加成功";

    // 创建data对象，将返回数据放入其中
    QJsonObject data;
    data["flight_id"] = query.lastInsertId().toInt();

    success["data"] = data;
    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}

// 2. 更新航班
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

    if (!jsonObj.contains("id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "缺少航班ID参数";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int flightId = jsonObj["id"].toInt();
    QString flightNumber = jsonObj["flight_number"].toString();
    QString origin = jsonObj["origin"].toString();
    QString destination = jsonObj["destination"].toString();
    QString departureTime = jsonObj["departure_time"].toString();
    QString landingTime = jsonObj["landing_time"].toString();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("UPDATE flights SET flight_number = ?, origin = ?, destination = ?, departure_time = ?, landing_time = ? WHERE ID = ?");
    query.addBindValue(flightNumber);
    query.addBindValue(origin);
    query.addBindValue(destination);
    query.addBindValue(departureTime);
    query.addBindValue(landingTime);
    query.addBindValue(flightId);

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
    if (!jsonObj.contains("username") || !jsonObj.contains("password")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "参数缺失（需要username, password）";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

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

    QSqlQuery query(db);
    query.prepare("INSERT INTO users (username, password, true_name, telephone, P_ID) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(username);
    query.addBindValue(password);
    query.addBindValue(trueName);
    query.addBindValue(telephone);
    query.addBindValue(pid);

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        if (query.lastError().text().contains("Duplicate")) {
            err["message"] = "用户名已存在";
        } else {
            err["message"] = "添加用户失败: " + query.lastError().text();
        }
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "用户添加成功";

    // 创建data对象
    QJsonObject data;
    data["user_id"] = query.lastInsertId().toInt();

    success["data"] = data;
    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}

// 5. 更新用户
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

    QSqlQuery query(db);
    query.prepare("UPDATE users SET username = ?, password = ?, true_name = ?, telephone = ?, P_ID = ? WHERE ID = ?");
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


// 6. 删除用户
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

    QSqlQuery query(db);
    query.prepare("DELETE FROM users WHERE ID = ?");
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

// 7. 订单统计
QHttpServerResponse SystemController::handleOrderStatistics(const QHttpServerRequest &request)
{
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) AS total_orders FROM orders");

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

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "订单统计查询成功";
    success["data"] = data;
    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}

// 8. 航班上座率统计
QHttpServerResponse SystemController::handleFlightOccupancyStatistics(const QHttpServerRequest &request)
{
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("SELECT flight_id, COUNT(*) AS seats_booked FROM orders GROUP BY flight_id");

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
        occupancyStats.append(flightStats);
    }

    QJsonObject success;
    success["status"] = "success";
    success["message"] = "航班上座率统计查询成功";
    success["data"] = occupancyStats;
    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}
