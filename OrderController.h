#ifndef ORDERCONTROLLER_H
#define ORDERCONTROLLER_H

#include "BaseController.h"
#include <QHttpServerResponse>
#include <QHttpServerRequest>

class OrderController : public BaseController
{
    Q_OBJECT
public:
    explicit OrderController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server) override;

private:
    // 1. 创建订单 (POST)
    QHttpServerResponse handleCreateOrder(const QHttpServerRequest &request);

    // 2. 查询我的订单 (POST)
    QHttpServerResponse handleGetOrders(const QHttpServerRequest &request);

    // 3. 退票/取消订单 (POST)
    QHttpServerResponse handleDeleteOrder(const QHttpServerRequest &request);

    QHttpServerResponse handleRefundOrder(const QHttpServerRequest &request);
};

#endif // ORDERCONTROLLER_H
