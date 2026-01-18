#!/bin/bash

# --- Colors for output ---
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Starting Picolink Build System...${NC}"

# --- Paths ---
REPO_ROOT=$(pwd)
KERNEL_DIR="$REPO_ROOT/kernel"
FIRMWARE_DIR="$REPO_ROOT/firmware"
BUILD_DIR="$REPO_ROOT/build"
SDK_DIR="$BUILD_DIR/pico-sdk"
UF2_FILE="$FIRMWARE_DIR/build/picolink_pico.uf2"

# --- Check/Install Pico SDK ---
echo -e "\n${YELLOW}[0/3] Checking Pico SDK...${NC}"
if [ -z "$PICO_SDK_PATH" ]; then
    echo -e "${YELLOW}PICO_SDK_PATH is not set. Checking local build folder...${NC}"
    if [ ! -d "$SDK_DIR" ]; then
        echo -e "${YELLOW}Pico SDK not found. Downloading to $SDK_DIR...${NC}"
        mkdir -p "$BUILD_DIR"
        git clone --recursive https://github.com/raspberrypi/pico-sdk.git "$SDK_DIR"
        if [ $? -ne 0 ]; then
            echo -e "${RED}Failed to download Pico SDK!${NC}"
            exit 1
        fi
    fi
    export PICO_SDK_PATH="$SDK_DIR"
    echo -e "${GREEN}Using Pico SDK at: $PICO_SDK_PATH${NC}"
else
    echo -e "${GREEN}Using existing PICO_SDK_PATH: $PICO_SDK_PATH${NC}"
fi

# --- Check Prerequisites ---
echo -e "\n${YELLOW}[1/3] Compiling Firmware...${NC}"
if [ -d "$FIRMWARE_DIR" ]; then
    mkdir -p "$FIRMWARE_DIR/build"
    cd "$FIRMWARE_DIR/build"
    # Pass the SDK path directly to CMake to be sure
    cmake -DPICO_SDK_PATH="$PICO_SDK_PATH" ..
    make -j$(nproc)
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Firmware compiled successfully.${NC}"
    else
        echo -e "${RED}Firmware compilation failed!${NC}"
        exit 1
    fi
else
    echo -e "${RED}Firmware directory not found at $FIRMWARE_DIR${NC}"
    exit 1
fi

echo -e "\n${YELLOW}[2/3] Compiling Kernel Module...${NC}"
cd "$KERNEL_DIR"
make
if [ $? -eq 0 ]; then
    echo -e "${GREEN}Kernel module compiled successfully.${NC}"
else
    echo -e "${RED}Kernel compilation failed!${NC}"
    exit 1
fi

echo -e "\n${YELLOW}[3/3] Deployment Check...${NC}"

# --- Root & Flashing Logic ---
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}Running as non-root user.${NC}"
    echo -e "Compilation finished. Please perform the following steps manually:"
    echo -e " 1. Flash the Pico: 'sudo picotool load -x $UF2_FILE'"
    echo -e " 2. Install the module: 'sudo insmod $KERNEL_DIR/picolink_mfd.ko'"
else
    echo -e "${GREEN}Running with root privileges.${NC}"
    
    # Check for picotool
    if command -v picotool &> /dev/null; then
        echo -e "${YELLOW}picotool found. Attempting to flash Raspberry Pi Pico...${NC}"
        picotool load -x "$UF2_FILE"
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}Pico flashed successfully!${NC}"
        else
            echo -e "${RED}Flash failed. Is the Pico in BOOTSEL mode or connected?${NC}"
        fi
    else
        echo -e "${RED}picotool not found. Skipping auto-flash.${NC}"
    fi

    # Ask for module installation
    echo -n -e "\n${YELLOW}Would you like to install the kernel module now? (y/n): ${NC}"
    read -r choice
    if [ "$choice" == "y" ]; then
        echo -e "Installing module..."
        rmmod picolink_mfd 2>/dev/null
        insmod "$KERNEL_DIR/picolink_mfd.ko"
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}Module picolink_mfd installed successfully.${NC}"
            dmesg | tail -n 5
        else
            echo -e "${RED}Module installation failed.${NC}"
        fi
    else
        echo -e "Skipping module installation."
    fi
fi

echo -e "\n${GREEN}Build process completed.${NC}"