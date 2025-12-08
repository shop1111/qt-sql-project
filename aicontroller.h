#ifndef AICONTROLLER_H
#define AICONTROLLER_H

#include "BaseController.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>

class AIController : public BaseController
{
    Q_OBJECT
public:
    explicit AIController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server) override;

private:
    // 处理 AI 对话请求
    QHttpServerResponse handleAIChat(const QHttpServerRequest &request);

    // 辅助：调用大模型 API 解析意图
    QJsonObject callLLMToParseIntent(const QString &userText);
    // 调用大模型生成回复
    QString callLLMToChat(const QString &systemPrompt, const QString &userText);
    // 辅助：根据解析出的参数查库
    QJsonArray searchFlightsInDB(const QString &from, const QString &to, const QString &date);

    QNetworkAccessManager *manager;
};

#endif // AICONTROLLER_H
