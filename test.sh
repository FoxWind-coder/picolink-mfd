#!/bin/bash

# Настройки пинов (база 588 + 25 = 613)
GPIO_LED=613
I2C_ADDR=0x3c
I2C_BUS=6

# Функция очистки при выходе (CTRL+C)
cleanup() {
    echo 0 | sudo tee /sys/class/gpio/gpio$GPIO_LED/value
    echo "Тест завершен."
    exit
}
trap cleanup SIGINT

echo "Запуск стресс-теста: GPIO 25 + OLED I2C"

# Убедимся, что пин экспортирован
if [ ! -d /sys/class/gpio/gpio$GPIO_LED ]; then
    echo $GPIO_LED | sudo tee /sys/class/gpio/export
fi
echo out | sudo tee /sys/class/gpio/gpio$GPIO_LED/direction

while true; do
    # 1. Зажигаем светодиод
    echo 1 | sudo tee /sys/class/gpio/gpio$GPIO_LED/value > /dev/null
    
    # 2. Инвертируем экран (белый фон)
    sudo i2cset -y $I2C_BUS $I2C_ADDR 0x00 0xA7
    
    sleep 0.01
    
    # 3. Гасим светодиод
    echo 0 | sudo tee /sys/class/gpio/gpio$GPIO_LED/value > /dev/null
    
    # 4. Возвращаем экран в нормальный режим (черный фон)
    sudo i2cset -y $I2C_BUS $I2C_ADDR 0x00 0xA6
    
    sleep 0.01
done
