# ------------------------------------------------
# 文件: FlightBackendServer.pro
# ------------------------------------------------

# 1. 告诉 Qt 我们需要哪些“工具箱”（模块）
#    core:    Qt 的核心功能 (默认)
#    network: 网络功能 (QHttpServer 需要)
#    sql:     数据库功能 (QSqlDatabase 需要)
#    httpserver: HTTP 服务器功能 (QHttpServer 主体)
QT += core network sql httpserver

# 2. 告诉编译器，我们要使用 C++ 17 标准
#    (QHttpServer 依赖 C++17 的特性)
CONFIG += c++17 console

# 3. 告诉 Qt 这是一个控制台程序，不是带窗口的
CONFIG -= app_bundle

# 4. 定义我们的“蓝图”文件
#    HEADERS: .h 头文件 (定义了“有什么”)
#    SOURCES: .cpp 源文件 (定义了“怎么做”)
#    (我们稍后会创建这些文件)
SOURCES += \
    BrowseHistoryController.cpp \
    OrderController.cpp \
    aicontroller.cpp \
    PaymentController.cpp \
    SeatController.cpp \
    SystemController.cpp \
    flightcontroller.cpp \
    logincontroller.cpp \
    main.cpp \
    usercontroller.cpp

HEADERS += \
    BaseController.h \
    BrowseHistoryController.h \
    DatabaseManager.h \
    OrderController.h \
    aicontroller.h \
    PaymentController.h \
    SeatController.h \
    SystemController.h \
    flightcontroller.h \
    logincontroller.h \
    usercontroller.h

# (后面的部署规则可以先忽略)
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    .gitignore \
    config.ini \
    flight_system.sql
