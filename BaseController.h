#ifndef BASECONTROLLER_H
#define BASECONTROLLER_H

#include <QHttpServer>
#include <QObject>

class BaseController : public QObject {
    Q_OBJECT
public:
    explicit BaseController(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~BaseController() = default;

    // 纯虚函数：子类必须实现这个函数来告诉 Server 只有哪些路由
    virtual void registerRoutes(QHttpServer *server) = 0;
};

#endif // BASECONTROLLER_H
