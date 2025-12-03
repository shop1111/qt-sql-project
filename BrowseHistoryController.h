#ifndef BROWSEHISTORYCONTROLLER_H
#define BROWSEHISTORYCONTROLLER_H

#include "BaseController.h"
#include <QHttpServerResponse>
#include <QHttpServerRequest>
#include <QDateTime>

class BrowseHistoryController : public BaseController
{
    Q_OBJECT
public:
    explicit BrowseHistoryController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server) override;

private:
    // 记录浏览历史（当用户查看航班详情时调用）
    QHttpServerResponse handleAddBrowseHistory(const QHttpServerRequest &request);

    // 获取用户最近10条浏览记录
    QHttpServerResponse handleGetBrowseHistory(const QHttpServerRequest &request);

    // 清空用户的浏览记录
    QHttpServerResponse handleClearBrowseHistory(const QHttpServerRequest &request);

    // 辅助函数：获取相对时间（如"1小时前"）
    QString getRelativeTime(const QDateTime &time);
};

#endif // BROWSEHISTORYCONTROLLER_H
