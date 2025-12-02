#ifndef PAYMENTCONTROLLER_H
#define PAYMENTCONTROLLER_H

#include "BaseController.h"
#include <QHttpServerRequest>
#include <QHttpServerResponse>

class PaymentController : public BaseController
{
    Q_OBJECT
public:
    explicit PaymentController(QObject *parent = nullptr);

    void registerRoutes(QHttpServer *server) override;

private:
    QHttpServerResponse handlePostPayment(const QHttpServerRequest &request);
    QHttpServerResponse handleGetPaymentStatus(int paymentId);
    QHttpServerResponse handleRefundPayment(const QHttpServerRequest &request);
};

#endif // PAYMENTCONTROLLER_H

