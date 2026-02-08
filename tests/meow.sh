#!/bin/bash

GPIO_NUM=520 # 512 + 8
GPIO_PATH="/sys/class/gpio/gpio$GPIO_NUM"

# Экспорт
if [ ! -d "$GPIO_PATH" ]; then
    echo "$GPIO_NUM" > /sys/class/gpio/export
fi
echo out > "$GPIO_PATH/direction"

echo "Attempting to drive Servo via Software Bitbang... This is going to be ugly! xD"
echo "Press [CTRL+C] to stop the madness."

# Пытаемся имитировать PWM (очень примерно)
# Нам нужно: 1.5мс High, 18.5мс Low для центрального положения
while true; do
    # HIGH (импульс)
    echo 1 > "$GPIO_PATH/value"
    # Мы не можем сделать sleep 0.0015 в обычном Bash точно, 
    # поэтому просто надеемся на тормознутость USB
    
    # LOW (пауза)
    echo 0 > "$GPIO_PATH/value"
    
    # Небольшая пауза, чтобы не повесить USB стек совсем
    sleep 0.02
done