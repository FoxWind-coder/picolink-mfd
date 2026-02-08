import smbus2
import time

I2C_BUS = 3
ADDR = 0x40

# Регистра INA219
REG_CONFIG = 0x00
REG_SHUNTV = 0x01
REG_BUSV   = 0x02
REG_POWER  = 0x03
REG_CURRENT = 0x04
REG_CALIB  = 0x05

bus = smbus2.SMBus(I2C_BUS)

def init_ina219():
    # Настройка: 32V range, 12-bit ADC, Shunt and Bus continuous
    # 0x399F - стандартный конфиг
    bus.write_i2c_block_data(ADDR, REG_CONFIG, [0x39, 0x9F])
    # Калибровка для шунта 0.1 Ом (для диапазона 2А)
    # Значение 4096 - типичное для популярных модулей
    bus.write_i2c_block_data(ADDR, REG_CALIB, [0x10, 0x00])

print("Initializing INA219...")
init_ina219()

try:
    while True:
        # Читаем Bus Voltage (мВ)
        data = bus.read_i2c_block_data(ADDR, REG_BUSV, 2)
        raw_bus = (data[0] << 8) | data[1]
        voltage = (raw_bus >> 3) * 4 
        
        # Читаем Current (мА) - требует калибровки выше
        data_curr = bus.read_i2c_block_data(ADDR, REG_CURRENT, 2)
        current = (data_curr[0] << 8) | data_curr[1]
        # Если значение отрицательное (доп. код)
        if current > 32767: current -= 65536
        
        print(f"V: {voltage} mV | I: {current} mA", end='\r')
        time.sleep(0.1)
except KeyboardInterrupt:
    print("\nStop.")