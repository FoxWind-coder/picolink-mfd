#!/bin/bash

# Configuration paths
MODULE_NAME="picolink_mfd"
MODULE_SRC="$(pwd)/kernel/build/picolink_mfd.ko"
INSTALL_MOD_PATH="/lib/modules/$(uname -r)/extra"
PERSIST_CONFIG_SCRIPT="/usr/local/bin/picolink_setup.sh"
SERVICE_NAME="picolink-config.service"
UDEV_RULE_FILE="/etc/udev/rules.d/99-picolink.rules"
DEV_NODE="/dev/picolink"

# Root privilege check
if [ "$EUID" -ne 0 ]; then
    echo "Error: Please run as root (use sudo)."
    exit 1
fi

# 0. Service Management (Uninstall/Reinstall Check)
if [ -f "$UDEV_RULE_FILE" ] || [ -f "/etc/systemd/system/$SERVICE_NAME" ]; then
    echo -e "\n--- Existing Installation Detected ---"
    read -p "Service already exists. [u]ninstall or [r]einstall/overwrite? (u/r/skip): " s_choice
    if [[ "$s_choice" == "u" ]]; then
        echo "[*] Removing service and module..."
        systemctl stop "$SERVICE_NAME" 2>/dev/null
        systemctl disable "$SERVICE_NAME" 2>/dev/null
        rm -f "/etc/systemd/system/$SERVICE_NAME"
        rm -f "$UDEV_RULE_FILE"
        rm -f "$PERSIST_CONFIG_SCRIPT"
        rmmod "$MODULE_NAME" 2>/dev/null
        rm -f "$INSTALL_MOD_PATH/$MODULE_NAME.ko"
        depmod -a
        systemctl daemon-reload
        echo "[+] Uninstalled successfully."
        exit 0
    elif [[ "$s_choice" == "skip" ]]; then
        echo "[*] Proceeding without changes to service."
    fi
fi

echo "--- Initializing PicoLink MFD Module ---"

# 1. Reload the kernel module
if lsmod | grep -q "$MODULE_NAME"; then
    echo "[*] Removing existing module $MODULE_NAME..."
    rmmod "$MODULE_NAME"
fi

if [ -f "$MODULE_SRC" ]; then
    echo "[*] Inserting module from $MODULE_SRC..."
    insmod "$MODULE_SRC"
    sleep 1 
elif [ -f "$INSTALL_MOD_PATH/$MODULE_NAME.ko" ]; then
    echo "[*] Using installed module..."
    modprobe "$MODULE_NAME" 2>/dev/null || insmod "$INSTALL_MOD_PATH/$MODULE_NAME.ko"
else
    echo "Error: Module not found. Please run the build script first."
    exit 1
fi

if [ ! -e "$DEV_NODE" ]; then
    echo "Error: Device node $DEV_NODE was not created."
    exit 1
fi

# Parsing variables
SCL=""
SDA=""
RX=""
TX=""
SCK=""
MOSI=""
MISO=""
CS_PINS=""
declare -A LEDS
declare -A ADCS
declare -A SERVOS

# 2. Process command line arguments
if [ $# -gt 0 ]; then
    for arg in "$@"; do
        case $arg in
            scl=*) SCL="${arg#*=}" ;;
            sda=*) SDA="${arg#*=}" ;;
            rx=*)  RX="${arg#*=}" ;;
            tx=*)  TX="${arg#*=}" ;;
            sck=*) SCK="${arg#*=}" ;;
            mosi=*) MOSI="${arg#*=}" ;;
            miso=*) MISO="${arg#*=}" ;;
            cs=*)  CS_PINS="${arg#*=}" ;;
            led*=*) 
                PIN=$(echo "${arg%=*}" | grep -oE '[0-9]+')
                TRIGGER="${arg#*=}"
                LEDS[$PIN]=$TRIGGER
                ;;
            adc*=*)
                PIN=$(echo "${arg%=*}" | grep -oE '[0-9]+')
                NAME="${arg#*=}"
                ADCS[$PIN]=$NAME
                ;;
            servo*=*)
                PIN=$(echo "${arg%=*}" | grep -oE '[0-9]+')
                PARAMS="${arg#*=}"
                SERVOS[$PIN]=$PARAMS
                ;;
        esac
    done
else
    # 3. Interactive Mode
    echo "--- Interactive Configuration (Enter 0 to skip) ---"
    read -p "I2C SCL Pin: " SCL
    read -p "I2C SDA Pin: " SDA
    read -p "UART RX Pin: " RX
    read -p "UART TX Pin: " TX
    
    echo -e "\n--- SPI Configuration ---"
    read -p "SPI SCK Pin: " SCK
    read -p "SPI MOSI Pin: " MOSI
    read -p "SPI MISO Pin: " MISO
    read -p "SPI CS Pins (space separated): " CS_PINS

    # LED Config
    echo -e "\n--- LED Configuration ---"
    FIRST_LED=true
    while true; do
        read -p "Add LED on pin (or 0 to finish): " LPIN
        if [[ "$LPIN" == "0" || -z "$LPIN" ]]; then break; fi
        if [ "$FIRST_LED" = true ]; then
            echo -e "[*] Available system triggers: $([ -d /sys/class/leds ] && ls /sys/class/leds | head -n 1 | xargs -I {} cat /sys/class/leds/{}/trigger | tr '\n' ' ')"
            FIRST_LED=false
        fi
        read -p "Trigger for LED $LPIN: " LTRIG
        LEDS[$LPIN]=${LTRIG:-none}
    done

    # ADC Config
    echo -e "\n--- ADC Configuration ---"
    while true; do
        read -p "Add ADC on pin (26-28, or 29 for Temp, or 0 to finish): " APIN
        if [[ "$APIN" == "0" || -z "$APIN" ]]; then break; fi
        read -p "Name for ADC $APIN (e.g. battery или vsys): " ANAME
        ADCS[$APIN]=${ANAME:-adc_$APIN}
    done

    # Servo Config
    echo -e "\n--- Servo Configuration ---"
    while true; do
        read -p "Add Servo on pin (or 0 to finish): " SPIN
        if [[ "$SPIN" == "0" || -z "$SPIN" ]]; then break; fi
        read -p "Range (max angle, e.g. 180): " SRANGE
        read -p "Min Pulse (us, default 500): " SMIN
        read -p "Max Pulse (us, default 2500): " SMAX
        SERVOS[$SPIN]="${SRANGE:-180} ${SMIN:-500} ${SMAX:-2500}"
    done
fi

# 4-6. Apply Function
apply_config() {
    echo -e "\n[*] Applying configuration to hardware..."
    # I2C
    [[ "$SCL" != "0" && -n "$SCL" ]] && echo "i2c $SCL $SDA" > "$DEV_NODE" && sleep 0.1
    # UART
    [[ "$RX" != "0" && -n "$RX" ]] && echo "uart $RX $TX" > "$DEV_NODE" && sleep 0.1
    # SPI
    if [[ "$SCK" != "0" && -n "$SCK" ]]; then
        echo "spi $SCK $MOSI $MISO" > "$DEV_NODE" && sleep 0.1
        idx=0
        for cspin in $CS_PINS; do
            [ $idx -lt 4 ] && echo "spi cs $idx $cspin" > "$DEV_NODE" && sleep 0.1
            idx=$((idx+1))
        done
    fi
    # LEDs
    for PIN in "${!LEDS[@]}"; do
        echo "led $PIN" > "$DEV_NODE"
        for i in {1..10}; do [ -d "/sys/class/leds/picolink_led_$PIN" ] && break; sleep 0.1; done
        [ "${LEDS[$PIN]}" != "none" ] && echo "${LEDS[$PIN]}" > "/sys/class/leds/picolink_led_$PIN/trigger" 2>/dev/null
    done
    # ADCs
    for PIN in "${!ADCS[@]}"; do
        echo "adc$PIN ${ADCS[$PIN]}" > "$DEV_NODE" && sleep 0.2
    done
    # Servos
    for PIN in "${!SERVOS[@]}"; do
        echo "servo$PIN ${SERVOS[$PIN]}" > "$DEV_NODE" && sleep 0.1
    done
}

apply_config

# 7. Persistence Setup
echo -e "\n--- Persistence Setup ---"
read -p "Install this configuration as a permanent hot-plug service? (y/n): " p_choice
if [[ "$p_choice" == "y" ]]; then
    mkdir -p "$INSTALL_MOD_PATH"
    cp "$MODULE_SRC" "$INSTALL_MOD_PATH/"
    depmod -a

    cat <<EOF > "$PERSIST_CONFIG_SCRIPT"
#!/bin/bash
# Auto-generated PicoLink setup
modprobe $MODULE_NAME
for i in {1..20}; do [ -e "$DEV_NODE" ] && break; sleep 0.1; done
[[ "$SCL" != "0" && -n "$SCL" ]] && echo "i2c $SCL $SDA" > "$DEV_NODE"
[[ "$RX" != "0" && -n "$RX" ]] && echo "uart $RX $TX" > "$DEV_NODE"
EOF
    # SPI
    if [[ "$SCK" != "0" && -n "$SCK" ]]; then
        echo "echo \"spi $SCK $MOSI $MISO\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
        idx=0
        for cspin in $CS_PINS; do
            [ $idx -lt 4 ] && echo "echo \"spi cs $idx $cspin\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
            idx=$((idx+1))
        done
    fi
    # LEDs
    for PIN in "${!LEDS[@]}"; do
        echo "echo \"led $PIN\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
        echo "sleep 0.5" >> "$PERSIST_CONFIG_SCRIPT"
        [[ "${LEDS[$PIN]}" != "none" ]] && echo "echo \"${LEDS[$PIN]}\" > /sys/class/leds/picolink_led_$PIN/trigger" >> "$PERSIST_CONFIG_SCRIPT"
    done
    # ADCs
    for PIN in "${!ADCS[@]}"; do
        echo "echo \"adc$PIN ${ADCS[$PIN]}\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
    done
    # Servos
    for PIN in "${!SERVOS[@]}"; do
        echo "echo \"servo$PIN ${SERVOS[$PIN]}\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
    done
    
    chmod +x "$PERSIST_CONFIG_SCRIPT"

    cat <<EOF > "/etc/systemd/system/$SERVICE_NAME"
[Unit]
Description=PicoLink MFD Hot-plug Configurator
After=network.target
[Service]
Type=oneshot
ExecStart=$PERSIST_CONFIG_SCRIPT
[Install]
WantedBy=multi-user.target
EOF

    echo "SUBSYSTEM==\"usb\", ACTION==\"add\", ATTR{idVendor}==\"1d50\", ATTR{idProduct}==\"6150\", TAG+=\"systemd\", ENV{SYSTEMD_WANTS}=\"$SERVICE_NAME\"" > "$UDEV_RULE_FILE"
    systemctl daemon-reload
    udevadm control --reload-rules
    echo "[+] Persistence installed!"
fi

# 8. Summary & Testing
echo -e "\n--- PicoLink System Status & Testing ---"
i2cdetect -l | grep "Pico" || echo "I2C: Not activated"
[ -e /dev/ttyPico0 ] && echo "UART: /dev/ttyPico0 is ready" || echo "UART: Not activated"

# Тест ADC через hwmon
for PIN in "${!ADCS[@]}"; do
    NAME_TO_FIND="${ADCS[$PIN]}"
    HWMON_PATH=""
    for h in /sys/class/hwmon/hwmon*; do
        if [ -f "$h/name" ] && [ "$(cat $h/name)" == "$NAME_TO_FIND" ]; then
            HWMON_PATH="$h"
            break
        fi
    done

    if [ -n "$HWMON_PATH" ]; then
        # Обычно значение лежит в in0_input или value
        VAL="N/A"
        [ -f "$HWMON_PATH/in0_input" ] && VAL=$(cat "$HWMON_PATH/in0_input")
        [ -f "$HWMON_PATH/value" ] && VAL=$(cat "$HWMON_PATH/value")
        echo "ADC $PIN ($NAME_TO_FIND): HWmon Path $HWMON_PATH, Current Value = $VAL"
    else
        echo "ADC $PIN ($NAME_TO_FIND): Waiting for kernel to register hwmon..."
        sleep 1
        # Вторая попытка
        for h in /sys/class/hwmon/hwmon*; do
            [ "$(cat $h/name 2>/dev/null)" == "$NAME_TO_FIND" ] && echo "ADC $PIN ($NAME_TO_FIND): Found! Value = $(cat $h/*input 2>/dev/null || cat $h/value 2>/dev/null)"
        done
    fi
done

for PIN in "${!SERVOS[@]}"; do
    [ -d "/sys/class/pico-servo/servo$PIN" ] && echo "Servo $PIN: /sys/class/pico-servo/servo$PIN (Range: $(echo ${SERVOS[$PIN]} | cut -d' ' -f1))"
done

for PIN in "${!LEDS[@]}"; do
    [ -d "/sys/class/leds/picolink_led_$PIN" ] && echo "LED $PIN: [$(cat /sys/class/leds/picolink_led_$PIN/trigger | grep -o '\[.*\]' | tr -d '[]')]"
done

echo -e "\n--- Configuration Complete ---"