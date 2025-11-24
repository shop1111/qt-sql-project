#ifndef FLIGHTCONTROLLER_H
#define FLIGHTCONTROLLER_H

#include "BaseController.h"
#include <QHttpServerResponse>
#include <QHttpServerRequest>

class FlightController : public BaseController
{
    Q_OBJECT
public:
    explicit FlightController(QObject *parent = nullptr);

    // 必须实现的接口：告诉服务器我负责哪些路由
    void registerRoutes(QHttpServer *server) override;

private:
    // 具体的业务处理函数（从原来的 FlightsApi 搬过来的）
    QHttpServerResponse handleGetFlights(const QHttpServerRequest &request);
    QHttpServerResponse handlePostFlights(const QHttpServerRequest &request);
};

#endif // FLIGHTCONTROLLER_H
