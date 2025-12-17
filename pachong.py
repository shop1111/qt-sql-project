import requests
import json
import pymysql
import time
from datetime import datetime, timedelta

# ================= 配置区域 =================
# 数据库配置 (请修改为您的真实配置)
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

# 城市代码映射 (携程API需要城市三字码，这里列举了常见城市)
# 您可以根据需要补充更多：BJS(北京), SHA(上海), CAN(广州), SZX(深圳), CTU(成都) ...
CITY_CODES = ['BJS', 'SHA', 'CAN', 'SZX', 'CTU'] 

# ===========================================

def get_db_connection():
    return pymysql.connect(**DB_CONFIG)

def fetch_flights(dept_code, arr_code):
    """从携程 API 获取航班数据"""
    print(f"正在抓取: {dept_code} -> {arr_code} ...")
    
    payload = {
        "departureCityCode": dept_code,
        "arriveCityCode": arr_code,
        "pageNo": 1
    }
    
    headers = {
        "Content-Type": "application/json",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"
    }

    try:
        response = requests.post(URL, json=payload, headers=headers)
        data = response.json()
        
        if not data.get('scheduleVOList'):
            print("  - 无航班数据")
            return []
            
        return data
    except Exception as e:
        print(f"  - 抓取失败: {e}")
        return []


import random # 需要在文件头部引入
def save_to_db(connection, api_data):
    """将数据保存到数据库 flights 表 (适配 flight_system.sql)"""
    cursor = connection.cursor()
    # 提取城市中文名
    dept_city_name = api_data.get('departureCityName', '未知')
    arr_city_name = api_data.get('arriveCityName', '未知')
    flight_list = api_data.get('scheduleVOList', [])

    count = 0
    for item in flight_list:
        try:
            flight_no = item.get('flightNo')
            airline = item.get('airlineCompanyName')
            
            # 构造时间
            flight_date = item.get('departDate') # YYYY-MM-DD
            dep_time_str = f"{flight_date} {item.get('departTime')}:00"
            arr_time_str = f"{flight_date} {item.get('arriveTime')}:00"
            
            # 价格处理
            # 携程接口一般返回的是最低价，我们把它当做经济舱价格
            eco_price = item.get('price', 800) 
            if eco_price is None: eco_price = 800
            
            # === 生成推断/默认数据以满足 SQL 约束 ===
            # 1. 商务舱价格：假设是经济舱的 2.5 倍
            bus_price = int(eco_price * 2.5)
            # 2. 头等舱价格：假设是经济舱的 4 倍
            first_price = int(eco_price * 4)
            # 3. 座位数：随机生成一个合理的数字
            eco_seats = random.randint(120, 180)
            bus_seats = random.randint(10, 30)
            first_seats = random.randint(4, 8)
            # 4. 机型
            aircraft = "Boeing 737" # 默认值

            # SQL 插入语句 (完全匹配 flight_system.sql)
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
            
            # 执行插入
            cursor.execute(sql, (
                flight_no,
                dept_city_name, # origin
                arr_city_name,  # destination
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
            # 打印详细错误方便调试
            print(f"  - 插入失败 [{flight_no}]: {e}")

    connection.commit()
    print(f"  - 成功入库 {count} 条航班")

def main():
    conn = get_db_connection()
    try:
        # 双重循环抓取所有城市对
        for dep in CITY_CODES:
            for arr in CITY_CODES:
                if dep == arr:
                    continue
                
                # 抓取
                data = fetch_flights(dep, arr)
                if data:
                    save_to_db(conn, data)
                
                # 礼貌延时，防止被封 IP
                time.sleep(2)
                
    finally:
        conn.close()
        print("所有抓取任务完成。")

if __name__ == "__main__":
    main()