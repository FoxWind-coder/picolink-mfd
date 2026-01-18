import smbus
import time

BUS_NUM = 6
ADDR = 0x3c

bus = smbus.SMBus(BUS_NUM)

def send_command(cmd):
    bus.write_byte_data(ADDR, 0x00, cmd)

def send_data_block(block):
    # SSD1306 может принимать данные пачками
    bus.write_i2c_block_data(ADDR, 0x40, block)

try:
    print(f"Инициализируем OLED на 0x{ADDR:02x}...")
    
    # Инициализация SSD1306 128x64
    cmds = [
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 
        0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 
        0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 
        0x40, 0xA4, 0xA6, 0xAF
    ]
    for c in cmds:
        send_command(c)
        time.sleep(0.01)

    print("Очистка и заливка паттерном...")
    # Рисуем шахматку или полоски
    for page in range(8):
        # Устанавливаем адрес страницы и колонки
        send_command(0xB0 + page) # Set Page Address
        send_command(0x00)        # Set Lower Column Address
        send_command(0x10)        # Set Higher Column Address
        
        # Отправляем 128 байт данных (белая полоса)
        line = [0xFF] * 32 # Кусочками по 32 байта, чтобы не перегружать USB
        for _ in range(4):
            send_data_block(line)
            
    print("Готово! Экран должен светиться.")

except Exception as e:
    print(f"Ошибка: {e}")
