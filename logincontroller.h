#ifndef LOGINCONTROLLER_H
#define LOGINCONTROLLER_H

#include"BaseController.h"
class LoginController: public BaseController
{
    Q_OBJECT
public:
    explicit LoginController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server);

private:
    QHttpServerResponse handleLogin(const QHttpServerRequest &request);
    QHttpServerResponse handleRegister(const QHttpServerRequest &request);
};

#endif // LOGINCONTROLLER_H
