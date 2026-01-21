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
UF2_FILE="$FIRMWARE_DIR/build/picolink_mfd.uf2"
TEST_SCRIPT="$REPO_ROOT/configurator.sh"

# --- Helper Function for Sudo ---
run_as_root() {
    if [ "$EUID" -ne 0 ]; then
        sudo "$@"
    else
        "$@"
    fi
}

# --- Check/Install Pico SDK ---
echo -e "\n${YELLOW}[0/3] Checking Pico SDK...${NC}"
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
    [ -d "$FIRMWARE_DIR/build" ] && rm -rf "$FIRMWARE_DIR/build"
    mkdir -p "$FIRMWARE_DIR/build"
    cd "$FIRMWARE_DIR/build"
    
    cmake -DPICO_SDK_PATH="$PICO_SDK_PATH" ..
    make clean
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
# Create build directory for the kernel module if it doesn't exist
mkdir -p "$KERNEL_DIR/build"

# Using the specific command requested
make clean
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Kernel module compiled successfully.${NC}"
else
    echo -e "${RED}Kernel compilation failed!${NC}"
    exit 1
fi

# --- Deployment ---
echo -e "\n${YELLOW}[3/3] Deployment Check...${NC}"

# 1. Flash Confirmation
echo -n -e "${YELLOW}Do you want to flash the Pico using picotool? (y/n): ${NC}"
read -r flash_choice
if [ "$flash_choice" == "y" ]; then
    if command -v picotool &> /dev/null; then
        echo -e "${YELLOW}Flashing Pico (may require sudo)...${NC}"
        run_as_root picotool load -x "$UF2_FILE"
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}Pico flashed successfully!${NC}"
            FLASH_SUCCESS=true
        else
            echo -e "${RED}Flash failed.${NC}"
        fi
    else
        echo -e "${RED}Error: picotool not found in PATH.${NC}"
    fi
fi

# 2. Module Installation Confirmation
echo -n -e "\n${YELLOW}Do you want to (re)install the kernel module? (y/n): ${NC}"
read -r insmod_choice
if [ "$insmod_choice" == "y" ]; then
    echo -e "${YELLOW}Installing module (requires sudo)...${NC}"
    run_as_root rmmod picolink_mfd 2>/dev/null
    run_as_root insmod "$KERNEL_DIR/build/picolink_mfd.ko"
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Module installed successfully.${NC}"
        MOD_SUCCESS=true
    else
        echo -e "${RED}Failed to install kernel module.${NC}"
    fi
fi

if ls "$KERNEL_DIR/build/picolink_mfd.ko"; then
    MOD_SUCCESS=true
fi

# 3. Final Step: Run Test Script
if [ "$FLASH_SUCCESS" = true ] || [ "$MOD_SUCCESS" = true ]; then
    echo -e "\n${GREEN}Deployment steps completed.${NC}"
    if [ -f "$TEST_SCRIPT" ]; then
        echo -n -e "${YELLOW}Would you like to run the setup script now? (y/n): ${NC}"
        read -r run_test
        if [ "$run_test" == "y" ]; then
            cd ".."
            chmod +x "$TEST_SCRIPT"
            run_as_root "$TEST_SCRIPT"
        fi
    else
        echo -e "${YELLOW}Note: test_activation.sh not found in $REPO_ROOT${NC}"
    fi
fi

echo -e "\n${GREEN}Build and Deployment process finished.${NC}"