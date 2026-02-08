#!/bin/bash

GPIO_NUM=537  # 512 + 25
GPIO_PATH="/sys/class/gpio/gpio$GPIO_NUM"

# 1. Подготовка: экспортируем, если еще не сделано
if [ ! -d "$GPIO_PATH" ]; then
    echo "$GPIO_NUM" > /sys/class/gpio/export 2>/dev/null
fi

# Даем системе время на создание файлов
sleep 0.1
echo out > "$GPIO_PATH/direction"

echo "Starting stress blink on GPIO 25 (sysfs)... Press [CTRL+C] to stop."

# 2. Бесконечный цикл записи
# Мы используем встроенный в bash способ записи, чтобы не вызывать 'echo' как бинарник
while true; do
    printf "1" > "$GPIO_PATH/value"
    printf "0" > "$GPIO_PATH/value"
done