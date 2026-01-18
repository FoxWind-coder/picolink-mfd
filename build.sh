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

# Если переменная установлена, проверяем, существует ли этот путь
if [ -n "$PICO_SDK_PATH" ] && [ ! -d "$PICO_SDK_PATH" ]; then
    echo -e "${YELLOW}Warning: PICO_SDK_PATH is set but directory does not exist. Resetting...${NC}"
    unset PICO_SDK_PATH
fi

if [ -z "$PICO_SDK_PATH" ]; then
    if [ ! -d "$SDK_DIR" ]; then
        echo -e "${YELLOW}Downloading Pico SDK to $SDK_DIR...${NC}"
        mkdir -p "$BUILD_DIR"
        git clone --recursive https://github.com/raspberrypi/pico-sdk.git "$SDK_DIR"
    fi
    export PICO_SDK_PATH="$SDK_DIR"
fi

echo -e "${GREEN}Using Pico SDK at: $PICO_SDK_PATH${NC}"

# --- Compiling Firmware ---
echo -e "\n${YELLOW}[1/3] Compiling Firmware...${NC}"
if [ -d "$FIRMWARE_DIR" ]; then
    # ОЧИСТКА: Удаляем старый кэш, чтобы избежать конфликтов путей
    if [ -f "$FIRMWARE_DIR/build/CMakeCache.txt" ]; then
        echo -e "${YELLOW}Cleaning previous CMake cache...${NC}"
        rm -rf "$FIRMWARE_DIR/build"
    fi

    mkdir -p "$FIRMWARE_DIR/build"
    cd "$FIRMWARE_DIR/build"
    
    # Конфигурация и сборка прошивки
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

# --- Compiling Kernel Module ---
echo -e "\n${YELLOW}[2/3] Compiling Kernel Module...${NC}"
cd "$KERNEL_DIR"
make clean  # Чистая сборка модуля
make
if [ $? -eq 0 ]; then
    echo -e "${GREEN}Kernel module compiled successfully.${NC}"
else
    echo -e "${RED}Kernel compilation failed!${NC}"
    exit 1
fi

# --- Deployment ---
echo -e "\n${YELLOW}[3/3] Deployment Check...${NC}"

if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}Running as non-root user.${NC}"
    echo -e "Compilation finished. Please manual flash and insmod."
else
    echo -e "${GREEN}Running with root privileges.${NC}"
    
    if command -v picotool &> /dev/null; then
        echo -e "${YELLOW}Flashing Pico...${NC}"
        picotool load -x "$UF2_FILE"
        [ $? -eq 0 ] && echo -e "${GREEN}Pico flashed!${NC}" || echo -e "${RED}Flash failed.${NC}"
    else
        echo -e "${RED}picotool not found.${NC}"
    fi

    echo -n -e "\n${YELLOW}Install kernel module? (y/n): ${NC}"
    read -r choice
    if [ "$choice" == "y" ]; then
        rmmod picolink_mfd 2>/dev/null
        insmod "$KERNEL_DIR/picolink_mfd.ko"
        [ $? -eq 0 ] && echo -e "${GREEN}Module installed.${NC}" || echo -e "${RED}Failed to install.${NC}"
    fi
fi

echo -e "\n${GREEN}Build process completed.${NC}"