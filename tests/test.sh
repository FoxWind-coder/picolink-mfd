#!/bin/bash

# Pin settings (Base 588 + 25 = 613)
GPIO_LED=537
I2C_ADDR=0x3c
I2C_BUS=3

# Cleanup function for exit (CTRL+C)
cleanup() {
    echo 0 | sudo tee /sys/class/gpio/gpio$GPIO_LED/value
    echo "Test completed."
    exit
}
trap cleanup SIGINT

echo "Starting stress test: GPIO 25 + OLED I2C"

# Ensure the GPIO pin is exported
if [ ! -d /sys/class/gpio/gpio$GPIO_LED ]; then
    echo $GPIO_LED | sudo tee /sys/class/gpio/export
fi
echo out | sudo tee /sys/class/gpio/gpio$GPIO_LED/direction

while true; do
    # Turn LED on
    echo 1 | sudo tee /sys/class/gpio/gpio$GPIO_LED/value > /dev/null
    
    # Invert screen (white background)
    sudo i2cset -y $I2C_BUS $I2C_ADDR 0x00 0xA7
    
    sleep 0.01
    
    # Turn LED off
    echo 0 | sudo tee /sys/class/gpio/gpio$GPIO_LED/value > /dev/null
    
    # Restore normal screen mode (black background)
    sudo i2cset -y $I2C_BUS $I2C_ADDR 0x00 0xA6
    
    sleep 0.01
done