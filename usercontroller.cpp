#include "UserController.h"
#include "DatabaseManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

UserController::UserController(QObject *parent) : BaseController(parent)
{
}

void UserController::registerRoutes(QHttpServer *server)
{
    // 对应前端 fetchUserInfo() -> /api/user/info
    server->route("/api/user/info", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleGetUserInfo(req);
                  });

    // 对应前端 updateUserInfo() -> /api/user/update
    server->route("/api/user/update", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleUpdateUserInfo(req);
                  });

    // 对应前端 submitVerify() -> /api/user/verify
    server->route("/api/user/verify", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleVerifyUser(req);
                  });
}

QHttpServerResponse UserController::handleGetUserInfo(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("uid")) {
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "缺少用户的ID"}},
                                   QHttpServerResponse::StatusCode::BadRequest);
    }
    int uid = jsonObj["uid"].toString().toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "数据库连接失败"}},
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
    QSqlQuery query(db);
    query.prepare("SELECT username, nickname, true_name, telephone, email, P_ID, photo, balance FROM users WHERE U_ID = ?");
    query.addBindValue(uid);
    if (query.exec() && query.next()) {
        QJsonObject data;
        QString pId = query.value("P_ID").toString();

        // 映射数据库字段到前端需要的字段名
        data["username"] = query.value("username").toString(); // 账号名
        data["nickname"] = query.value("nickname").toString(); // 昵称
        data["truename"] = query.value("true_name").toString(); // 真实姓名
        data["phone"] = query.value("telephone").toString();   // 电话
        data["email"] = query.value("email").toString();       // 邮箱
        data["avatar"] = query.value("photo").toString();      // 头像
        data["id_card"] = pId; // 身份证号（虽然前端不一定直接展示，但可能需要）
        data["balance"] = query.value("balance").toDouble();
        // 计算性别
        data["gender"] = getGenderFromIdCard(pId);

        QJsonObject response;
        response["status"] = "success";
        response["data"] = data;

        return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);
    } else {
        qInfo()<<"请求失败："<<uid;
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "用户不存在"}},
                                   QHttpServerResponse::StatusCode::NotFound);
    }
}

QHttpServerResponse UserController::handleUpdateUserInfo(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    // 前端传参: { "uid": ..., "field": "nickname"|"telephone"|"email", "value": ... }
    if (!jsonObj.contains("uid") || !jsonObj.contains("field") || !jsonObj.contains("value")) {
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "参数不完整"}},
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    int uid = jsonObj["uid"].toString().toInt();
    QString field = jsonObj["field"].toString();
    QString value = jsonObj["value"].toString();

    // 字段白名单检查，防止SQL注入或修改非法字段
    QString dbField;
    if (field == "nickname") dbField = "nickname";
    else if (field == "telephone") dbField = "telephone";
    else if (field == "email") dbField = "email";
    else {
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "不支持修改该字段"}},
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    QSqlQuery query(db);
    // 注意：字段名不能作为绑定参数，必须直接拼接到 SQL 语句中（因为上面已经做了白名单检查，所以是安全的）
    QString sql = QString("UPDATE users SET %1 = ? WHERE U_ID = ?").arg(dbField);

    query.prepare(sql);
    query.addBindValue(value);
    query.addBindValue(uid);

    if (query.exec()) {
        return QHttpServerResponse(QJsonObject{{"status", "success"}, {"message", "更新成功"}},
                                   QHttpServerResponse::StatusCode::Ok);
    } else {
        qWarning() << "Update User Error:" << query.lastError().text();
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "更新失败，可能是格式错误或已被占用"}},
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

QHttpServerResponse UserController::handleVerifyUser(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    // 前端传参: { "uid": ..., "truename": ..., "id_card": ... }
    if (!jsonObj.contains("uid") || !jsonObj.contains("truename") || !jsonObj.contains("id_card")) {
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "认证信息不全"}},
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    int uid = jsonObj["uid"].toString().toInt();
    QString trueName = jsonObj["truename"].toString();
    QString idCard = jsonObj["id_card"].toString();

    // 简单校验身份证长度
    if (idCard.length() != 18) {
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "身份证号格式不正确"}},
                                   QHttpServerResponse::StatusCode::BadRequest);
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    QSqlQuery query(db);
    // 更新真实姓名和身份证号
    query.prepare("UPDATE users SET true_name = ?, P_ID = ? WHERE U_ID = ?");
    query.addBindValue(trueName);
    query.addBindValue(idCard);
    query.addBindValue(uid);

    if (query.exec()) {
        return QHttpServerResponse(QJsonObject{{"status", "success"}, {"message", "认证成功"}},
                                   QHttpServerResponse::StatusCode::Ok);
    } else {
        qWarning() << "Verify User Error:" << query.lastError().text();
        // 常见错误是身份证号已被其他账号绑定 (Duplicate entry)
        if (query.lastError().text().contains("Duplicate")) {
            return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "该身份证号已被绑定"}},
                                       QHttpServerResponse::StatusCode::Conflict);
        }
        return QHttpServerResponse(QJsonObject{{"status", "failed"}, {"message", "认证失败，数据库修改错误"}},
                                   QHttpServerResponse::StatusCode::InternalServerError);
    }
}

QString UserController::getGenderFromIdCard(const QString &idCard)
{
    if (idCard.length() != 18) return "未知";
    // 中国身份证第17位代表性别：奇数男，偶数女
    QChar genderChar = idCard.at(16);
    int digit = genderChar.digitValue();
    if (digit == -1) return "未知";
    return (digit % 2 == 1) ? "男" : "女";
}
