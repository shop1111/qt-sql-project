#include "logincontroller.h"

#include "DatabaseManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

LoginController::LoginController(QObject *parent) : BaseController(parent)
{
}

void LoginController::registerRoutes(QHttpServer *server)
{
    // 路由：POST /api/login
    server->route("/api/login", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleLogin(req);
                  });
    server->route("/api/register", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleRegister(req);
                  });
}

QHttpServerResponse LoginController::handleLogin(const QHttpServerRequest &request)
{
    // 1. 解析 JSON 请求体
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "无法解析出JSON对象";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("username") || !jsonObj.contains("password")) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        if(jsonObj.contains("username")==0)
            responseObj["message"] = "无法解析出用户名";
        else
            responseObj["message"] = "无法解析出密码";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QString username = jsonObj["username"].toString();
    QString password = jsonObj["password"].toString();

    QSqlDatabase database = DatabaseManager::getConnection();
    if (!database.isOpen()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "数据库无法打开";
        return QHttpServerResponse(responseObj,QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(database);
    query.prepare("SELECT U_ID, username, telephone, email, photo FROM users WHERE username = ? AND password = ?");
    query.addBindValue(username);
    query.addBindValue(password);

    if (!query.exec()) {
        qWarning() << "Login SQL Error:" << query.lastError().text();
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "SQL查询失败";
        return QHttpServerResponse(responseObj,QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 检查是否有匹配的用户
    if (query.next()) {
        // --- 登录成功 ---
        QJsonObject userObj;
        userObj["id"] = query.value("U_ID").toInt();
        userObj["name"] = query.value("username").toString();
        userObj["telephone"] = query.value("telephone").toString();
        userObj["email"] = query.value("email").toString();
        // 如果有头像也可以返回
        // userObj["photo"] = query.value("photo").toString();

        QJsonObject responseObj;
        responseObj["status"] = "success";
        responseObj["message"] = "登陆成功";
        responseObj["user"] = userObj;

        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
    } else {
        // --- 登录失败 (账号或密码错误) ---
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "登陆失败";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Unauthorized);
    }
}


QHttpServerResponse LoginController::handleRegister(const QHttpServerRequest &request)
{
    // 1. 解析 JSON 请求体
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "无法解析出JSON对象";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    // 2. 校验必要字段
    if (!jsonObj.contains("username") || !jsonObj.contains("password") ||
        !jsonObj.contains("email") || !jsonObj.contains("telephone") || !jsonObj.contains("ID")) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "注册数据不全";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::BadRequest);
    }

    // 3. 提取数据
    QString username = jsonObj["username"].toString();
    QString password = jsonObj["password"].toString();
    QString email = jsonObj["email"].toString();
    QString telephone = jsonObj["telephone"].toString();
    QString pid = jsonObj["ID"].toString(); // JSON中的 ID 对应数据库的 P_ID

    // 4. 获取数据库连接
    QSqlDatabase database = DatabaseManager::getConnection();
    if (!database.isOpen()) {
        QJsonObject responseObj;
        responseObj["status"] = "failed";
        responseObj["message"] = "数据库无法打开";
        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 5. 执行插入操作
    QSqlQuery query(database);
    query.prepare("INSERT INTO users (username, password, telephone, email, P_ID) "
                  "VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(username);
    query.addBindValue(password);
    query.addBindValue(telephone);
    query.addBindValue(email);
    query.addBindValue(pid);

    if (!query.exec()) {
        // 记录具体的 SQL 错误
        qWarning() << "Register SQL Error:" << query.lastError().text();

        QJsonObject responseObj;
        responseObj["status"] = "failed";

        // 简单判断一下是否是重复键错误 (Duplicate entry)
        if (query.lastError().text().contains("Duplicate")) {
            responseObj["message"] = "注册失败：用户名，电话号码或身份证号已被注册";
        } else {
            responseObj["message"] = "注册失败：数据库写入错误";
        }

        return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 6. 注册成功
    QJsonObject responseObj;
    responseObj["status"] = "success";
    responseObj["message"] = "注册成功";

    // 可以选择返回新用户的 ID，但通常只需要告诉前端成功即可
    responseObj["new_user_id"] = query.lastInsertId().toInt();

    return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
}
