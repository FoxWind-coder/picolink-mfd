import gpiod
from gpiod.line import Direction, Bias
import time

# Путь к чипу
CHIP_PATH = '/dev/gpiochip2'
LINE_OFFSET = 4

print("--- Testing PicoLink GPIO Pulls (v2.0 API) ---")

def test_bias(bias_mode, label):
    with gpiod.request_lines(
        CHIP_PATH,
        consumer="pico-test",
        config={
            LINE_OFFSET: gpiod.LineSettings(
                direction=Direction.INPUT,
                bias=bias_mode,
            )
        },
    ) as request:
        # Даем время на срабатывание подтяжки (задержка USB + емкость линии)
        time.sleep(0.2)
        val = request.get_value(LINE_OFFSET)
        print(f"Mode: {label} | Value: {val}")

try:
    # Тестируем Pull-up (Pico подтянет пин к 3.3V)
    test_bias(Bias.PULL_UP, "PULL-UP")
    
    # Тестируем Pull-down (Pico подтянет пин к GND)
    test_bias(Bias.PULL_DOWN, "PULL-DOWN")
    
    # Тестируем Disabled (состояние "висящего" пина)
    test_bias(Bias.DISABLED, "DISABLED ")
    
except FileNotFoundError:
    print(f"Error: {CHIP_PATH} not found. Check if Pico is connected.")
except Exception as e:
    print(f"Error: {e}")