import spidev
import time

# SPI configuration
spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000
spi.mode = 0

def test_len(length):
    # Create test pattern (0, 1, 2, 3...)
    tx = [(i % 256) for i in range(length)]
    
    # xfer2 maintains Chip Select (CS) low between bytes in the same list
    try:
        rx = spi.xfer2(tx)
    except Exception as e:
        print(f"Error during xfer: {e}")
        return False

    if tx == rx:
        print(f"OK (len={length})")
        print(f"   TX: {tx[:8]}...")
        print(f"   RX: {rx[:8]}...")
        return True
    else:
        print(f"FAILED (len={length})")
        # Display first 16 bytes for analysis
        print(f"   TX (first 16): {tx[:16]}")
        print(f"   RX (first 16): {rx[:16]}")
        
        if length > 16:
            print(f"   TX (last 8):  {tx[-8:]}")
            print(f"   RX (last 8):  {rx[-8:]}")
        return False

lengths = [4, 60, 64, 128, 256, 4096, 4096]
for l in lengths:
    test_len(l)
    time.sleep(0.1)

spi.close()