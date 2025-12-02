#ifndef SYSTEMCONTROLLER_H
#define SYSTEMCONTROLLER_H

#include "BaseController.h"
#include <QHttpServerResponse>
#include <QHttpServerRequest>

class SystemController : public BaseController
{
    Q_OBJECT
public:
    explicit SystemController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server) override;

private:
    //航班管理 - 管理员添加/修改/删除航班
    QHttpServerResponse handleAddFlight(const QHttpServerRequest &request);
    QHttpServerResponse handleUpdateFlight(const QHttpServerRequest &request);
    QHttpServerResponse handleDeleteFlight(const QHttpServerRequest &request);

    //用户管理 - 管理员管理用户账号
    QHttpServerResponse handleAddUser(const QHttpServerRequest &request);
    QHttpServerResponse handleUpdateUser(const QHttpServerRequest &request);
    QHttpServerResponse handleDeleteUser(const QHttpServerRequest &request);

    //数据统计 - 订单统计、航班上座率统计
    QHttpServerResponse handleOrderStatistics(const QHttpServerRequest &request);
    QHttpServerResponse handleFlightOccupancyStatistics(const QHttpServerRequest &request);
};

#endif // SYSTEMCONTROLLER_H
