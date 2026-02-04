import spidev
import time
import os
import sys
import struct
from luma.oled.device import ssd1306
from luma.core.interface.serial import i2c
from luma.core.render import canvas

class OLEDDisplay:
    def __init__(self, port=3, address=0x3c):
        try:
            self.serial = i2c(port=port, address=address)
            self.device = ssd1306(self.serial)
            print(f"[*] OLED display found on i2c{port} (0x{address:02x})")
        except Exception as e:
            print(f"[!] OLED Error: {e}")
            self.device = None

    def update(self, percent, speed_text, block):
        if not self.device: return
        with canvas(self.device) as draw:
            draw.rectangle(self.device.bounding_box, outline="white", fill="black")
            draw.text((10, 5), "PicoLink SD Test", fill="white")
            draw.text((10, 20), f"Progress: {percent:3.1f}%", fill="white")
            draw.text((10, 35), f"Speed: {speed_text}", fill="white")
            draw.text((10, 50), f"Block: {block}", fill="white")

class SimpleSDSPI:
    def __init__(self, bus=0, device=0):
        self.spi = spidev.SpiDev()
        self.spi.open(bus, device)
        self.spi.max_speed_hz = 100000 
        self.spi.mode = 0b00

    def send_cmd(self, cmd, arg, crc):
        self.spi.xfer2([0xFF])
        packet = [0x40 | cmd, (arg >> 24) & 0xFF, (arg >> 16) & 0xFF, (arg >> 8) & 0xFF, arg & 0xFF, crc]
        self.spi.xfer2(packet)
        for _ in range(15):
            res = self.spi.xfer2([0xFF])[0]
            if not (res & 0x80): return res
        return 0xFF

    def init_card(self):
        print("[*] Initializing SD card...")
        self.spi.xfer2([0xFF] * 15)
        if self.send_cmd(0, 0, 0x95) != 0x01: return False
        self.send_cmd(8, 0x1AA, 0x87)
        start = time.time()
        while time.time() - start < 3.0:
            self.send_cmd(55, 0, 0xFF)
            if self.send_cmd(41, 0x40000000, 0xFF) == 0x00:
                print("[*] Card status: Ready!")
                self.spi.max_speed_hz = 100000 
                return True
            time.sleep(0.05)
        return False

    def read_block(self, block_address):
        # Clear bus
        self.spi.xfer2([0xFF])
        
        # CMD17: Read Single Block
        if self.send_cmd(17, block_address, 0xFF) != 0x00:
            return None

        # Wait for data token 0xFE (read byte-by-byte to avoid overshoot)
        found = False
        for _ in range(2000):
            byte = self.spi.xfer2([0xFF])[0]
            if byte == 0xFE:
                found = True
                break
            if byte != 0xFF: return None # Error condition
        
        if not found: return None

        # Read data in 32-byte chunks
        # Crucial for preventing Pico buffer overflow
        full_data = []
        for _ in range(512 // 32):
            chunk = self.spi.xfer2([0xFF] * 32)
            full_data.extend(chunk)
            # Micro-delay for USB stability
            time.sleep(0.001)

        # Read CRC bytes
        self.spi.xfer2([0xFF] * 2)
        
        return full_data

    def analyze_fat32(self):
        print("\n--- Structure Analysis ---")
        sector0 = self.read_block(0)
        sector1 = self.read_block(1)
        
        if sector0:
            # FIX HERE
            with open("sd_spi_head.bin", "wb") as f:
                f.write(bytearray(sector0))
                if sector1:
                    f.write(bytearray(sector1))
            print("[*] Dump of first 2 blocks (1024 bytes) saved.")

            # Check signature (bytes 510-511)
            if sector0[510] == 0x55 and sector0[511] == 0xAA:
                print("[+] Signature 55 AA found!")
            else:
                print(f"[!] Signature error: {sector0[510]:02x} {sector0[511]:02x}")

def run():
    oled = OLEDDisplay()
    sd = SimpleSDSPI()
    if not sd.init_card(): return
    sd.analyze_fat32()

if __name__ == "__main__":
    run()