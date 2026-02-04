import smbus
import time
import math
import numpy as np

I2C_BUS = 3
I2C_ADDR = 0x3C
WIDTH = 128
HEIGHT = 64

class FastOLED:
    def __init__(self, bus_num, addr):
        self.bus = smbus.SMBus(bus_num)
        self.addr = addr
        self._init_display()

    def _command(self, *cmds):
        for cmd in cmds:
            self.bus.write_byte_data(self.addr, 0x00, cmd)

    def _init_display(self):
        # Initialize in Horizontal Addressing Mode for fastest data throughput
        init_cmds = [
            0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
            0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
            0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
        ]
        for cmd in init_cmds:
            self.bus.write_byte_data(self.addr, 0x00, cmd)

    def update(self, buffer):
        # Reset pointers to the start of the screen
        self._command(0x21, 0, 127) # Column addr
        self._command(0x22, 0, 7)   # Page addr
        
        # Send 1024 bytes in 32-byte chunks
        # Required due to Linux i2c-dev buffer limits
        for i in range(0, 1024, 32):
            self.bus.write_i2c_block_data(self.addr, 0x40, buffer[i:i+32])

def generate_wave_frame(t):
    """ Generates a frame with a traveling sine wave """
    # Buffer: 8 pages x 128 bytes
    buffer = np.zeros(1024, dtype=np.uint8)
    
    for x in range(WIDTH):
        # Primary sine wave calculation (0 to 63)
        y = int(32 + 28 * math.sin(x * 0.1 + t))
        
        # Map Y to page (0-7) and bit (0-7)
        page = y // 8
        bit = y % 8
        
        # Write pixel to buffer: index = page * 128 + x
        buffer[page * 128 + x] |= (1 << bit)
        
        # Secondary harmonic for visual complexity
        y2 = int(32 + 15 * math.cos(x * 0.05 - t * 2))
        buffer[(y2 // 8) * 128 + x] |= (1 << (y2 % 8))

    return buffer.tolist()

def run_oscillator():
    oled = FastOLED(I2C_BUS, I2C_ADDR)
    print("[*] Data stream started. Checking wave smoothness...")
    
    

    start_time = time.time()
    frames = 0
    
    try:
        while True:
            t = time.time() * 5 # Animation speed
            frame = generate_wave_frame(t)
            oled.update(frame)
            
            frames += 1
            if frames % 100 == 0:
                elapsed = time.time() - start_time
                print(f"[*] Performance: {frames / elapsed:.2f} FPS")
                
    except KeyboardInterrupt:
        print(f"\n[!] Stopped. Average FPS: {frames / (time.time() - start_time):.2f}")

if __name__ == "__main__":
    run_oscillator()