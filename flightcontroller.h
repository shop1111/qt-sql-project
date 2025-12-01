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
    void registerRoutes(QHttpServer *server) override;

private:
    QHttpServerResponse handleSearchFlights(const QHttpServerRequest &request);
    // 辅助函数 (代码转中文名)
    QString getCityNameByCode(const QString &code);
};

#endif // FLIGHTCONTROLLER_H
