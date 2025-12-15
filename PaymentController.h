// PaymentController.h - 更新版本
#ifndef PAYMENTCONTROLLER_H
#define PAYMENTCONTROLLER_H

#include "BaseController.h"
#include <QHttpServerResponse>
#include <QHttpServerRequest>
#include <QJsonObject>
#include <QSqlDatabase>

class PaymentController : public BaseController
{
    Q_OBJECT
public:
    explicit PaymentController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server) override;

private:
    // 主要处理函数
    QHttpServerResponse handleRecharge(const QHttpServerRequest &request);
    QHttpServerResponse handlePayment(const QHttpServerRequest &request);
    QHttpServerResponse handleUserInfo(const QHttpServerRequest &request);
    QHttpServerResponse handleUpdateUser(const QHttpServerRequest &request);
    QHttpServerResponse handleVerifyUser(const QHttpServerRequest &request);
    QHttpServerResponse handleGetOrders(const QHttpServerRequest &request);
    QHttpServerResponse handleDeleteOrder(const QHttpServerRequest &request);

    // 辅助函数
    int extractIntValue(const QJsonObject &obj, const QString &key);
    double extractDoubleValue(const QJsonObject &obj, const QString &key);
    bool userExists(QSqlDatabase &db, int userId);
    double getUserBalance(QSqlDatabase &db, int userId);
    QJsonObject createSuccessResponse(const QString &message);
    QJsonObject createErrorResponse(const QString &message,
                                    QHttpServerResponse::StatusCode status = QHttpServerResponse::StatusCode::BadRequest);

    // 生成唯一的订单号
    QString generateOrderNo(const QString &prefix = "RECH");

    // 模拟支付处理
    bool processMockPayment(int userId, const QString &orderId, double amount,
                            const QString &method);
};

#endif // PAYMENTCONTROLLER_H
