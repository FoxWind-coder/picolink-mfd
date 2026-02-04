import serial
import time
import sys

# Port configurations
PICO_PORT = '/dev/ttyPico0'
USB_PORT = '/dev/ttyUSB0'
BAUD = 500000 

try:
    s_out = serial.Serial(PICO_PORT, BAUD, timeout=0)
    s_in = serial.Serial(USB_PORT, BAUD, timeout=0)
except Exception as e:
    print(f"Error opening ports: {e}")
    sys.exit(1)

print(f"Testing {PICO_PORT} -> {USB_PORT} @ {BAUD}...")
print("Press Ctrl+C to stop.")

time.sleep(0.5) 
last_report_time = time.time()
latencies = []
total_sent = 0      
total_received = 0  

try:
    while True:
        current_time = time.time()
        
        # Send timestamp payload
        timestamp_send = time.time()
        payload = f"{timestamp_send:.6f}\n".encode()
        bytes_sent = s_out.write(payload)
        total_sent += bytes_sent
        
        # Read response
        time.sleep(0.001) 
        line = s_in.readline()
        
        if line:
            total_received += len(line)
            try:
                timestamp_rcv = float(line.decode().strip())
                latency_ms = (time.time() - timestamp_rcv) * 1000
                latencies.append(latency_ms)
            except (ValueError, UnicodeDecodeError):
                pass # Ignore corrupted data or decoding errors

        # Output statistics every 100ms
        if current_time - last_report_time >= 0.1:
            if latencies:
                avg_latency = sum(latencies) / len(latencies)
                min_lat = min(latencies)
                max_lat = max(latencies)
                
                sent_kb = total_sent / 1024
                recv_kb = total_received / 1024
                
                stats = (
                    f"\rLatency: Avg: {avg_latency:6.3f}ms | Min: {min_lat:6.3f}ms | Max: {max_lat:6.3f}ms | "
                    f"Samples: {len(latencies)} | Sent: {sent_kb:7.2f} KB | Rcvd: {recv_kb:7.2f} KB"
                )
                
                sys.stdout.write(stats + "   ")
                sys.stdout.flush()
            
            # Reset latency list for the next window while keeping cumulative totals
            latencies = []
            last_report_time = current_time

except KeyboardInterrupt:
    print("\nTest stopped by user.")
finally:
    print(f"Total Sent: {total_sent / 1024:.2f} KB")
    print(f"Total Received: {total_received / 1024:.2f} KB")
    # Warning: Closing ports causes Kernel Panic in the current driver!
    s_out.close()
    s_in.close()