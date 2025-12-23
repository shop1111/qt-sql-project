#include "OrderController.h"
#include "DatabaseManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>
#include <cmath>
#include <QSet>

// ==============================================================================
//  内部辅助类：座位分配器 (SeatAllocator)
//  负责在内存中计算虚拟座位表，并执行随机/指定筛选
// ==============================================================================
class SeatAllocator {
public:
    // 根据舱位配置，生成指定舱位的所有座位号列表 (例如: "1A", "1B", "2A"...)
    static QStringList generateAllSeats(int firstCount, int businessCount, int economyCount, int targetType) {
        QStringList allSeats;

        // 1. 计算各舱位需要的行数 (向上取整)
        int firstRows = std::ceil((double)firstCount / 2.0);      // 头等舱每排2座 (AB)
        int businessRows = std::ceil((double)businessCount / 4.0); // 商务舱每排4座 (ABCD)
        int economyRows = std::ceil((double)economyCount / 6.0);   // 经济舱每排6座 (ABCDEF)

        int startRow = 1;
        int endRow = 0;
        QString layout = "";

        // 2. 根据目标舱位确定 行号范围 和 列布局
        if (targetType == 2) { // 头等舱
            startRow = 1;
            endRow = firstRows;
            layout = "AB";
        } else if (targetType == 1) { // 商务舱
            startRow = 1 + firstRows; // 紧接头等舱之后
            endRow = startRow + businessRows - 1;
            layout = "ABCD";
        } else { // 经济舱 (默认 Type 0)
            startRow = 1 + firstRows + businessRows; // 紧接商务舱之后
            endRow = startRow + economyRows - 1;
            layout = "ABCDEF";
        }

        // 3. 生成虚拟座位表
        for (int r = startRow; r <= endRow; ++r) {
            for (const QChar &col : layout) {
                allSeats.append(QString::number(r) + col);
            }
        }

        // 4. 裁剪多余座位 (因为行数是向上取整生成的，可能会多出几个空座)
        int maxCount = (targetType == 2 ? firstCount : (targetType == 1 ? businessCount : economyCount));
        // 如果生成的比总数多，裁掉末尾的
        while(allSeats.size() > maxCount) {
            allSeats.removeLast();
        }

        return allSeats;
    }

    // 核心分配逻辑：从全量座位中剔除已占用的，然后根据偏好随机抽取
    static QString assignSeat(const QStringList& fullSeatMap, const QSet<QString>& occupiedSeats, const QString& preferLetter) {
        // 1. 筛选可用座位 (Available = Full - Occupied)
        QStringList availableSeats;
        for (const QString& seat : fullSeatMap) {
            if (!occupiedSeats.contains(seat)) {
                availableSeats.append(seat);
            }
        }

        // 如果该舱位已满
        if (availableSeats.isEmpty()) return "";

        // 2. 尝试筛选符合用户偏好字母的 (比如用户想要 "A")
        QStringList candidates;
        if (!preferLetter.isEmpty()) {
            for (const QString& seat : availableSeats) {
                if (seat.endsWith(preferLetter, Qt::CaseInsensitive)) {
                    candidates.append(seat);
                }
            }
        }

        // 3. 执行随机抽取
        // 如果有符合偏好的，就在candidates里随机；否则在所有available里随机 (降级策略)
        const QStringList& finalPool = candidates.isEmpty() ? availableSeats : candidates;

        int idx = QRandomGenerator::global()->bounded(finalPool.size());
        return finalPool[idx];
    }
};

// ==============================================================================
//  OrderController 实现
// ==============================================================================

OrderController::OrderController(QObject *parent) : BaseController(parent)
{
}

void OrderController::registerRoutes(QHttpServer *server)
{
    // 1. 下单 (自动分配座位)
    server->route("/api/create_order", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleCreateOrder(req);
                  });

    // 2. 查单
    server->route("/api/get_orders", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleGetOrders(req);
                  });

    // 3. 删除单
    server->route("/api/delete_order", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleDeleteOrder(req);
                  });

    server->route("/api/refund_order", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleRefundOrder(req);
                  });
}

// ----------------------------------------------------------------------------
// 1. 创建订单 (自动分配)
// 请求示例: { "user_id": 1, "flight_id": 10, "seat_type": 0, "prefer_letter": "A" }
// ----------------------------------------------------------------------------
QHttpServerResponse OrderController::handleCreateOrder(const QHttpServerRequest &request)
{
    // 1. 解析请求 JSON
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("user_id") || !jsonObj.contains("flight_id")) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "参数缺失";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int userId = jsonObj["user_id"].toInt();
    int flightId = jsonObj["flight_id"].toInt();
    int seatType = jsonObj["seat_type"].toInt(0); // 0:经济, 1:商务, 2:头等
    QString preferLetter = jsonObj["prefer_letter"].toString().toUpper(); // 用户想要的字母

    // 数据库连接
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()){
        QJsonObject err; err["status"] = "failed"; err["message"] = "数据库连接失败";
        return QHttpServerResponse(err,QHttpServerResponse::StatusCode::InternalServerError);
    }

    // --- 开启事务 (保证查占座和插入的原子性) ---
    db.transaction();

    QSqlQuery query(db);

    // 2. 获取航班的总座位配置 (用于生成虚拟座位表)
    query.prepare("SELECT economy_seats, business_seats, first_class_seats, "
                  "economy_price, business_price, first_class_price " // <--- 新增查询价格
                  "FROM flights WHERE ID = ?");
    query.addBindValue(flightId);
    if (!query.exec() || !query.next()) {
        db.rollback();
        QJsonObject err; err["status"] = "failed"; err["message"] = "航班不存在";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    // 获取座位数
    int ecoCount = query.value("economy_seats").toInt();
    int busCount = query.value("business_seats").toInt();
    int firCount = query.value("first_class_seats").toInt();

    int ecoPrice = query.value("economy_price").toInt();
    int busPrice = query.value("business_price").toInt();
    int firPrice = query.value("first_class_price").toInt();

    double orderAmount = 0.0;
    if (seatType == 0) orderAmount = ecoPrice;
    else if (seatType == 1) orderAmount = busPrice;
    else if (seatType == 2) orderAmount = firPrice;
    else {
        // 防止非法 seatType
        orderAmount = ecoPrice;
    }
    // 3. 获取当前已占用的座位 (排除已取消的)
    // 使用 FOR UPDATE 锁住相关行，防止并发下同一座位被重复分配，事务锁
    QSqlQuery occupiedQuery(db);
    occupiedQuery.prepare("SELECT seat_number FROM orders WHERE flight_id = ? AND status != '已取消' FOR UPDATE");
    occupiedQuery.addBindValue(flightId);

    if (!occupiedQuery.exec()) {
        db.rollback();
        QJsonObject err; err["status"] = "failed"; err["message"] = "系统繁忙 (Lock Error)";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSet<QString> occupiedSeats;
    while(occupiedQuery.next()) {
        occupiedSeats.insert(occupiedQuery.value("seat_number").toString());
    }

    // 4. 执行分配算法
    // A. 在内存中生成该舱位的完整座位表
    QStringList fullSeatMap = SeatAllocator::generateAllSeats(firCount, busCount, ecoCount, seatType);

    // B. 根据占用情况和用户偏好，计算出分配的座位
    QString assignedSeat = SeatAllocator::assignSeat(fullSeatMap, occupiedSeats, preferLetter);

    if (assignedSeat.isEmpty()) {
        db.rollback();
        QJsonObject err; err["status"] = "failed"; err["message"] = "该舱位已售罄，无法分配座位";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
    }

    // 5. 写入订单 (Status: 未支付)
    QSqlQuery insertQuery(db);
    insertQuery.prepare("INSERT INTO orders (user_id, flight_id, seat_type, seat_number, status, order_date, total_amount) "
                        "VALUES (?, ?, ?, ?, '未支付', CURRENT_TIMESTAMP, ?)"); // <--- 增加了一个占位符
    insertQuery.addBindValue(userId);
    insertQuery.addBindValue(flightId);
    insertQuery.addBindValue(seatType);
    insertQuery.addBindValue(assignedSeat);
    insertQuery.addBindValue(orderAmount); // <--- 绑定计算好的价格

    if (!insertQuery.exec()) {
        db.rollback();
        qWarning() << "Create Order Error:" << insertQuery.lastError().text();
        QJsonObject err; err["status"] = "failed"; err["message"] = "下单失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 获取新生成的订单ID
    int newOrderId = insertQuery.lastInsertId().toInt();
    db.commit(); // 提交事务

    // 6. 返回成功响应 (带回分配的座位号)
    QJsonObject success;
    success["status"] = "success";
    success["message"] = "预订成功";
    success["order_id"] = newOrderId;
    success["seat_number"] = assignedSeat; // 告诉前端分配到了哪里
    success["seat_type"] = seatType;

    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}


// ----------------------------------------------------------------------------
// 2. 查询用户订单
// ----------------------------------------------------------------------------
QHttpServerResponse OrderController::handleGetOrders(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("user_id")) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "参数缺失";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }
    int userId = jsonObj["user_id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    // 【修改点 1】SQL语句增加价格字段查询
    QString sql = R"(
        SELECT
            o.ID as order_id, o.seat_type, o.seat_number, o.order_date, o.status,
            f.flight_number, f.airline, f.origin, f.destination,
            f.departure_time, f.landing_time, f.aircraft_model,
            f.economy_price, f.business_price, f.first_class_price
        FROM orders o
        JOIN flights f ON o.flight_id = f.ID
        WHERE o.user_id = ?
        ORDER BY o.order_date DESC
    )";

    query.prepare(sql);
    query.addBindValue(userId);

    if (!query.exec()) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "数据库查询失败";
        return QHttpServerResponse(err,QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray list;
    while (query.next()) {
        QJsonObject item;
        item["order_id"] = query.value("order_id").toInt();
        if(query.value("status").toString() == "未支付")
            item["status"] = 0;
        else if(query.value("status").toString()=="已支付")
            item["status"] = 1;
        else
            item["status"] = 2;
        item["flight_number"] = query.value("flight_number").toString();
        item["airline"] = query.value("airline").toString();
        // 前端对应 dep_city, arr_city，这里后端字段名为 origin, destination
        // 建议后端保持数据库字段名，前端去适配；或者在这里做别名转换
        item["dep_city"] = query.value("origin").toString();
        item["arr_city"] = query.value("destination").toString();
        item["aircraft_model"] = query.value("aircraft_model").toString();

        // 时间格式化
        QDateTime dep = query.value("departure_time").toDateTime();
        QDateTime arr = query.value("landing_time").toDateTime();
        item["dep_time"] = dep.toString("yyyy-MM-dd HH:mm");
        item["arr_time"] = arr.toString("HH:mm");

        item["seat_number"] = query.value("seat_number").toString();

        // 【修改点 2】根据舱位类型计算具体价格
        int type = query.value("seat_type").toInt();
        int price = 0;
        QString seatClassStr = "经济舱";

        if (type == 0) {
            seatClassStr = "经济舱";
            price = query.value("economy_price").toInt();
        } else if (type == 1) {
            seatClassStr = "商务舱";
            price = query.value("business_price").toInt();
        } else if (type == 2) {
            seatClassStr = "头等舱";
            price = query.value("first_class_price").toInt();
        }

        item["seat_class"] = seatClassStr;
        item["price"] = price; // ✅ 补全前端需要的价格字段

        list.append(item);
    }

    QJsonObject resp;
    resp["status"] = "success";
    resp["data"] = list;
    return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse OrderController::handleDeleteOrder(const QHttpServerRequest &request)
{
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    int userId = 0;
    userId = jsonObj["user_id"].toInt();

    if (!jsonObj.contains("order_id") || userId == 0) {
        qInfo()<<"error1: "<<userId;
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    }
    // QString orderId = jsonObj["order_id"].toString();
    QString orderId = jsonObj["order_id"].toVariant().toString();
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);

    // 【核心修改】执行物理删除
    // 加上 user_id 是为了安全，防止用户删除别人的订单
    query.prepare("DELETE FROM orders WHERE ID = ? AND user_id = ?");
    query.addBindValue(orderId);
    query.addBindValue(userId);

    if (!query.exec()) {
        QJsonObject err;
        qInfo()<<"error2: "<<orderId;
        err["status"] = "failed";
        // 如果有外键约束(如payments表关联)，直接删除可能会报错，需要数据库配置级联删除(ON DELETE CASCADE)
        err["message"] = "删除失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (query.numRowsAffected() > 0) {
        QJsonObject success;
        success["status"] = "success";
        success["message"] = "订单已删除";
        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
    } else {
        QJsonObject fail;
        qInfo()<<"error3: "<<orderId;
        fail["status"] = "failed";
        fail["message"] = "订单不存在或无权操作";
        return QHttpServerResponse(fail, QHttpServerResponse::StatusCode::NotFound);
    }
}

// 4. 订单退款 (事务处理：改状态 + 退余额)
// 请求示例: { "user_id": 1, "order_id": 123 }
// ----------------------------------------------------------------------------
QHttpServerResponse OrderController::handleRefundOrder(const QHttpServerRequest &request)
{
    // 1. 解析请求参数
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();

    if (!jsonObj.contains("user_id") || !jsonObj.contains("order_id")) {
        QJsonObject err; err["status"] = "failed"; err["message"] = "参数缺失";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int userId = jsonObj["user_id"].toInt();
    int orderId = jsonObj["order_id"].toString().toInt();

    // 2. 连接数据库
    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 3. 开启事务 (非常重要：涉及资金变动)
    db.transaction();
    QSqlQuery query(db);

    // 4. 查询订单状态及支付金额 (使用 FOR UPDATE 锁行，防止并发重复退款)
    query.prepare("SELECT status, paid_amount, user_id FROM orders WHERE ID = ? FOR UPDATE");
    query.addBindValue(orderId);

    if (!query.exec() || !query.next()) {
        db.rollback();
        QJsonObject err; err["status"] = "failed"; err["message"] = "订单不存在";
        qInfo()<<"找不到订单号："<<orderId;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    // 5. 校验逻辑
    int dbUserId = query.value("user_id").toInt();
    QString status = query.value("status").toString();
    double paidAmount = query.value("paid_amount").toDouble();

    // 校验归属权
    if (dbUserId != userId) {
        db.rollback();
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Forbidden);
    }

    // 校验状态 (只有“已支付”的订单才能退款)
    if (status != "已支付") {
        db.rollback();
        QJsonObject err;
        err["status"] = "failed";

        if (status == "已退款") err["message"] = "该订单已退款，请勿重复操作";
        else if (status == "未支付") err["message"] = "订单未支付，无法退款";
        else err["message"] = "当前订单状态无法退款: " + status;

        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
    }

    // 6. 执行退款操作

    // A. 增加用户余额
    QSqlQuery updateUser(db);
    updateUser.prepare("UPDATE users SET balance = balance + ? WHERE U_ID = ?");
    updateUser.addBindValue(paidAmount);
    updateUser.addBindValue(userId);

    if (!updateUser.exec()) {
        db.rollback();
        QJsonObject err; err["status"] = "failed"; err["message"] = "退款到余额失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // B. 更新订单状态为 "已退款"
    QSqlQuery updateOrder(db);
    updateOrder.prepare("UPDATE orders SET status = '已退款' WHERE ID = ?");
    updateOrder.addBindValue(orderId);

    if (!updateOrder.exec()) {
        db.rollback();
        QJsonObject err; err["status"] = "failed"; err["message"] = "更新订单状态失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 7. 提交事务
    db.commit();

    QJsonObject success;
    success["status"] = "success";
    success["message"] = QString("退款成功，金额 %.2f 已退回账户余额").arg(paidAmount);
    return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);
}
