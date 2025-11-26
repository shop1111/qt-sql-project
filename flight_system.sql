-- 创建数据库
CREATE DATABASE IF NOT EXISTS flight_system CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE flight_system;

-- 1. 用户表 (对应 表1.png)
CREATE TABLE IF NOT EXISTS users (
    U_ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(30) NOT NULL,
    true_name VARCHAR(30) NULL,
    nickname VARCHAR(30) NULL,
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


INSERT INTO users (username, true_name, nickname, telephone, password, P_ID, email, photo) VALUES 
('zhangsan', '张三', '法外狂徒', '13800138001', 'pass123', '110101199001011234', 'zs@example.com', NULL),
('lisi', '李四', '朝阳群众', '13900139002', 'pass456', '110101199202025678', 'ls@example.com', NULL),
('wangwu', '王五', '隔壁老王', '13700137003', 'pass789', '110101198803039012', 'ww@example.com', NULL);

INSERT INTO flights (flight_number, origin, destination, departure_time, landing_time, airline, aircraft_model, economy_seats, economy_price, business_seats, business_price, first_class_seats, first_class_price) VALUES 
('CA1001', '北京', '上海', '2025-12-01 08:00:00', '2025-12-01 10:15:00', '中国国航', 'Boeing 737', 150, 800, 20, 2000, 8, 4500),
('MU2567', '上海', '东京', '2025-12-02 14:30:00', '2025-12-02 18:30:00', '东方航空', 'Airbus A330', 200, 2500, 30, 5000, 10, 12000),
('CZ3888', '广州', '纽约', '2025-12-05 23:00:00', '2025-12-06 14:00:00', '南方航空', 'Boeing 787', 220, 6000, 40, 15000, 12, 35000);

INSERT INTO orders (user_id, flight_id, seat_type, seat_number, order_date) VALUES 
(1, 1, 0, '12A', '2025-11-26 10:00:00'),
(2, 1, 0, '12B', '2025-11-26 10:05:00'),
(3, 2, 1, '01F', '2025-11-26 11:00:00');