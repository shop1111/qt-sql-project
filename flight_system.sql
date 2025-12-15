-- 创建数据库
CREATE DATABASE IF NOT EXISTS flight_system CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE flight_system;

-- 1. 用户表 (对应 表1.png) - ✅ 保持不变
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

-- 2. 航班表 (对应 表2.png) - ✅ 保持不变
CREATE TABLE IF NOT EXISTS flights (
    ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    flight_number VARCHAR(10) NOT NULL,           -- 班次号
    origin VARCHAR(50) NOT NULL,                  -- 出发地
    destination VARCHAR(50) NOT NULL,             -- 目的地
    departure_time DATETIME NOT NULL,             -- 起飞时间
    landing_time DATETIME NOT NULL,               -- 落地时间
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

-- 3. 订单表 (对应 表3.png) - 需要小修改来支持 SeatController
CREATE TABLE IF NOT EXISTS orders (
    ID INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    order_date DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,  -- 预定日期
    user_id INT NOT NULL,                         -- 用户ID
    flight_id INT NOT NULL,                       -- 航班ID
    seat_type INT NOT NULL,                       -- 座位类型 (0:经济舱, 1:商务舱, 2:头等舱)
    seat_number VARCHAR(50) NOT NULL,             -- 座位号
    status VARCHAR(20) DEFAULT '未支付' COMMENT '订单状态: 未支付, 已支付, 已取消, 已完成, 已退款',
    -- 新增字段：支持 SeatController 的锁定功能
    locked_at DATETIME NULL COMMENT '锁定时间（用于座位锁定）',
    locked_by INT NULL COMMENT '锁定用户ID',
    -- 可选的其他字段（代码中已添加但未使用）
    total_amount DECIMAL(10, 2) NULL COMMENT '订单总金额',
    paid_amount DECIMAL(10, 2) DEFAULT 0.00 COMMENT '已支付金额',
    payment_id INT NULL COMMENT '关联的支付ID',
    cancelled_at DATETIME NULL COMMENT '取消时间',

    FOREIGN KEY (user_id) REFERENCES users(U_ID) ON DELETE CASCADE,
    FOREIGN KEY (flight_id) REFERENCES flights(ID) ON DELETE CASCADE,
    -- 修改唯一约束：允许同一座位在不同状态的订单中存在
    INDEX idx_flight_seat (flight_id, seat_number),
    INDEX idx_status (status),
    INDEX idx_user (user_id)
);

-- 4. 城市代码映射表
CREATE TABLE IF NOT EXISTS city_codes (
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    city_name VARCHAR(50) NOT NULL COMMENT '城市中文名',
    city_code VARCHAR(3) NOT NULL COMMENT 'IATA三字码',
    pinyin VARCHAR(50) NULL COMMENT '城市拼音，方便后续做模糊搜索',
    UNIQUE KEY unique_code (city_code)
) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 在 orders 表添加更多payment相关字段
ALTER TABLE orders
ADD COLUMN payment_time DATETIME NULL COMMENT '支付时间',
ADD COLUMN payment_method VARCHAR(20) NULL COMMENT '支付方式: wechat-微信, alipay-支付宝, bank-银行卡';


-- ============================================
-- 数据初始化
-- ============================================

-- 插入示例用户数据
INSERT INTO users (username, true_name, nickname, telephone, password, P_ID, email, photo) VALUES
('zhangsan', '张三', '法外狂徒', '13800138001', 'pass123', '110101199001011234', 'zs@example.com', NULL),
('lisi', '李四', '朝阳群众', '13900139002', 'pass456', '110101199202025678', 'ls@example.com', NULL),
('wangwu', '王五', '隔壁老王', '13700137003', 'pass789', '110101198803039012', 'ww@example.com', NULL),
('admin', '管理员', '系统管理员', '13600136000', 'admin123', '110101199501011111', 'admin@example.com', NULL);

-- 插入城市代码数据
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

-- 插入航班数据
INSERT INTO flights (flight_number, origin, destination, departure_time, landing_time, airline, aircraft_model, economy_seats, economy_price, business_seats, business_price, first_class_seats, first_class_price) VALUES
('CA1001', '北京', '上海', '2025-12-01 08:00:00', '2025-12-01 10:15:00', '中国国航', 'Boeing 737', 150, 800, 20, 2000, 8, 4500),
('MU2567', '上海', '东京', '2025-12-02 14:30:00', '2025-12-02 18:30:00', '东方航空', 'Airbus A330', 200, 2500, 30, 5000, 10, 12000),
('CZ3888', '广州', '纽约', '2025-12-05 23:00:00', '2025-12-06 14:00:00', '南方航空', 'Boeing 787', 220, 6000, 40, 15000, 12, 35000),
('CA1502', '北京', '广州', '2025-12-10 10:00:00', '2025-12-10 13:30:00', '中国国航', 'Airbus A320', 180, 1200, 25, 3000, 6, 6000);

-- 插入订单数据
INSERT INTO orders (user_id, flight_id, seat_type, seat_number, status, order_date) VALUES
(1, 1, 0, '12A', '已支付', '2025-11-26 10:00:00'),
(2, 1, 0, '12B', '已支付', '2025-11-26 10:05:00'),
(3, 2, 1, '01F', '已取消', '2025-11-26 11:00:00'),
(1, 3, 2, '01A', '未支付', '2025-11-27 09:00:00');

-- 浏览历史表
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

-- 插入新的测试数据，确保每个flight_id都能在flights表中找到
INSERT INTO browse_history (user_id, flight_id, browse_time) VALUES
-- 用户1的浏览记录
(1, 1, '2025-12-01 08:00:00'),  -- flight_id=1 对应 CA1001
(1, 2, '2025-12-02 09:30:00'),  -- flight_id=2 对应 MU2567
(1, 3, '2025-12-03 14:20:00'),  -- flight_id=3 对应 CZ3888
(1, 4, '2025-12-04 16:45:00'),  -- flight_id=4 对应 CA1502
-- 用户2的浏览记录
(2, 1, '2025-12-05 10:00:00'),
(2, 4, '2025-12-05 11:30:00');

-- 统计视图（用于SystemController）
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

-- ============================================
-- 实用工具查询
-- ============================================
SELECT '数据库初始化完成' as message;
SHOW TABLES;
