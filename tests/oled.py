import smbus
import time

BUS_NUM = 3
ADDR = 0x3c

bus = smbus.SMBus(BUS_NUM)

def send_command(cmd):
    bus.write_byte_data(ADDR, 0x00, cmd)

def send_data_block(block):
    # SSD1306 can accept data in blocks
    bus.write_i2c_block_data(ADDR, 0x40, block)

try:
    print(f"Initializing OLED at 0x{ADDR:02x}...")
    
    # Initialize SSD1306 128x64
    cmds = [
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 
        0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 
        0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 
        0x40, 0xA4, 0xA6, 0xAF
    ]
    for c in cmds:
        send_command(c)
        time.sleep(0.01)

    print("Clearing and filling with pattern...")
    # Drawing a pattern (checkerboard or stripes)
    for page in range(8):
        # Set page and column addresses
        send_command(0xB0 + page)
        send_command(0x00)
        send_command(0x10)
        
        # Send 128 bytes of data (white stripe)
        # Using 32-byte chunks to prevent I2C/USB buffer overflow
        line = [0xFF] * 32 
        for _ in range(4):
            send_data_block(line)
            
    print("Done! The screen should be illuminated.")

except Exception as e:
    print(f"Error: {e}")