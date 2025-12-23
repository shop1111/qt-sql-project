import requests
import json
import pymysql
import time
import random  # 必须引入
from datetime import datetime, timedelta

# ================= 配置区域 =================
# 数据库配置
DB_CONFIG = {
    'host': '127.0.0.1',
    'port': 3306,
    'user': 'root',
    'password': '123456',
    'database': 'flight_system',
    'charset': 'utf8mb4'
}

# 携程接口地址
URL = 'https://flights.ctrip.com/schedule/getScheduleByCityPair'

# 扩展后的热门城市代码
CITY_CODES = [
    'BJS',  # 北京
    'SHA',  # 上海 (虹桥)
    'PVG',  # 上海 (浦东)
    'ZUH',  # 珠海
    'CAN',  # 广州
    'SZX',  # 深圳
    'CTU',  # 成都
    'HGH',  # 杭州
    'CKG',  # 重庆
    'XIY',  # 西安
    'WUH',  # 武汉
    'NKG',  # 南京
    'CSX',  # 长沙
]

# 机型备选列表
AIRCRAFT_OPTIONS = [
    "Boeing 737", "Boeing 747", "Boeing 777", "Boeing 787",
    "Airbus A320", "Airbus A330", "Airbus A350", "Comac C919"
]

def get_db_connection():
    return pymysql.connect(**DB_CONFIG)

def fetch_flights(dept_code, arr_code, date_str):
    """
    从携程 API 获取指定日期的航班数据
    :param date_str: 格式 'YYYY-MM-DD'
    """
    print(f"正在抓取: {date_str} [{dept_code} -> {arr_code}] ...")
    
    payload = {
        "departureCityCode": dept_code,
        "arriveCityCode": arr_code,
        "departDate": date_str,  # 关键修改：加入日期参数
        "pageNo": 1
    }
    
    headers = {
        "Content-Type": "application/json",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"
    }

    try:
        response = requests.post(URL, json=payload, headers=headers, timeout=10)
        data = response.json()
        
        # 检查是否有数据
        schedule_list = data.get('scheduleVOList')
        if not schedule_list:
            print("  - 该日期无航班数据")
            return []
            
        return data
    except Exception as e:
        print(f"  - 抓取失败: {e}")
        return []

def save_to_db(connection, api_data):
    """将数据保存到数据库"""
    cursor = connection.cursor()
    
    dept_city_name = api_data.get('departureCityName', '未知')
    arr_city_name = api_data.get('arriveCityName', '未知')
    flight_list = api_data.get('scheduleVOList', [])

    # === 修改开始：限制每条航线的入库数量 ===
    MAX_FLIGHTS_PER_ROUTE = 5
    
    if len(flight_list) > MAX_FLIGHTS_PER_ROUTE:
        # 如果获取到的航班数多于限制，则随机抽出 x 条
        flight_list = random.sample(flight_list, MAX_FLIGHTS_PER_ROUTE)

    count = 0
    for item in flight_list:
        try:
            flight_no = item.get('flightNo')
            airline = item.get('airlineCompanyName')
            
            # --- 时间处理 ---
            # API返回的 dates 可能是 '2023-10-01' 格式
            flight_date_raw = item.get('departDate') 
            
            # 组合成完整的 datetime 字符串
            dep_time_str = f"{flight_date_raw} {item.get('departTime')}:00"
            arr_time_str = f"{flight_date_raw} {item.get('arriveTime')}:00"
            
            # 如果到达时间比出发时间小，说明跨天了(第二天到达)，简单处理可以加一天，或者暂不处理
            # 这里保持原逻辑直接拼接
            
            # --- 价格处理 (你的需求) ---
            # 1. 尝试获取价格
            eco_price = item.get('price') 
            
            # 2. 如果价格是 None 或 0 或 空，则随机生成
            if not eco_price: 
                eco_price = random.randint(600, 1200)
            else:
                # 确保是 int 类型
                eco_price = int(eco_price)

            # --- 衍生数据生成 ---
            bus_price = int(eco_price * 2.5)
            first_price = int(eco_price * 4)
            
            eco_seats = random.randint(120, 180)
            bus_seats = random.randint(10, 30)
            first_seats = random.randint(4, 8)
            
            # --- 机型处理 (你的需求) ---
            # 优先用 API 的，如果没有则随机
            aircraft = item.get('aircraft')
            if not aircraft:
                aircraft = random.choice(AIRCRAFT_OPTIONS)

            # --- SQL 插入 ---
            sql = """
                INSERT INTO flights 
                (flight_number, origin, destination, departure_time, landing_time, 
                 airline, aircraft_model, 
                 economy_seats, economy_price, 
                 business_seats, business_price, 
                 first_class_seats, first_class_price)
                VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                ON DUPLICATE KEY UPDATE 
                economy_price = VALUES(economy_price),
                departure_time = VALUES(departure_time);
            """
            
            cursor.execute(sql, (
                flight_no,
                dept_city_name,
                arr_city_name,
                dep_time_str,
                arr_time_str,
                airline,
                aircraft,
                eco_seats,
                eco_price,
                bus_seats,
                bus_price,
                first_seats,
                first_price
            ))
            count += 1
            
        except Exception as e:
            # print(f"  - 插入单条失败: {e}") # 调试时可打开
            continue

    connection.commit()
    print(f"  - 成功入库 {count} 条")

def main():
    conn = get_db_connection()
    
    # === 关键修改：设置要爬取的天数 ===
    # 从明天开始，爬取未来 5 天的数据
    # 这样能覆盖 早上、中午、晚上 各个时段的航班（因为是全量抓取每一天的）
    days_to_crawl = 5 
    start_date = datetime.now() + timedelta(days=1) # 从明天开始
    
    try:
        # 第一层循环：日期
        for i in range(days_to_crawl):
            current_date = start_date + timedelta(days=i)
            date_str = current_date.strftime('%Y-%m-%d')
            
            print(f"\n====== 开始抓取日期: {date_str} ======")

            # 第二层循环：出发城市
            for dep in CITY_CODES:
                # 第三层循环：到达城市
                for arr in CITY_CODES:
                    if dep == arr:
                        continue
                    
                    # 某些航线可能不存在（比如同一个城市的不同机场），这里可以做个简单随机跳过以节省时间
                    # if random.random() < 0.3: continue 

                    data = fetch_flights(dep, arr, date_str)
                    if data:
                        save_to_db(conn, data)
                    
                    # 延时，防止请求过快
                    time.sleep(random.uniform(1.0, 2.0))
                    
    finally:
        conn.close()
        print("所有抓取任务完成。")

if __name__ == "__main__":
    main()