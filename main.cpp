#include <QCoreApplication>
#include <QHttpServer>
#include <QDebug>
#include "FlightController.h"
#include "DatabaseManager.h"
#include "logincontroller.h"
#include "OrderController.h"
#include"PaymentController.h"
#include"SeatController.h"
#include"SystemController.h"
#include"BrowseHistoryController.h"
#include "aicontroller.h"
#include "usercontroller.h"
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        qCritical() << "无法连接数据库，服务器启动中止！";
        // 记得把 config.ini 放到 build 目录下的 debug 文件夹里！
        return -1;
    }

    // 创建 HTTP 服务器实例
    QHttpServer httpServer;

    // 创建并注册 FlightController
    // 这里展示了核心思想：Controller 只是一个负责干活的插件
    FlightController *flightCtrl = new FlightController(&a);
    flightCtrl->registerRoutes(&httpServer);

    httpServer.route("/<arg>", [](const QHttpServerRequest &) {
        return QHttpServerResponse("Not Found", QHttpServerResponse::StatusCode::NotFound);
    });

    // 修改：注册 LoginController
    // 只有执行了这一步，服务器才认识 "/api/login" 这个地址
    LoginController *loginCtrl = new LoginController(&a);
    loginCtrl->registerRoutes(&httpServer);


    OrderController *orderCtrl = new OrderController(&a);
    orderCtrl->registerRoutes(&httpServer);

    // 注册 PaymentController
    PaymentController *paymentCtrl = new PaymentController(&a);
    paymentCtrl->registerRoutes(&httpServer);

    SeatController *seatCtrl = new SeatController(&a);
    seatCtrl->registerRoutes(&httpServer);

    // Register SystemController for system management routes
    SystemController *systemCtrl = new SystemController(&a); // Instantiate SystemController
    systemCtrl->registerRoutes(&httpServer); // Register its routes

    // 注册 BrowseHistoryController
    BrowseHistoryController *browseHistoryCtrl = new BrowseHistoryController(&a);
    browseHistoryCtrl->registerRoutes(&httpServer);

    AIController *aiCtrl = new AIController(&a);
    aiCtrl->registerRoutes(&httpServer);

    UserController* userCtrl = new UserController(&a);
    userCtrl->registerRoutes(&httpServer);

    // 启动监听, 开始监听本机的全部ip地址和给定的端口
    const quint16 port = 8080;
    if (!httpServer.listen(QHostAddress::Any, port)) {
        qWarning() << "端口" << port << "被占用，启动失败。";
        return -1;
    }

    qInfo() << "==========================================";
    qInfo() << "   服务器已启动 | 监听端口:" << port;
    qInfo() << "   已加载模块: FlightController";
    qInfo() << "   已加载模块: LoginController";
    qInfo() << "   已加载模块: AIController";
    qInfo() << "   已加载模块: UserController";
<<<<<<< HEAD
    qInfo() << "   已加载模块: PaymentController";
    qInfo() << "   已加载模块: SeatController";
    qInfo() << "   已加载模块: SystemController";
    qInfo() << "   已加载模块: BrowseHistoryController";
=======
    qInfo() << "   已加载模块: OrderController";
>>>>>>> e2e5fe7e3c55ad4a343bb087b5fb01aec3efdb20
    qInfo() << "==========================================";
    // const QHostAddress &localhost = QHostAddress(QHostAddress::LocalHost);
    // for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
    //     // 过滤掉 IPv6 和 本地回环地址(127.0.0.1)
    //     if (address.protocol() == QAbstractSocket::IPv4Protocol && address != localhost) {
    //         qDebug() << "我的局域网 IP 是:" << address.toString();
    //     }
    // }

    return a.exec();
}
