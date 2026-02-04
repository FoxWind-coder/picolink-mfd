import spidev
import time
import os
import sys
import struct

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
        res_chunk = self.spi.xfer2([0xFF] * 16)
        for response in res_chunk:
            if not (response & 0x80): return response
        return 0xFF

    def init_card(self):
        print("[*] Starting SD card initialization (100 kHz)...")
        self.spi.xfer2([0xFF] * 10)
        
        # CMD0: Reset card
        if self.send_cmd(0, 0, 0x95) != 0x01: 
            return False
            
        # CMD8: Check voltage (for SDHC support)
        self.send_cmd(8, 0x1AA, 0x87)
        
        start = time.time()
        while time.time() - start < 2.0:
            self.send_cmd(55, 0, 0xFF)
            if self.send_cmd(41, 0x40000000, 0xFF) == 0x00:
                print("[*] Card ready. Switching to 4 MHz.")
                self.spi.max_speed_hz = 4000000 
                return True
            time.sleep(0.01)
        return False

    def read_block(self, block_address):
        if self.send_cmd(17, block_address, 0xFF) != 0x00: 
            return None
            
        read_size = 512 + 2 + 64
        big_chunk = self.spi.xfer2([0xFF] * read_size)
        try:
            pos = big_chunk.index(0xFE)
            data = big_chunk[pos + 1 : pos + 513]
            if len(data) < 512:
                needed = 512 - len(data)
                extra = self.spi.xfer2([0xFF] * (needed + 2))
                data.extend(extra[:needed])
            return data
        except ValueError: 
            return None

    def print_directory_structure(self):
        print("\n--- FAT32 File System Analysis ---")
        mbr = self.read_block(0)
        if not mbr: 
            print("[!] Failed to read MBR")
            return
            
        lba_start = struct.unpack("<I", bytearray(mbr[454:458]))[0]
        vbr = self.read_block(lba_start)
        if not vbr: 
            print("[!] Failed to read VBR")
            return
            
        sectors_per_cluster = vbr[13]
        reserved_sectors = struct.unpack("<H", bytearray(vbr[14:16]))[0]
        num_fats = vbr[16]
        fat_size = struct.unpack("<I", bytearray(vbr[36:40]))[0]
        root_cluster = struct.unpack("<I", bytearray(vbr[44:48]))[0]
        
        first_fat_sector = lba_start + reserved_sectors
        data_sector = first_fat_sector + (num_fats * fat_size)
        root_dir_sector = data_sector + (root_cluster - 2) * sectors_per_cluster
        
        dir_data = self.read_block(root_dir_sector)
        if not dir_data: return
        
        print(f"{'Filename':<15} | {'Type':<10} | {'Size (Bytes)'}")
        print("-" * 45)
        for i in range(0, 512, 32):
            entry = dir_data[i:i+32]
            if entry[0] == 0x00: break 
            if entry[0] == 0xE5 or entry[11] == 0x0F: continue 
            
            name = "".join(chr(b) for b in entry[0:8]).strip()
            ext = "".join(chr(b) for b in entry[8:11]).strip()
            full_name = f"{name}.{ext}" if ext else name
            is_dir = "DIR" if entry[11] & 0x10 else "FILE"
            size = struct.unpack("<I", bytearray(entry[28:32]))[0]
            print(f"{full_name:<15} | {is_dir:<10} | {size}")

def format_speed(bytes_per_sec):
    if bytes_per_sec > 1024*1024:
        return f"{bytes_per_sec / (1024*1024):.2f} MB/s"
    return f"{bytes_per_sec / 1024:.2f} KB/s"

def run_test():
    sd = SimpleSDSPI(bus=0, device=0) 
    
    if not sd.init_card():
        print("[!] SD card initialization failed! Check connections.")
        return

    try:
        sd.print_directory_structure()
    except Exception as e:
        print(f"[!] FAT32 parsing error: {e}")

    test_mb = 4
    block_size = 512
    num_blocks = (test_mb * 1024 * 1024) // block_size
    output_file = "sd_test_result.bin"

    print(f"\n--- Read Test: {test_mb}MB ---")
    start_time = time.time()
    blocks_read = 0
    
    try:
        with open(output_file, "wb") as f:
            for i in range(num_blocks):
                data = sd.read_block(i)
                if data is None:
                    # Retry once on failure
                    data = sd.read_block(i)
                    if data is None:
                        print(f"\n[!] Critical error at block {i}")
                        break
                
                f.write(bytearray(data))
                blocks_read += 1
                
                if i % 50 == 0 or i == num_blocks - 1:
                    elapsed = time.time() - start_time
                    curr_speed = (blocks_read * block_size) / elapsed if elapsed > 0 else 0
                    percent = (i / num_blocks) * 100
                    speed_str = format_speed(curr_speed)
                    
                    sys.stdout.write(f"\rProgress: {percent:5.1f}% | Speed: {speed_str} | Block: {i}")
                    sys.stdout.flush()

        duration = time.time() - start_time
        avg_speed = (blocks_read * block_size) / duration
        print(f"\n\nTest complete!")
        print(f"Total time: {duration:.2f} sec.")
        print(f"Average speed: {format_speed(avg_speed)}")
        print(f"File saved as: {output_file}")

    except KeyboardInterrupt:
        print("\nTest interrupted by user.")

if __name__ == "__main__":
    run_test()