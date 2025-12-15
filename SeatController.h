#ifndef SEATCONTROLLER_H
#define SEATCONTROLLER_H

#include "BaseController.h"
#include <QHttpServerResponse>
#include <QHttpServerRequest>

class SeatController : public BaseController
{
    Q_OBJECT
public:
    explicit SeatController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server) override;

private:
    QHttpServerResponse handleSeatSelection(const QHttpServerRequest &request);
    QHttpServerResponse handleSeatLock(const QHttpServerRequest &request);
    QHttpServerResponse handleSeatStatus(const QHttpServerRequest &request);
    QHttpServerResponse handleSeatUnlock(const QHttpServerRequest &request);
};

#endif // SEATCONTROLLER_H
