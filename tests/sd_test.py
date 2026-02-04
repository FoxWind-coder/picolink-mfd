import spidev
import time
import sys
import struct
import os
from luma.oled.device import ssd1306
from luma.core.interface.serial import i2c
from luma.core.render import canvas

class OLEDDisplay:
    def __init__(self, port=3, address=0x3c):
        try:
            self.serial = i2c(port=port, address=address)
            self.device = ssd1306(self.serial)
            print(f"[*] OLED found on i2c{port}")
        except:
            self.device = None

    def update(self, title, percent, speed_text, info):
        if not self.device: return
        with canvas(self.device) as draw:
            draw.text((5, 2), title, fill="white")
            draw.rectangle((5, 18, 120, 28), outline="white")
            draw.rectangle((5, 18, 5 + (percent * 1.15), 28), fill="white")
            draw.text((5, 32), f"Speed: {speed_text}", fill="white")
            draw.text((5, 48), info, fill="white")

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
        # Wait for R1 response
        for _ in range(64):
            res = self.spi.xfer2([0xFF])[0]
            if not (res & 0x80): return res
        return 0xFF

    def init_card(self):
        print("[*] Attempting hard reset of SD card...")
        self.spi.xfer2([0xFF] * 20)
        
        if self.send_cmd(0, 0, 0x95) != 0x01: return False
        self.send_cmd(8, 0x1AA, 0x87)
        
        print("[*] Waiting for card to be ready...", end="", flush=True)
        start = time.time()
        while time.time() - start < 3.0:
            self.send_cmd(55, 0, 0xFF)
            if self.send_cmd(41, 0x40000000, 0xFF) == 0x00:
                print(" Ready!")
                self.spi.max_speed_hz = 4000000
                return True
            print(".", end="", flush=True)
            time.sleep(0.1)
        return False

    def read_block(self, block_address, chunk_size=32):
        self.spi.xfer2([0xFF])
        if self.send_cmd(17, block_address, 0xFF) != 0x00: return None
        
        # Wait for data token (0xFE)
        for _ in range(5000):
            if self.spi.xfer2([0xFF])[0] == 0xFE: break
        else: return None
        
        data = []
        for _ in range(512 // chunk_size):
            data.extend(self.spi.xfer2([0xFF] * chunk_size))
            
        self.spi.xfer2([0xFF] * 2) # Discard CRC
        return data

def run():
    oled = OLEDDisplay()
    sd = SimpleSDSPI()
    if not sd.init_card(): return

    # --- Phase 1: Reference (Capture MBR) ---
    print("\n[*] Phase 1: Obtaining reference (32 byte chunk)...")
    reference = sd.read_block(0, 32)
    if not reference:
        print("[!] Reference read error")
        return

    # --- Phase 2: Stress test with error logging ---
    print("\n[*] Phase 2: Stress test and threshold search...")
    print(f"{'Chunk':<8} | {'Speed':<12} | {'Status'}")
    print("-" * 45)

    for cs in [32, 40, 60, 120, 180]:
        start_t = time.time()
        test_data = sd.read_block(0, cs)
        
        if test_data == reference:
            speed = 512 / (time.time() - start_t) / 1024
            print(f"{cs:<8} | {speed:>8.2f} KB/s | OK")
            oled.update("Stress Test", (cs/512)*100, f"{speed:.1f}KB/s", f"Chunk: {cs}")
        else:
            print(f"{cs:<8} | {'-----':>8} | !!! FAILED !!!")
            
            # Save corrupted dump
            fail_file = f"failed_chunk_{cs}.bin"
            with open(fail_file, "wb") as f:
                f.write(bytearray(test_data) if test_data else b"TIMEOUT")
            
            # Debugging comparison
            if test_data:
                for idx, (ref_b, tst_b) in enumerate(zip(reference, test_data)):
                    if ref_b != tst_b:
                        print(f"    [!] Mismatch at byte {idx}:")
                        print(f"    Expected: {reference[idx:idx+4]}")
                        print(f"    Received: {test_data[idx:idx+4]}")
                        break
            
            print(f"    [i] Erroneous block saved to {fail_file}")
            oled.update("STRESS FAILED", 100, "ERR SAVED", f"Chunk {cs}")
            break

if __name__ == "__main__":
    run()