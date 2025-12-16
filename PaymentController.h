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
    // --- 核心业务接口 ---
    // 充值接口：处理用户充值请求
    QHttpServerResponse handleRecharge(const QHttpServerRequest &request);

    // 支付接口：处理订单支付及余额扣除
    QHttpServerResponse handlePayment(const QHttpServerRequest &request);

    // --- 辅助函数 ---
    int extractIntValue(const QJsonObject &obj, const QString &key);
    double extractDoubleValue(const QJsonObject &obj, const QString &key);
    bool userExists(QSqlDatabase &db, int userId);

    // 统一响应封装
    QJsonObject createSuccessResponse(const QString &message);
    QJsonObject createErrorResponse(const QString &message,
                                    QHttpServerResponse::StatusCode status = QHttpServerResponse::StatusCode::BadRequest);

};

#endif // PAYMENTCONTROLLER_H
