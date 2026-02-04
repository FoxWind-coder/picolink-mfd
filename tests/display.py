import spidev
import os
import time
import random

# Pin configurations
GPIO_BASE = 512
PIN_DC = str(GPIO_BASE + 6)
PIN_RESET = str(GPIO_BASE + 5)
LED_PATH = "/sys/class/leds/picolink_led_7/brightness"

def gpio_init():
    """Export and configure GPIO directions"""
    for pin in [PIN_DC, PIN_RESET]:
        # Export if not already exported
        if not os.path.exists(f"/sys/class/gpio/gpio{pin}"):
            try:
                with open("/sys/class/gpio/export", "w") as f:
                    f.write(pin)
            except OSError:
                pass # Might be exported by another process
        
        # Set direction to 'out'
        time.sleep(0.1) # Wait for system to create files
        with open(f"/sys/class/gpio/gpio{pin}/direction", "w") as f:
            f.write("out")

def gpio_write(pin, value):
    with open(f"/sys/class/gpio/gpio{pin}/value", "w") as f:
        f.write(str(value))

def set_backlight(value):
    try:
        with open(LED_PATH, "w") as f:
            f.write(str(int(value)))
    except:
        pass

# Initialize hardware
gpio_init()

# Display Hard Reset
gpio_write(PIN_RESET, 1); time.sleep(0.01)
gpio_write(PIN_RESET, 0); time.sleep(0.05)
gpio_write(PIN_RESET, 1); time.sleep(0.05)

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 12000000
spi.mode = 0b11

def write_cmd(cmd):
    gpio_write(PIN_DC, 0)
    spi.xfer2([cmd])

def write_data(data):
    gpio_write(PIN_DC, 1)
    spi.xfer3(data)

def draw_block(x, y, w, h, color_r565):
    write_cmd(0x2A) # Column Address Set
    write_data([x >> 8, x & 0xFF, (x + w - 1) >> 8, (x + w - 1) & 0xFF])
    write_cmd(0x2B) # Row Address Set
    write_data([y >> 8, y & 0xFF, (y + h - 1) >> 8, (y + h - 1) & 0xFF])
    write_cmd(0x2C) # Memory Write
    gpio_write(PIN_DC, 1)
    pixel_data = list(color_r565 * (w * h))
    spi.xfer3(pixel_data)

try:
    print("[*] Initializing display...")
    set_backlight(128)
    
    # ST7789 Minimal Initialization
    write_cmd(0x01); time.sleep(0.1)  # Soft Reset
    write_cmd(0x11); time.sleep(0.1)  # Sleep Out
    write_cmd(0x3A); write_data([0x55]) # 16-bit color
    write_cmd(0x21)                   # Inversion On
    write_cmd(0x29)                   # Display On

    print("[*] Clearing screen...")
    write_cmd(0x2A); write_data([0, 0, 0, 239])
    write_cmd(0x2B); write_data([0, 0, 0, 239])
    write_cmd(0x2C)
    gpio_write(PIN_DC, 1)
    for _ in range(240):
        spi.xfer3([0] * (240 * 2))

    x, y = 120, 120
    dx = random.choice([-4, -3, 3, 4])
    dy = random.choice([-4, -3, 3, 4])
    
    color_dot = [0x07, 0xE0] # Green
    brightness = 128

    print("[*] Starting animation loop.")
    while True:
        hit = False
        draw_block(int(x), int(y), 3, 3, [0, 0]) # Erase
        
        x += dx
        y += dy
        
        # Boundary check X
        if x <= 0 or x >= 237:
            dx = -dx + random.uniform(-1, 1)
            dx = max(min(dx, 6), -6)
            if abs(dx) < 2: dx = 2 if dx > 0 else -2
            hit = True
            
        # Boundary check Y
        if y <= 0 or y >= 237:
            dy = -dy + random.uniform(-1, 1)
            dy = max(min(dy, 6), -6)
            if abs(dy) < 2: dy = 2 if dy > 0 else -2
            hit = True
            
        if hit:
            brightness = 255
            color_dot = [random.getrandbits(8), random.getrandbits(8)]
        else:
            if brightness > 128:
                brightness -= 10
        
        set_backlight(brightness)
        x = max(min(x, 237), 0)
        y = max(min(y, 237), 0)
        
        draw_block(int(x), int(y), 3, 3, color_dot)
        time.sleep(0.015)

except KeyboardInterrupt:
    print("\n[*] Shutting down.")
finally:
    set_backlight(255)
    spi.close()