#ifndef AICONTROLLER_H
#define AICONTROLLER_H

#include "BaseController.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>

class AIController : public BaseController
{
    Q_OBJECT
public:
    explicit AIController(QObject *parent = nullptr);
    void registerRoutes(QHttpServer *server) override;

private:
    // 处理 AI 对话请求
    QHttpServerResponse handleAIChat(const QHttpServerRequest &request);

    // 辅助：调用大模型 API 解析意图 (新增 history 参数)
    QJsonObject callLLMToParseIntent(const QString &userText, const QJsonArray &history);

    // 辅助：调用大模型生成回复 (新增 history 参数)
    QString callLLMToChat(const QString &systemPrompt, const QString &userText, const QJsonArray &history);

    // 辅助：根据解析出的参数查库
    QJsonArray searchFlightsInDB(const QString &from, const QString &to, const QString &date);

    // 辅助：通用的 LLM 网络请求发送函数 (避免代码重复)
    QJsonObject performLLMRequest(const QJsonObject &payload);

    QNetworkAccessManager *manager;
};

#endif // AICONTROLLER_H
