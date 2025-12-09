#include "AIController.h"
#include "DatabaseManager.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QEventLoop>
#include <QDate>
#include <QDebug>
#include <QNetworkRequest>
#include <QUrl>
#include <QSqlQuery>   // <--- 解决 incomplete type 'QSqlQuery'
#include <QSqlError>   // <--- 建议加上，方便打印数据库错误

#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>


QString getAiConfig(const QString &key, const QString &defaultValue = "") {
    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";

    // 检查文件是否存在
    if (!QFileInfo::exists(configPath)) {
        qWarning() << "配置文件未找到，将使用默认值:" << configPath;
        return defaultValue;
    }

    QSettings settings(configPath, QSettings::IniFormat);
    return settings.value("AI/" + key, defaultValue).toString();
}


AIController::AIController(QObject *parent) : BaseController(parent)
{
    manager = new QNetworkAccessManager(this);
}

void AIController::registerRoutes(QHttpServer *server)
{
    // 路由：POST /api/ai_chat
    // 请求体: { "message": "我想查明天北京到上海的航班" }
    server->route("/api/ai_chat", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleAIChat(req);
                  });
}

// AIController.cpp

QHttpServerResponse AIController::handleAIChat(const QHttpServerRequest &request)
{
    // 1. 获取用户输入
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject jsonObj = jsonDoc.object();
    QString userMessage = jsonObj["message"].toString(); // 比如 "帮我查明天的票"

    // 2. 第一步：尝试提取意图
    QJsonObject intent = callLLMToParseIntent(userMessage);

    QString aiReplyText;
    QJsonArray flightData; // 用于可能的结构化返回

    // 判断意图是否完整（比如必须有 from 和 to 才能查）
    if (!intent.isEmpty() && intent.contains("from") && intent.contains("to") && !intent["from"].toString().isEmpty()&&intent.contains("to")) {

        // --- 情况 A：意图明确，执行查询 ---
        QString from = intent["from"].toString();
        QString to = intent["to"].toString();
        QString date = intent["date"].toString();
        if (date.isEmpty() || date == "null") date = QDate::currentDate().addDays(1).toString("yyyy-MM-dd");//自动取明天

        // 查库
        flightData = searchFlightsInDB(from, to, date);

        // --- 核心改动：把查到的数据转成字符串，喂给 AI ---
        QString dataStr = QJsonDocument(flightData).toJson(QJsonDocument::Compact);

        QString systemPrompt = QString(
                                   "你是一个航班助手。用户正在查询航班。\n"
                                   "数据库查询结果如下（JSON格式）：\n%1\n"
                                   "请根据上述数据回答用户的问题。如果数据为空，请礼貌告知没有查到。\n"
                                   "不要直接把JSON甩给用户，要用自然的语言描述，比如'为您查到3趟航班，分别是...'。"
                                   "如果有多趟，简要列出价格和时间。"
                                   ).arg(dataStr);

        // 让 AI 生成最终回复
        aiReplyText = callLLMToChat(systemPrompt, userMessage);

    } else {
        // --- 情况 B：意图不全 或 纯闲聊 ---
        // 比如用户只说了 "你好" 或者 "我想去旅游"

        QString systemPrompt = "你是一个航班查询助手。请简短热情地回复用户。如果用户想查票但没提供地点，请追问出发地和目的地。";
        aiReplyText = callLLMToChat(systemPrompt, userMessage);
    }

    // 3. 返回给前端
    // 前端现在只需要展示 aiReplyText 即可，如果需要展示卡片，也可以带上 flightData
    QJsonObject responseObj;
    responseObj["status"] = "success";
    responseObj["message"] = "AI回复成功"; // AI 的自然语言回复
    QJsonObject ai_chat_data;
    // 如果查到了数据，也顺便带给前端（前端可以用卡片形式展示，也可以只展示文字）
    if (!flightData.isEmpty()) {
        ai_chat_data["data"] = flightData;
        ai_chat_data["type"] = "flight_list_with_chat";
    } else {
        ai_chat_data["type"] = "chat_only";
    }
    ai_chat_data["chat"] = aiReplyText;
    responseObj["data"] = ai_chat_data;
    return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
}

// 调用 LLM 提取from,to,date
QJsonObject AIController::callLLMToParseIntent(const QString &userText)
{
    // 构造 System Prompt：这是 AI 的“人设”
    // 核心技巧：强制 AI 只返回 JSON，不要废话
    QString systemPrompt = R"(
        你是一个航班查询助手。请从用户的输入中提取：
        1. from (出发城市中文名，如"北京")
        2. to (到达城市中文名，如"上海")
        3. date (日期，格式 YYYY-MM-DD。如果用户说"明天"，请根据当前日期推算。如果未提及，返回null)

        当前日期是: )" + QDate::currentDate().toString("yyyy-MM-dd") + R"(

        请严格只返回一个 JSON 对象，格式如下：
        {"from": "北京", "to": "上海", "date": "2025-12-01"}
        如果无法提取地点，返回空 JSON {}。
    )";

    // 构造请求体
    QJsonObject messageSystem;
    messageSystem["role"] = "system";
    messageSystem["content"] = systemPrompt;

    QJsonObject messageUser;
    messageUser["role"] = "user";
    messageUser["content"] = userText;

    QJsonArray messages;
    messages.append(messageSystem);
    messages.append(messageUser);

    QJsonObject payload;
    payload["model"] = "qwen-turbo"; // 根据你的服务商修改模型名称，如 gpt-3.5-turbo
    payload["messages"] = messages;
    payload["temperature"] = 0.1; // 温度设低，让 AI 回答更严谨
    QString apiUrl = getAiConfig("ApiUrl", "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions");
    // 读取 Key
    QString apiKey = getAiConfig("ApiKey");
    QNetworkRequest req(apiUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());

    // 发送请求并同步等待结果 (利用 QEventLoop)
    QNetworkReply *reply = manager->post(req, QJsonDocument(payload).toJson());
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "AI Network Error:" << reply->errorString();
        reply->deleteLater();
        return QJsonObject();
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    // 解析 AI 的回复
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    QString content = doc.object()["choices"].toArray().first().toObject()["message"].toObject()["content"].toString();

    // 清理可能的 Markdown 标记 (```json ... ```)
    content.remove("```json");
    content.remove("```");

    qDebug() << "AI Raw Response:" << content;

    return QJsonDocument::fromJson(content.toUtf8()).object();
}

// 复用数据库查询逻辑
QJsonArray AIController::searchFlightsInDB(const QString &from, const QString &to, const QString &date)
{
    QSqlDatabase db = DatabaseManager::getConnection();
    QJsonArray flightList;
    if (!db.isOpen()) return flightList;

    QSqlQuery query(db);
    // 这里直接用中文名查，因为 flights 表存的是中文，且 AI 提取的也是中文
    QString sql = "SELECT * FROM flights WHERE origin = ? AND destination = ? AND DATE(departure_time) = ?";

    query.prepare(sql);
    query.addBindValue(from);
    query.addBindValue(to);
    query.addBindValue(date);

    if (query.exec()) {
        while (query.next()) {
            QJsonObject flight;
            flight["id"] = query.value("ID").toInt();
            flight["flight_number"] = query.value("flight_number").toString();
            flight["airline"] = query.value("airline").toString();
            flight["aircraft_model"] = query.value("aircraft_model").toString();

            QDateTime depTime = query.value("departure_time").toDateTime();
            QDateTime arrTime = query.value("landing_time").toDateTime();
            flight["departure_time"] = depTime.toString("HH:mm");
            flight["landing_time"] = arrTime.toString("HH:mm");
            flight["price"] = query.value("economy_price").toInt(); // 默认展示经济舱价格

            flightList.append(flight);
        }
    }
    return flightList;
}


// --- 新增通用对话函数 ---
QString AIController::callLLMToChat(const QString &systemPrompt, const QString &userText)
{
    // 构造请求
    QJsonObject messageSystem;
    messageSystem["role"] = "system";
    messageSystem["content"] = systemPrompt;

    QJsonObject messageUser;
    messageUser["role"] = "user";
    messageUser["content"] = userText;

    QJsonArray messages;
    messages.append(messageSystem);
    messages.append(messageUser);

    QJsonObject payload;
    payload["model"] = "qwen-turbo"; // 或 gpt-3.5-turbo
    payload["messages"] = messages;
    payload["temperature"] = 0.7; // 稍微高一点，让说话自然些

    QString apiUrl = getAiConfig("ApiUrl", "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions");
    // 读取 Key
    QString apiKey = getAiConfig("ApiKey");
    QUrl url(apiUrl);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QString authValue = "Bearer " + apiKey;
    req.setRawHeader("Authorization", authValue.toUtf8());

    // 发送并等待
    QNetworkReply *reply = manager->post(req, QJsonDocument(payload).toJson());
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "AI Chat Error:" << reply->errorString();
        reply->deleteLater();
        return "抱歉，AI连接出了点问题。";
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    // 提取 content
    return doc.object()["choices"].toArray().first().toObject()["message"].toObject()["content"].toString();
}
