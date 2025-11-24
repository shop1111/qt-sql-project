#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H


#include <QSqlDatabase>
#include <QThread>
#include <QDebug>
#include <QSqlError>
#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>

class DatabaseManager {
public:
    static QSqlDatabase getConnection() {
        // 用当前线程的 ID 作为连接的唯一名称
        const QString connectionName = QString("conn_%1").arg((quintptr)QThread::currentThreadId());
        // applicationDirPath() 指向的是 .exe 所在的目录
        QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";

        // 检查文件是否存在
        if (!QFileInfo::exists(configPath)) {
            qWarning() << "配置文件 config.ini 未找到！请拷贝 config.example.ini 并修改配置。";
            return QSqlDatabase();
        }

        QSettings settings(configPath, QSettings::IniFormat);

        if (QSqlDatabase::contains(connectionName)) {
            return QSqlDatabase::database(connectionName, false);
        }

        QString dbHost = settings.value("Database/Host", "localhost").toString();
        QString dbName = settings.value("Database/Name", "flight_system").toString();
        QString dbUser = settings.value("Database/User", "root").toString();
        QString dbPass = settings.value("Database/Password", "").toString(); // 默认为空，强迫用户配置

        QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", connectionName);
        db.setHostName(dbHost);
        db.setDatabaseName(dbName);
        db.setUserName(dbUser);
        db.setPassword(dbPass);
        if (!db.open()) {
            qWarning() << "DB Error:" << db.lastError().text();
        }
        return db;
    }
};


#endif // DATABASEMANAGER_H
