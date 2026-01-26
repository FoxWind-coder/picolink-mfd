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
            cs=*)  CS_PINS="${arg#*=}" ;; # Example: cs="10 11"
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
    read -p "SPI CS Pins (space separated, max 4): " CS_PINS

    # LED Config
    echo -e "\n--- LED Configuration ---"
    FIRST_LED=true
    while true; do
        read -p "Add LED on pin (or 0 to finish): " LPIN
        if [[ "$LPIN" == "0" || -z "$LPIN" ]]; then break; fi
        if [ "$FIRST_LED" = true ]; then
            echo -e "\n[*] Available system triggers:"
            [ -d /sys/class/leds ] && ls /sys/class/leds | head -n 1 | xargs -I {} cat /sys/class/leds/{}/trigger
            echo -e "\n"
            FIRST_LED=false
        fi
        read -p "Trigger for LED $LPIN: " LTRIG
        LEDS[$LPIN]=${LTRIG:-none}
    done

    # ADC Config
    echo -e "\n--- ADC Configuration ---"
    while true; do
        read -p "Add ADC on pin (e.g. 26-28, or 0 to finish): " APIN
        if [[ "$APIN" == "0" || -z "$APIN" ]]; then break; fi
        read -p "Name for ADC $APIN (e.g. battery): " ANAME
        ADCS[$APIN]=${ANAME:-adc_raw_$APIN}
    done
fi

# 4-6. Apply Function
apply_config() {
    # I2C
    if [[ "$SCL" != "0" && -n "$SCL" ]]; then
        echo "i2c $SCL $SDA" > "$DEV_NODE"
        sleep 0.2
    fi
    # UART
    if [[ "$RX" != "0" && -n "$RX" ]]; then
        echo "uart $RX $TX" > "$DEV_NODE"
        sleep 0.2
    fi
    # SPI Bus
    if [[ "$SCK" != "0" && -n "$SCK" ]]; then
        echo "spi $SCK $MOSI $MISO" > "$DEV_NODE"
        sleep 0.2
        # SPI CS
        if [[ -n "$CS_PINS" ]]; then
            idx=0
            for cspin in $CS_PINS; do
                if [ $idx -lt 4 ]; then
                    echo "spi cs $idx $cspin" > "$DEV_NODE"
                    sleep 0.1
                    idx=$((idx+1))
                fi
            done
        fi
    fi
    # Apply LEDS
    for PIN in "${!LEDS[@]}"; do
        echo "led $PIN" > "$DEV_NODE"
        for i in {1..10}; do
            [ -d "/sys/class/leds/picolink_led_$PIN" ] && break
            sleep 0.1
        done
        [ "${LEDS[$PIN]}" != "none" ] && echo "${LEDS[$PIN]}" > "/sys/class/leds/picolink_led_$PIN/trigger" 2>/dev/null
    done
    # Apply ADCS
    for PIN in "${!ADCS[@]}"; do
        echo "adc$PIN ${ADCS[$PIN]}" > "$DEV_NODE"
        sleep 0.1
    done
}

apply_config

# 7. Persistence Setup
echo -e "\n--- Persistence Setup ---"
read -p "Install this configuration as a permanent hot-plug service? (y/n): " p_choice
if [[ "$p_choice" == "y" ]]; then
    # a. Install module
    mkdir -p "$INSTALL_MOD_PATH"
    cp "$MODULE_SRC" "$INSTALL_MOD_PATH/"
    depmod -a

    # b. Create setup script
    cat <<EOF > "$PERSIST_CONFIG_SCRIPT"
#!/bin/bash
# Auto-generated PicoLink setup
modprobe $MODULE_NAME
# Wait for dev node
for i in {1..20}; do [ -e "$DEV_NODE" ] && break; sleep 0.1; done
[[ "$SCL" != "0" && -n "$SCL" ]] && echo "i2c $SCL $SDA" > "$DEV_NODE"
[[ "$RX" != "0" && -n "$RX" ]] && echo "uart $RX $TX" > "$DEV_NODE"
EOF
    # SPI to persist script
    if [[ "$SCK" != "0" && -n "$SCK" ]]; then
        echo "echo \"spi $SCK $MOSI $MISO\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
        idx=0
        for cspin in $CS_PINS; do
            if [ $idx -lt 4 ]; then
                echo "echo \"spi cs $idx $cspin\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
                idx=$((idx+1))
            fi
        done
    fi
    # LEDs to persist script
    for PIN in "${!LEDS[@]}"; do
        echo "echo \"led $PIN\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
        echo "sleep 0.5" >> "$PERSIST_CONFIG_SCRIPT"
        [[ "${LEDS[$PIN]}" != "none" ]] && echo "echo \"${LEDS[$PIN]}\" > /sys/class/leds/picolink_led_$PIN/trigger" >> "$PERSIST_CONFIG_SCRIPT"
    done
    # ADCs to persist script
    for PIN in "${!ADCS[@]}"; do
        echo "echo \"adc$PIN ${ADCS[$PIN]}\" > $DEV_NODE" >> "$PERSIST_CONFIG_SCRIPT"
    done
    
    chmod +x "$PERSIST_CONFIG_SCRIPT"

    # c. Create Systemd Service
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

    # d. Create Udev Rule
    echo "SUBSYSTEM==\"usb\", ACTION==\"add\", ATTR{idVendor}==\"1d50\", ATTR{idProduct}==\"6150\", TAG+=\"systemd\", ENV{SYSTEMD_WANTS}=\"$SERVICE_NAME\"" > "$UDEV_RULE_FILE"
    
    systemctl daemon-reload
    udevadm control --reload-rules
    echo "[+] Persistence installed! Configuration will trigger every time the Pico is connected."
fi

# 8. Summary
echo -e "\n--- PicoLink System Status ---"
i2cdetect -l | grep "Pico" || echo "I2C: Not activated"
ls -lh /dev/ttyPico0 2>/dev/null || echo "UART: Not activated"
ls -lh /dev/spidev* 2>/dev/null || echo "SPI: No devices found"
ls -lh /dev/picolink 
for PIN in "${!LEDS[@]}"; do
    T=$(cat "/sys/class/leds/picolink_led_$PIN/trigger" 2>/dev/null | grep -o '\[.*\]')
    echo "LED $PIN: $T"
done
for PIN in "${!ADCS[@]}"; do
    H_PATH=$(grep -l "${ADCS[$PIN]}" /sys/class/hwmon/hwmon*/name 2>/dev/null | sed 's/name//')
    if [ -n "$H_PATH" ]; then
        VAL=$(cat "${H_PATH}value" 2>/dev/null)
        echo "ADC $PIN (${ADCS[$PIN]}): Raw Value = $VAL"
    else
        echo "ADC $PIN (${ADCS[$PIN]}): Device not found in hwmon"
    fi
done
echo "--- Configuration Complete ---"