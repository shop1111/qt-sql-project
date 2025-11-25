-- 创建数据库
CREATE DATABASE IF NOT EXISTS flight_system CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE flight_system;

-- 1. 用户表 (对应 表1.png)
CREATE TABLE IF NOT EXISTS users (
    U_ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(30) NOT NULL,
    telephone VARCHAR(11) NOT NULL,
    password VARCHAR(30) NOT NULL,
    P_ID VARCHAR(18) NOT NULL,
    email VARCHAR(40) NULL,
    photo VARCHAR(100) NULL,
    UNIQUE KEY unique_tele (telephone),
    UNIQUE KEY unique_pid (P_ID)
);

-- 2. 航班表 (对应 表2.png，并适配现有后端代码)
CREATE TABLE IF NOT EXISTS flights (
    ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    flight_number VARCHAR(10) NOT NULL,           -- 班次号
    origin VARCHAR(50) NOT NULL,                  -- 出发地
    destination VARCHAR(50) NOT NULL,             -- 目的地
    departure_time DATETIME NOT NULL,             -- 起飞时间 (改为DATETIME类型)
    landing_time DATETIME NOT NULL,               -- 落地时间 (改为DATETIME类型)
    airline VARCHAR(20) NOT NULL,                 -- 航空公司
    aircraft_model VARCHAR(15) NOT NULL,          -- 飞机型号
    economy_seats INT NOT NULL,                   -- 经济舱座位数量
    economy_price INT NOT NULL,                   -- 经济舱价格
    business_seats INT NOT NULL,                  -- 商务舱座位数量
    business_price INT NOT NULL,                  -- 商务舱价格
    first_class_seats INT NOT NULL,               -- 头等舱座位数量
    first_class_price INT NOT NULL,               -- 头等舱价格
    UNIQUE KEY unique_flight_number (flight_number)
);

-- 3. 订单表 (对应 表3.png)
CREATE TABLE IF NOT EXISTS orders (
    ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    order_date DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,  -- 预定日期 (改为DATETIME类型)
    user_id INT NOT NULL,                         -- 用户ID
    flight_id INT NOT NULL,                       -- 航班ID
    seat_type INT NOT NULL,                       -- 座位类型 (0:经济舱, 1:商务舱, 2:头等舱)
    seat_number VARCHAR(50) NOT NULL,             -- 座位号
    FOREIGN KEY (user_id) REFERENCES users(U_ID) ON DELETE CASCADE,
    FOREIGN KEY (flight_id) REFERENCES flights(ID) ON DELETE CASCADE,
    UNIQUE KEY unique_seat_assignment (flight_id, seat_number)
);

show tables;
