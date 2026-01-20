import smbus2
import time

# Configuration
I2C_BUS = 6
ADDR = 0x40
REG_BUS_VOLTAGE = 0x02

bus = smbus2.SMBus(I2C_BUS)

def read_voltage():
    # Read 2 bytes from the Bus Voltage register
    data = bus.read_i2c_block_data(ADDR, REG_BUS_VOLTAGE, 2)
    
    # Combine bytes (Big Endian)
    raw_val = (data[0] << 8) | data[1]
    
    # Shift right by 3 bits and multiply by LSB (4 mV) 
    # specific to INA219 register mapping
    voltage_v = (raw_val >> 3) * 0.004
    return voltage_v

try:
    while True:
        v = read_voltage()
        print(f"Bus Voltage: {v:.3f} V")
        time.sleep(1)
except KeyboardInterrupt:
    bus.close()