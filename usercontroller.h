#ifndef USERCONTROLLER_H
#define USERCONTROLLER_H

#include "BaseController.h"
#include <QHttpServer>
#include <QObject>

class UserController : public BaseController
{
    Q_OBJECT
public:
    explicit UserController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server);

private:
    // 获取用户信息
    QHttpServerResponse handleGetUserInfo(const QHttpServerRequest &request);
    // 更新用户信息 (昵称、电话、邮箱)
    QHttpServerResponse handleUpdateUserInfo(const QHttpServerRequest &request);
    // 实名认证
    QHttpServerResponse handleVerifyUser(const QHttpServerRequest &request);

    // 辅助函数：根据身份证号计算性别
    QString getGenderFromIdCard(const QString &idCard);
};

#endif // USERCONTROLLER_H
