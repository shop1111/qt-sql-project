#include "aicontroller.h"
#include "DatabaseManager.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QEventLoop>
#include <QDate>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>

// 辅助函数：读取配置文件
QString getAiConfig(const QString &key, const QString &defaultValue = "") {
    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    if (!QFileInfo::exists(configPath)) {
        // 如果没有配置文件，可以使用默认值或者硬编码
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
    // 请求体示例:
    // {
    //   "message": "明天",
    //   "history": [ {"role":"user", "content":"我想去北京"}, {"role":"assistant", "content":"请问您从哪里出发？"} ]
    // }
    server->route("/api/ai_chat", QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) {
                      return handleAIChat(req);
                  });
}

QHttpServerResponse AIController::handleAIChat(const QHttpServerRequest &request)
{
    // 1. 解析请求体
    QJsonDocument jsonDoc = QJsonDocument::fromJson(request.body());
    QJsonObject reqObj = jsonDoc.object();

    QString userMessage = reqObj["message"].toString();
    QJsonArray history = reqObj["history"].toArray(); // 获取前端传来的历史上下文

    // 2. 意图解析 (传入 history，让 AI 结合上下文理解 "明天" 指的是 "明天去哪")
    QJsonObject intent = callLLMToParseIntent(userMessage, history);

    // 提取解析结果
    QString type = intent["type"].toString();
    QString from = intent["from"].toString();
    QString to = intent["to"].toString();
    QString date = intent["date"].toString();

    QString aiReplyText;
    QJsonArray flightData;

    // --- 分支 A：意图是查票，且信息完整 ---
    if (type == "query" && !from.isEmpty() && !to.isEmpty()) {

        // 日期处理：如果 AI 没解析出日期，默认查明天
        bool isDateGuessed = false;
        if (date.isEmpty() || date == "null") {
            date = QDate::currentDate().addDays(1).toString("yyyy-MM-dd");
            isDateGuessed = true;
        }

        // 查库
        flightData = searchFlightsInDB(from, to, date);
        QString dataStr = QJsonDocument(flightData).toJson(QJsonDocument::Compact);

        // 构造 System Prompt (注入查询结果)
        QString systemPrompt = QString(
                                   "你是一个专业的票务专家。用户查询：%1 -> %2 在 %3 的航班。\n"
                                   "%4" // 插入日期推断提示
                                   "数据库查询结果如下(JSON)：\n%5\n"
                                   "要求：\n"
                                   "1. 如果有数据：直接推荐性价比最高和时间最早的航班。不要罗列JSON代码，用自然语言回答。\n"
                                   "2. 如果无数据：礼貌告知，并建议用户换个日期。\n"
                                   "3. 语气热情专业。"
                                   ).arg(from, to, date,
                                        isDateGuessed ? "(注意：用户未指定日期，我已默认帮他查询了明天的航班，请在回复中说明这一点)。" : "",
                                        dataStr);

        // 生成回复 (传入 history 以保持对话连贯性)
        aiReplyText = callLLMToChat(systemPrompt, userMessage, history);

    }
    // --- 分支 B：意图是查票，但缺少关键信息 ---
    else if (type == "query" && (!from.isEmpty() || !to.isEmpty())) {

        // 确定缺什么
        QString missingInfo;
        if (from.isEmpty()) missingInfo += "出发地";
        if (to.isEmpty()) missingInfo += (missingInfo.isEmpty() ? "" : "和") + QString("目的地");

        QString systemPrompt = QString(
                                   "你是一个航班助手。用户想查票，但缺少: %1。\n"
                                   "当前已识别: from=%2, to=%3。\n"
                                   "请礼貌地根据当前已知信息追问缺失信息。例如：'收到，去%3，请问您从哪里出发？'"
                                   ).arg(missingInfo, from.isEmpty() ? "?" : from, to.isEmpty() ? "?" : to);

        aiReplyText = callLLMToChat(systemPrompt, userMessage, history);
    }
    // --- 分支 C：闲聊或其他 ---
    else {
        QString systemPrompt = "你是一个风趣的航空旅行助手。简短热情地回复用户。如果用户提到旅行计划，可以主动问是否需要查票。可以尝试推荐一些热门的旅游景点。";
        aiReplyText = callLLMToChat(systemPrompt, userMessage, history);
    }

    // 3. 构造返回 JSON
    QJsonObject responseObj;
    responseObj["status"] = "success";

    QJsonObject dataObj;
    dataObj["chat"] = aiReplyText; // AI 的自然语言回复

    // 如果查到了数据，也带上（前端可用于渲染卡片）
    if (!flightData.isEmpty()) {
        dataObj["data"] = flightData;
        dataObj["type"] = "flight_list_with_chat";
    } else {
        dataObj["type"] = "chat_only";
    }

    responseObj["data"] = dataObj;
    return QHttpServerResponse(responseObj, QHttpServerResponse::StatusCode::Ok);
}

// 意图解析函数
QJsonObject AIController::callLLMToParseIntent(const QString &userText, const QJsonArray &history)
{
    QString currentDate = QDate::currentDate().toString("yyyy-MM-dd");

    // 将历史记录转为文本摘要，帮助意图解析
    QString historySummary;
    int start = (history.size() > 5) ? history.size() - 5 : 0; // 只看最近5条
    for (int i = start; i < history.size(); ++i) {
        QJsonObject obj = history[i].toObject();
        QString role = (obj["role"].toString() == "user") ? "用户" : "AI";
        historySummary += QString("%1: %2\n").arg(role, obj["content"].toString());
    }

    QString systemPrompt = QString(R"(
        你是一个智能意图解析器。当前日期: %1

        【任务】
        分析用户的意图。必须结合下面的【对话历史】来补充当前输入中缺失的信息。
        例如：如果历史中AI问“从哪出发？”，用户回“北京”，那么 intent.from = "北京"。

        【对话历史】
        %2

        【输出要求】
        只返回一个 JSON 对象，不要Markdown格式，格式如下：
        {
            "type": "query" (查票) 或 "chat" (闲聊),
            "from": "北京",   (中文城市名，无则null)
            "to": "上海",     (中文城市名，无则null)
            "date": "2025-12-01" (YYYY-MM-DD，若用户说"明天"请基于当前日期推算，无则null)
        }
    )").arg(currentDate, historySummary);

    // 构造 Messages
    QJsonArray messages;
    messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
    messages.append(QJsonObject{{"role", "user"}, {"content", userText}});

    QJsonObject payload;
    payload["model"] = "qwen-plus"; // 建议使用能力较强的模型进行意图解析
    payload["messages"] = messages;
    payload["temperature"] = 0.1; // 低温以保证 JSON 格式稳定

    QJsonObject resp = performLLMRequest(payload);

    // 解析返回的 JSON 字符串
    QString content = resp["content_str"].toString();
    // 清理 Markdown 代码块标记
    content.remove("```json");
    content.remove("```");

    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    return doc.object();
}

// 对话生成函数
QString AIController::callLLMToChat(const QString &systemPrompt, const QString &userText, const QJsonArray &history)
{
    QJsonArray messages;

    // 1. System Prompt (最高优先级)
    messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});

    // 2. 插入历史记录 (上下文)
    // 限制条数防止 Token 溢出，取最近 10 条
    int start = (history.size() > 10) ? history.size() - 10 : 0;
    for (int i = start; i < history.size(); ++i) {
        QJsonObject obj = history[i].toObject();
        // 确保字段清洗，只保留 role 和 content
        QJsonObject cleanObj;
        cleanObj["role"] = obj["role"];
        cleanObj["content"] = obj["content"];
        messages.append(cleanObj);
    }

    // 3. 当前用户输入
    messages.append(QJsonObject{{"role", "user"}, {"content", userText}});

    QJsonObject payload;
    payload["model"] = "qwen-turbo"; // 对话可以使用快模型
    payload["messages"] = messages;
    payload["temperature"] = 0.7; // 稍高温度让回答自然

    QJsonObject resp = performLLMRequest(payload);
    return resp["content_str"].toString();
}

// 查库函数 (保持不变)
QJsonArray AIController::searchFlightsInDB(const QString &from, const QString &to, const QString &date)
{
    QSqlDatabase db = DatabaseManager::getConnection();
    QJsonArray flightList;
    if (!db.isOpen()) return flightList;

    QSqlQuery query(db);
    // 注意：flights 表结构应与 flight_system.sql 一致
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
            flight["price"] = query.value("economy_price").toInt();

            flightList.append(flight);
        }
    }
    return flightList;
}

// 通用 LLM 请求函数
QJsonObject AIController::performLLMRequest(const QJsonObject &payload)
{
    // 从配置文件读取配置，支持回退
    QString apiUrl = getAiConfig("ApiUrl", "https://open.bigmodel.cn/api/paas/v4/chat/completions");
    QString apiKey = getAiConfig("ApiKey", "");

    QNetworkRequest req((QUrl(apiUrl)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());

    // // --- 新增：打印请求体 (调试核心) ---
    // QByteArray requestData = QJsonDocument(payload).toJson();
    // qInfo() << "\n[AI Request] Sending to LLM:\n" << requestData;
    // // --------------------------------

    QNetworkReply *reply = manager->post(req, QJsonDocument(payload).toJson());

    // 同步等待 (虽然会阻塞线程，但代码简单；生产环境建议用异步 callback)
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QJsonObject result;
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "AI Request Error:" << reply->errorString();
        // 返回错误提示给调用方，防止崩溃
        result["content_str"] = "抱歉，AI连接出现网络错误，请稍后再试。";
    } else {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        // 提取 content
        // 假设 API 返回结构符合 OpenAI 标准
        if (doc.object().contains("choices") && !doc.object()["choices"].toArray().isEmpty()) {
            QJsonObject choice = doc.object()["choices"].toArray().first().toObject();
            result["content_str"] = choice["message"].toObject()["content"].toString();
        } else {
            qWarning() << "AI Response Format Error:" << responseData;
            result["content_str"] = "抱歉，AI返回的数据格式异常。";
        }
    }

    reply->deleteLater();
    return result;
}
