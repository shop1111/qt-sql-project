-- 创建数据库
CREATE DATABASE IF NOT EXISTS flight_system CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE flight_system;

-- 1. 用户表
CREATE TABLE IF NOT EXISTS users (
    U_ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(30) NOT NULL,
    true_name VARCHAR(30) NULL,
    nickname VARCHAR(30) NULL,
    telephone VARCHAR(11) NOT NULL,
    password VARCHAR(30) NOT NULL,
    P_ID VARCHAR(18) NULL,
    email VARCHAR(40) NULL,
    photo VARCHAR(100) NULL,
    UNIQUE KEY unique_tele (telephone),
    UNIQUE KEY unique_pid (P_ID),
    UNIQUE KEY unique_username (username)
);

-- 2. 航班表
CREATE TABLE IF NOT EXISTS flights (
    ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    flight_number VARCHAR(10) NOT NULL,
    origin VARCHAR(50) NOT NULL,
    destination VARCHAR(50) NOT NULL,
    departure_time DATETIME NOT NULL,
    landing_time DATETIME NOT NULL,
    airline VARCHAR(20) NOT NULL,
    aircraft_model VARCHAR(15) NOT NULL,
    economy_seats INT NOT NULL,
    economy_price INT NOT NULL,
    business_seats INT NOT NULL,
    business_price INT NOT NULL,
    first_class_seats INT NOT NULL,
    first_class_price INT NOT NULL,
    UNIQUE KEY unique_flight_number (flight_number)
);

-- 3. 订单表 - 完整修复版
CREATE TABLE IF NOT EXISTS orders (
    ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    order_id VARCHAR(50) NULL COMMENT '前端订单号',
    order_date DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    user_id INT NOT NULL,
    flight_id INT NOT NULL,
    seat_type INT NOT NULL COMMENT '0:经济舱, 1:商务舱, 2:头等舱',
    seat_number VARCHAR(50) NOT NULL,
    status VARCHAR(20) DEFAULT '未支付' COMMENT '未支付, 已支付, 已取消, 已完成, 已退款',
    total_amount DECIMAL(10, 2) DEFAULT 0.00,
    paid_amount DECIMAL(10, 2) DEFAULT 0.00,
    payment_method VARCHAR(20) NULL COMMENT 'balance-余额, wechat-微信, alipay-支付宝',
    payment_time DATETIME NULL,
    locked_at DATETIME NULL COMMENT '锁定时间（用于座位锁定）',
    locked_by INT NULL COMMENT '锁定用户ID',
    payment_id INT NULL COMMENT '关联的支付ID',
    cancelled_at DATETIME NULL COMMENT '取消时间',

    FOREIGN KEY (user_id) REFERENCES users(U_ID) ON DELETE CASCADE,
    FOREIGN KEY (flight_id) REFERENCES flights(ID) ON DELETE CASCADE,
    UNIQUE KEY unique_order_id (order_id),
    INDEX idx_flight_seat (flight_id, seat_number),
    INDEX idx_status (status),
    INDEX idx_user (user_id)
);
-- 在 orders 表添加更多payment相关字段
ALTER TABLE orders
ADD COLUMN payment_time DATETIME NULL COMMENT '支付时间',
ADD COLUMN payment_method VARCHAR(20) NULL COMMENT '支付方式: wechat-微信, alipay-支付宝, bank-银行卡';

-- 4. 城市代码映射表
CREATE TABLE IF NOT EXISTS city_codes (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    city_name VARCHAR(50) NOT NULL COMMENT '城市中文名',
    city_code VARCHAR(3) NOT NULL COMMENT 'IATA三字码',
    pinyin VARCHAR(50) NULL COMMENT '城市拼音，方便后续做模糊搜索',
    UNIQUE KEY unique_code (city_code)
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 5. 浏览历史表
CREATE TABLE IF NOT EXISTS browse_history (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    flight_id INT NOT NULL,
    flight_data JSON NULL,
    browse_time DATETIME DEFAULT CURRENT_TIMESTAMP,

    FOREIGN KEY (user_id) REFERENCES users(U_ID) ON DELETE CASCADE,
    FOREIGN KEY (flight_id) REFERENCES flights(ID) ON DELETE CASCADE,
    INDEX idx_user (user_id),
    INDEX idx_browse_time (browse_time)
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 6. 用户账户表
CREATE TABLE IF NOT EXISTS user_accounts (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    balance DECIMAL(10, 2) DEFAULT 0.00 COMMENT '账户余额',
    total_recharge DECIMAL(10, 2) DEFAULT 0.00 COMMENT '累计充值',
    last_recharge_time DATETIME NULL COMMENT '最后充值时间',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(U_ID) ON DELETE CASCADE,
    UNIQUE KEY unique_user_account (user_id)
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 7. 充值记录表
CREATE TABLE IF NOT EXISTS recharge_records (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    order_no VARCHAR(50) NOT NULL COMMENT '充值订单号',
    amount DECIMAL(10, 2) NOT NULL COMMENT '充值金额',
    status VARCHAR(20) DEFAULT 'success' COMMENT '充值状态',
    recharge_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(U_ID) ON DELETE CASCADE,
    UNIQUE KEY unique_order_no (order_no),
    INDEX idx_user_time (user_id, recharge_time)
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- ============================================
-- 数据初始化
-- ============================================

-- 1. 插入示例用户数据
INSERT INTO users (U_ID, username, true_name, nickname, telephone, password, P_ID, email, photo) VALUES
(1, 'zhangsan', '张三', '法外狂徒', '13800138001', 'pass123', '110101199001011234', 'zs@example.com', NULL),
(2, 'lisi', '李四', '朝阳群众', '13900139002', 'pass456', '110101199202025678', 'ls@example.com', NULL),
(3, 'wangwu', '王五', '隔壁老王', '13700137003', 'pass789', '110101198803039012', 'ww@example.com', NULL),
(4, 'admin', '管理员', '系统管理员', '13600136000', 'admin123', '110101199501011111', 'admin@example.com', NULL),
(123, 'test123', '测试用户123', '测试昵称', '13800138123', 'test123', '110101199001011235', 'test123@example.com', NULL);

-- 2. 插入城市代码数据
INSERT INTO city_codes (city_name, city_code, pinyin) VALUES
('北京', 'BJS', 'Beijing'),
('上海', 'SHA', 'Shanghai'),
('广州', 'CAN', 'Guangzhou'),
('深圳', 'SZX', 'Shenzhen'),
('珠海', 'ZUH', 'Zhuhai'),
('成都', 'CTU', 'Chengdu'),
('杭州', 'HGH', 'Hangzhou'),
('昆明', 'KMG', 'Kunming'),
('西安', 'XIY', 'Xian'),
('重庆', 'CKG', 'Chongqing'),
('武汉', 'WUH', 'Wuhan'),
('南京', 'NKG', 'Nanjing'),
('厦门', 'XMN', 'Xiamen'),
('长沙', 'CSX', 'Changsha'),
('海口', 'HAK', 'Haikou'),
('三亚', 'SYX', 'Sanya'),
('青岛', 'TAO', 'Qingdao'),
('大连', 'DLC', 'Dalian'),
('天津', 'TSN', 'Tianjin'),
('郑州', 'CGO', 'Zhengzhou'),
('沈阳', 'SHE', 'Shenyang'),
('哈尔滨', 'HRB', 'Harbin'),
('乌鲁木齐', 'URC', 'Urumqi'),
('贵阳', 'KWE', 'Guiyang'),
('南宁', 'NNG', 'Nanning'),
('福州', 'FOC', 'Fuzhou'),
('兰州', 'LHW', 'Lanzhou'),
('太原', 'TYN', 'Taiyuan'),
('长春', 'CGQ', 'Changchun'),
('南昌', 'KHN', 'Nanchang'),
('呼和浩特', 'HET', 'Hohhot'),
('宁波', 'NGB', 'Ningbo'),
('温州', 'WNZ', 'Wenzhou'),
('合肥', 'HFE', 'Hefei'),
('济南', 'TNA', 'Jinan'),
('石家庄', 'SJW', 'Shijiazhuang'),
('银川', 'INC', 'Yinchuan'),
('西宁', 'XNN', 'Xining'),
('拉萨', 'LXA', 'Lhasa'),
('丽江', 'LJG', 'Lijiang'),
('西双版纳', 'JHG', 'Xishuangbanna'),
('桂林', 'KWL', 'Guilin'),
('烟台', 'YNT', 'Yantai'),
('泉州', 'JJN', 'Quanzhou'),
('无锡', 'WUX', 'Wuxi'),
('洛阳', 'LYA', 'Luoyang');

-- 3. 插入航班数据
INSERT INTO flights (flight_number, origin, destination, departure_time, landing_time, airline, aircraft_model, economy_seats, economy_price, business_seats, business_price, first_class_seats, first_class_price) VALUES
('CA1001', '北京', '上海', '2025-12-01 08:00:00', '2025-12-01 10:15:00', '中国国航', 'Boeing 737', 150, 800, 20, 2000, 8, 4500),
('MU2567', '上海', '东京', '2025-12-02 14:30:00', '2025-12-02 18:30:00', '东方航空', 'Airbus A330', 200, 2500, 30, 5000, 10, 12000),
('CZ3888', '广州', '纽约', '2025-12-05 23:00:00', '2025-12-06 14:00:00', '南方航空', 'Boeing 787', 220, 6000, 40, 15000, 12, 35000),
('CA1502', '北京', '广州', '2025-12-10 10:00:00', '2025-12-10 13:30:00', '中国国航', 'Airbus A320', 180, 1200, 25, 3000, 6, 6000);

-- 4. 插入订单数据（包含测试订单）
INSERT INTO orders (order_id, user_id, flight_id, seat_type, seat_number, status, total_amount, paid_amount, order_date) VALUES
('ORD00001', 1, 1, 0, '12A', '已支付', 800.00, 800.00, '2025-11-26 10:00:00'),
('ORD00002', 2, 1, 0, '12B', '已支付', 800.00, 800.00, '2025-11-26 10:05:00'),
('ORD00003', 3, 2, 1, '01F', '已取消', 5000.00, 0.00, '2025-11-26 11:00:00'),
('ORD00004', 1, 3, 2, '01A', '未支付', 35000.00, 0.00, '2025-11-27 09:00:00'),
('ORD20251202001', 123, 1, 0, '15A', '未支付', 1250.00, 0.00, NOW());

-- 5. 插入浏览历史数据
INSERT INTO browse_history (user_id, flight_id, browse_time) VALUES
(1, 1, '2025-12-01 08:00:00'),
(1, 2, '2025-12-02 09:30:00'),
(1, 3, '2025-12-03 14:20:00'),
(1, 4, '2025-12-04 16:45:00'),
(2, 1, '2025-12-05 10:00:00'),
(2, 4, '2025-12-05 11:30:00');

-- 6. 初始化用户账户
INSERT IGNORE INTO user_accounts (user_id, balance, total_recharge) VALUES
(1, 1000.00, 1000.00),
(2, 800.00, 800.00),
(3, 5000.00, 5000.00),
(4, 10000.00, 10000.00),
(123, 0.00, 0.00);

-- 7. 插入充值记录示例
INSERT INTO recharge_records (user_id, order_no, amount, status, recharge_time) VALUES
(1, 'RECH202512011230001', 1000.00, 'success', '2025-12-01 12:30:00'),
(2, 'RECH202512011445002', 800.00, 'success', '2025-12-01 14:45:00'),
(3, 'RECH202512021030003', 5000.00, 'success', '2025-12-02 10:30:00'),
(4, 'RECH202512021530004', 10000.00, 'success', '2025-12-02 15:30:00');

-- ============================================
-- 创建视图（用于SystemController）
-- ============================================

-- 订单统计视图
CREATE OR REPLACE VIEW order_statistics AS
SELECT
    DATE(o.order_date) as order_date,
    COUNT(*) as total_orders,
    SUM(CASE WHEN o.status = '已支付' THEN 1 ELSE 0 END) as paid_orders,
    SUM(CASE WHEN o.status = '已取消' THEN 1 ELSE 0 END) as cancelled_orders,
    SUM(CASE WHEN o.status = '未支付' THEN 1 ELSE 0 END) as unpaid_orders,
    SUM(COALESCE(o.total_amount, 0)) as total_revenue,
    COUNT(DISTINCT o.user_id) as unique_users
FROM orders o
GROUP BY DATE(o.order_date);

-- 航班上座率统计视图
CREATE OR REPLACE VIEW flight_occupancy_stats AS
SELECT
    f.ID as flight_id,
    f.flight_number,
    f.origin,
    f.destination,
    f.departure_time,
    (f.economy_seats + f.business_seats + f.first_class_seats) as total_seats,
    COUNT(o.ID) as booked_seats,
    ROUND(COUNT(o.ID) * 100.0 / (f.economy_seats + f.business_seats + f.first_class_seats), 2) as occupancy_rate,
    SUM(CASE WHEN o.seat_type = 0 THEN 1 ELSE 0 END) as economy_booked,
    SUM(CASE WHEN o.seat_type = 1 THEN 1 ELSE 0 END) as business_booked,
    SUM(CASE WHEN o.seat_type = 2 THEN 1 ELSE 0 END) as first_class_booked
FROM flights f
LEFT JOIN orders o ON f.ID = o.flight_id AND o.status IN ('已支付', '已完成')
GROUP BY f.ID, f.flight_number, f.origin, f.destination, f.departure_time;

-- ============================================
-- 实用工具查询
-- ============================================
SELECT '数据库初始化完成' as message;
SELECT '数据库表清单:' as info;
SHOW TABLES;

SELECT '用户数据:' as info;
SELECT U_ID, username, telephone FROM users;

SELECT '订单数据:' as info;
SELECT ID, order_id, user_id, status, total_amount, paid_amount FROM orders;

SELECT '用户账户:' as info;
SELECT user_id, balance FROM user_accounts;
