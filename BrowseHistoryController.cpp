#include "BrowseHistoryController.h"
#include "DatabaseManager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDateTime>
#include <QTimer>

BrowseHistoryController::BrowseHistoryController(QObject *parent) : BaseController(parent)
{
}

//注册3个api端点
void BrowseHistoryController::registerRoutes(QHttpServer *server)
{
    // 记录浏览历史
    server->route("/api/browse/add", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleAddBrowseHistory(req);
                  });

    // 获取浏览历史（最近10条）
    server->route("/api/browse/history", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleGetBrowseHistory(req);
                  });

    // 清空浏览历史
    server->route("/api/browse/clear", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleClearBrowseHistory(req);
                  });
}

/*数据流：
1. 解析JSON请求体
2. 验证必需字段(user_id, flight_id)
3. 获取数据库连接
4. 开始事务
5. 检查用户现有记录数量
6. 如果超过9条，删除最旧的一条
7. 插入新记录
8. 提交事务
9. 返回操作结果

逻辑说明：
- 每个用户最多保留10条浏览记录
- 使用FIFO（先进先出）策略管理记录数量
- 支持保存航班快照数据作为历史状态
- 所有数据库操作在事务中完成，保证数据一致性
          */

QHttpServerResponse BrowseHistoryController::handleAddBrowseHistory(const QHttpServerRequest &request)
{
    // 解析客户端传来的JSON数据
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    //验证必需字段
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "无效的JSON格式";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    // 验证必需字段，必须含有user_id flight_id才能继续
    if (!jsonObj.contains("user_id") || !jsonObj.contains("flight_id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "缺少必要字段: user_id 和 flight_id";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int userId = jsonObj["user_id"].toInt();
    int flightId = jsonObj["flight_id"].toInt();

    // 可选：保存航班快照数据（用于记录航班在浏览时的状态）
    QJsonObject flightSnapshot;
    if (jsonObj.contains("flight_snapshot")) {
        flightSnapshot = jsonObj["flight_snapshot"].toObject();
    }

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 开始事务
    db.transaction();

    try {
        // 1. 先检查该用户是否已经有超过10条记录
        QSqlQuery checkQuery(db);
        checkQuery.prepare("SELECT COUNT(*) as count FROM browse_history WHERE user_id = ?");
        checkQuery.addBindValue(userId);

        if (!checkQuery.exec() || !checkQuery.next()) {
            db.rollback();
            QJsonObject err;
            err["status"] = "failed";
            err["message"] = "检查记录数量失败";
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
        }

        int recordCount = checkQuery.value("count").toInt();

        // 2. 如果已有记录，先删除最旧的一条（如果超过9条）
        // 使用子查询找到最早的记录，保持最多10条记录
        if (recordCount >= 10) {
            QSqlQuery deleteQuery(db);
            deleteQuery.prepare(R"(
                DELETE FROM browse_history
                WHERE user_id = ?
                AND id IN (
                    SELECT id FROM (
                        SELECT id FROM browse_history
                        WHERE user_id = ?
                        ORDER BY browse_time ASC
                        LIMIT 1
                    ) as tmp
                )
            )");
            deleteQuery.addBindValue(userId);
            deleteQuery.addBindValue(userId);

            if (!deleteQuery.exec()) {
                db.rollback();
                QJsonObject err;
                err["status"] = "failed";
                err["message"] = "删除旧记录失败";
                return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
            }
        }

        // 3. 插入新的浏览记录
        QSqlQuery insertQuery(db);

        if (!flightSnapshot.isEmpty()) {
            // 如果有快照数据，保存为JSON
            QJsonDocument snapshotDoc(flightSnapshot);
            QString snapshotStr = QString::fromUtf8(snapshotDoc.toJson(QJsonDocument::Compact));

            insertQuery.prepare("INSERT INTO browse_history (user_id, flight_id, flight_data, browse_time) VALUES (?, ?, ?, NOW())");
            insertQuery.addBindValue(userId);
            insertQuery.addBindValue(flightId);
            insertQuery.addBindValue(snapshotStr);
        } else {
            insertQuery.prepare("INSERT INTO browse_history (user_id, flight_id, browse_time) VALUES (?, ?, NOW())");
            insertQuery.addBindValue(userId);
            insertQuery.addBindValue(flightId);
        }

        if (!insertQuery.exec()) {
            db.rollback();
            QJsonObject err;
            err["status"] = "failed";
            err["message"] = "添加浏览记录失败: " + insertQuery.lastError().text();
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
        }

        // 提交事务
        db.commit();

        QJsonObject success;
        success["status"] = "success";
        success["message"] = "浏览记录添加成功";
        success["history_id"] = insertQuery.lastInsertId().toInt();

        return QHttpServerResponse(success, QHttpServerResponse::StatusCode::Ok);

    } catch (...) {
        db.rollback();
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "系统错误";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }
}

QHttpServerResponse BrowseHistoryController::handleGetBrowseHistory(const QHttpServerRequest &request)
{
    // 解析请求体
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "无效的JSON格式";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    // 验证必需字段
    if (!jsonObj.contains("user_id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "缺少必要字段: user_id";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int userId = jsonObj["user_id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // 查询最近10条浏览记录，同时关联航班信息
    QSqlQuery query(db);
    query.prepare(R"(
        SELECT
            bh.id as history_id,
            bh.browse_time,
            bh.flight_data,
            f.ID as flight_id,
            f.flight_number,
            f.airline,
            f.origin,
            f.destination,
            f.departure_time,
            f.landing_time,
            f.aircraft_model,
            f.economy_price,
            f.business_price,
            f.first_class_price
        FROM browse_history bh
        LEFT JOIN flights f ON bh.flight_id = f.ID
        WHERE bh.user_id = ?
        ORDER BY bh.browse_time DESC
        LIMIT 10
    )");
    query.addBindValue(userId);

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "查询浏览记录失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray historyList;
    while (query.next()) {
        QJsonObject historyItem;

        // 浏览记录基本信息
        historyItem["history_id"] = query.value("history_id").toInt();

        // 时间格式化
        QDateTime browseTime = query.value("browse_time").toDateTime();
        historyItem["browse_time"] = browseTime.toString("yyyy-MM-dd HH:mm:ss");
        historyItem["browse_time_relative"] = getRelativeTime(browseTime);

        // 航班信息
        historyItem["flight_id"] = query.value("flight_id").toInt();
        historyItem["flight_number"] = query.value("flight_number").toString();
        historyItem["airline"] = query.value("airline").toString();
        historyItem["origin"] = query.value("origin").toString();
        historyItem["destination"] = query.value("destination").toString();

        // 时间格式化
        QDateTime depTime = query.value("departure_time").toDateTime();
        QDateTime arrTime = query.value("landing_time").toDateTime();
        historyItem["departure_time"] = depTime.toString("yyyy-MM-dd HH:mm");
        historyItem["landing_time"] = arrTime.toString("HH:mm");

        historyItem["aircraft_model"] = query.value("aircraft_model").toString();

        // 价格信息
        historyItem["economy_price"] = query.value("economy_price").toInt();
        historyItem["business_price"] = query.value("business_price").toInt();
        historyItem["first_class_price"] = query.value("first_class_price").toInt();

        // 如果有快照数据，也返回
        QString flightDataStr = query.value("flight_data").toString();
        if (!flightDataStr.isEmpty()) {
            QJsonDocument snapshotDoc = QJsonDocument::fromJson(flightDataStr.toUtf8());
            if (snapshotDoc.isObject()) {
                historyItem["flight_snapshot"] = snapshotDoc.object();
            }
        }

        historyList.append(historyItem);
    }

    QJsonObject response;
    response["status"] = "success";
    response["message"] = "获取浏览记录成功";
    response["data"] = historyList;
    response["count"] = historyList.size();

    return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse BrowseHistoryController::handleClearBrowseHistory(const QHttpServerRequest &request)
{
    // 解析请求体
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    if (!jsonDoc.isObject()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "无效的JSON格式";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QJsonObject jsonObj = jsonDoc.object();

    // 验证必需字段
    if (!jsonObj.contains("user_id")) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "缺少必要字段: user_id";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    int userId = jsonObj["user_id"].toInt();

    QSqlDatabase db = DatabaseManager::getConnection();
    if (!db.isOpen()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "数据库连接失败";
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QSqlQuery query(db);
    query.prepare("DELETE FROM browse_history WHERE user_id = ?");
    query.addBindValue(userId);

    if (!query.exec()) {
        QJsonObject err;
        err["status"] = "failed";
        err["message"] = "清空浏览记录失败: " + query.lastError().text();
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonObject response;
    response["status"] = "success";
    response["message"] = "浏览记录已清空";
    response["deleted_count"] = query.numRowsAffected();

    return QHttpServerResponse(response, QHttpServerResponse::StatusCode::Ok);
}

// 辅助函数：获取相对时间（如"1小时前"）
QString BrowseHistoryController::getRelativeTime(const QDateTime &time)
{
    qint64 seconds = time.secsTo(QDateTime::currentDateTime());

    if (seconds < 60) {
        return "刚刚";
    } else if (seconds < 3600) {
        return QString("%1分钟前").arg(seconds / 60);
    } else if (seconds < 86400) {
        return QString("%1小时前").arg(seconds / 3600);
    } else if (seconds < 604800) {
        return QString("%1天前").arg(seconds / 86400);
    } else {
        return time.toString("yyyy-MM-dd");
    }
}
