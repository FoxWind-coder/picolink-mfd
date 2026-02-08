#!/bin/bash

SERVO_PATH="/sys/class/pico-servo/servo8/angle"

# Проверка наличия устройства
if [ ! -f "$SERVO_PATH" ]; then
    echo "Error: Servo 8 not found at $SERVO_PATH"
    exit 1
fi

echo "Starting Servo Sweep (0 <-> 180)... Press [CTRL+C] to stop."

while true; do
    # Движение от 0 до 180
    echo "[*] Moving to 180..."
    for i in $(seq 0 2 180); do
        echo $i > "$SERVO_PATH"
        sleep 0.02 # Регулируй это значение для изменения скорости
    done

    # Движение от 180 до 0
    echo "[*] Moving to 0..."
    for i in $(seq 180 -2 0); do
        echo $i > "$SERVO_PATH"
        sleep 0.02
    done
done