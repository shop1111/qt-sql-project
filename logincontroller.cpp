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
}

QHttpServerResponse LoginController::handleLogin(const QHttpServerRequest &request)
{
    // 1. 解析 JSON 请求体
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        return QHttpServerResponse("Invalid JSON", QHttpServerResponse::StatusCode::BadRequest);
    }
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("username") || !jsonObj.contains("password")) {
        return QHttpServerResponse("Missing username or password", QHttpServerResponse::StatusCode::BadRequest);
    }

    QString username = jsonObj["username"].toString();
    QString password = jsonObj["password"].toString();

    QSqlDatabase database = DatabaseManager::getConnection();
    if (!database.isOpen()) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(database);
    query.prepare("SELECT U_ID, username, telephone, email, photo FROM users WHERE username = ? AND password = ?");
    query.addBindValue(username);
    query.addBindValue(password);

    if (!query.exec()) {
        qWarning() << "Login SQL Error:" << query.lastError().text();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 5. 检查是否有匹配的用户
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

        // 此处内容不直接发送字符串，改为发送json
        QJsonObject errorObj;
        errorObj["status"] = "error";
        errorObj["message"] = "用户名或密码错误"; // 这是一个友好的提示

        // return QHttpServerResponse("Invalid username or password", QHttpServerResponse::StatusCode::Unauthorized);
        return QHttpServerResponse(errorObj, QHttpServerResponse::StatusCode::Unauthorized);
    }
}
