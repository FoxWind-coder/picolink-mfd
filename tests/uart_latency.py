import serial
import time
import sys

# Настройки портов
PICO_PORT = '/dev/ttyPico0'
USB_PORT = '/dev/ttyUSB0'
BAUD = 500000 # Вернул вашу скорость из лога strace

try:
    s_out = serial.Serial(PICO_PORT, BAUD, timeout=0)
    s_in = serial.Serial(USB_PORT, BAUD, timeout=0)
except Exception as e:
    print(f"Ошибка открытия портов: {e}")
    sys.exit(1)

print(f"Тестирование {PICO_PORT} -> {USB_PORT} @ {BAUD}...")
print("Нажмите Ctrl+C для остановки.")

time.sleep(0.5) 
last_report_time = time.time()
latencies = []
total_sent = 0      # Общий объем отправленных данных
total_received = 0  # Общий объем полученных данных

try:
    while True:
        current_time = time.time()
        
        # 1. Отправляем метку времени
        timestamp_send = time.time()
        payload = f"{timestamp_send:.6f}\n".encode()
        bytes_sent = s_out.write(payload)
        total_sent += bytes_sent
        
        # 2. Пытаемся прочитать ответ
        time.sleep(0.001) 
        line = s_in.readline()
        
        if line:
            total_received += len(line)
            try:
                timestamp_rcv = float(line.decode().strip())
                latency_ms = (time.time() - timestamp_rcv) * 1000
                latencies.append(latency_ms)
            except (ValueError, UnicodeDecodeError):
                pass # Битая строка или ошибка декодирования

        # 3. Вывод статистики каждые 100мс
        if current_time - last_report_time >= 0.1:
            if latencies:
                avg_latency = sum(latencies) / len(latencies)
                min_lat = min(latencies)
                max_lat = max(latencies)
                
                # Форматируем объем данных (КБ или МБ для удобства)
                sent_kb = total_sent / 1024
                recv_kb = total_received / 1024
                
                stats = (
                    f"\rLatency: Avg: {avg_latency:6.3f}ms | Min: {min_lat:6.3f}ms | Max: {max_lat:6.3f}ms | "
                    f"Samples: {len(latencies)} | Sent: {sent_kb:7.2f} KB | Rcvd: {recv_kb:7.2f} KB"
                )
                
                sys.stdout.write(stats + "   ")
                sys.stdout.flush()
            
            # Очищаем список задержек для следующего окна вывода, 
            # но total_sent/received продолжаем копить
            latencies = []
            last_report_time = current_time

except KeyboardInterrupt:
    print("\nТест остановлен пользователем.")
finally:
    print(f"Итого передано: {total_sent / 1024:.2f} KB")
    print(f"Итого получено: {total_received / 1024:.2f} KB")
    # Важно: закрытие портов вызывает Kernel Panic в вашем текущем драйвере!
    s_out.close()
    s_in.close()