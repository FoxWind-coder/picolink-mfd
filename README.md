# Picolink MFD: Linux Kernel Driver for Raspberry Pi Pico

**Picolink** is a custom Multi-Function Device (MFD) driver ecosystem that allows a Raspberry Pi Pico (connected via USB) to act as a bridge for hardware peripherals on a Linux host. It enables controlling GPIOs, I2C buses, and other hardware directly from the Linux kernel space using standard subsystems.

## 🚀 Features

* **USB-based MFD Core:** A central driver that handles USB communication, packet routing, and synchronization using `completion` primitives to prevent race conditions.
* **GPIO Subsystem Integration:** Full support for standard Linux GPIO character devices (`/dev/gpiochipX`) and `sysfs` interface.
* Input/Output mode switching.
* Pull-up/Pull-down configuration support via `pinconf`.
* Interrupt-safe USB transfers using `GFP_ATOMIC`.


* **I2C Controller Bridge:** (In development/Integrated) Registering a virtual I2C adapter to control physical I2C devices connected to the Pico.
* **Custom Communication Protocol:** A robust packet-based protocol featuring:
* `CMD_TYPE_READ/WRITE/CONFIG` operations.
* Synchronous transfers with `picolink_transfer`.
* Asynchronous event handling via USB Bulk-In URBs.



## 🏗 Architecture

The project consists of several layers:

1. **Kernel Core (`picolink-core.c`):** Manages USB probing, interface indexing, and provides an API for sub-devices.
2. **GPIO Sub-device (`mfd-gpio.c`):** Implements `gpio_chip` operations and communicates with the core.
3. **Pico Firmware:** A C/C++ SDK application running on the RP2040 that parses incoming USB packets and executes hardware commands.

## 🛠 Installation & Usage

### Prerequisites

* Linux Kernel Headers (6.8+ recommended).
* GCC-13+ and Make.
* Raspberry Pi Pico with Picolink Firmware flashed.

### Building

```bash
cd kernel
make
sudo insmod picolink_mfd.ko

```

### Controlling GPIO

Once the driver is loaded, find your GPIO chip:

```bash
# Check dmesg for the assigned GPIO base
dmesg | grep picolink

# Export a pin (e.g., local pin 2 on a chip with base 601 = 603)
echo 603 | sudo tee /sys/class/gpio/export
echo in | sudo tee /sys/class/gpio/gpio603/direction
cat /sys/class/gpio/gpio603/value

```

## 🛡 Memory Safety & DMA

The driver is designed with modern Linux kernel constraints in mind. It strictly avoids stack-based DMA buffers to prevent kernel warnings and crashes, utilizing `kzalloc` for all USB transfer packets to ensure reliable memory mapping.

## 🔧 Advanced Configuration

### GPIO Operating Modes

The driver utilizes the `set_config` interface to communicate with the RP2040 internal pad controllers. The following modes are supported:

| Mode | Description | Payload Value |
| --- | --- | --- |
| **Input** | High-impedance digital input | `0x00` |
| **Output** | Push-pull digital output | `0x01` |
| **Pull-up** | Internal weak pull-up (~50kΩ) enabled | `0x02` |
| **Pull-down** | Internal weak pull-down (~50kΩ) enabled | `0x03` |

### Dynamic Pin Function Switching (UART/I2C/SPI)

Picolink supports dynamic re-routing of hardware blocks to different physical pins, leveraging the RP2040's flexible PIO and Multiplexer (MUX) capabilities.

To switch a pin function, the driver sends a `CMD_TYPE_CONFIG` packet with a function selector:

* **Function Mapping:** You can reassign I2C0, I2C1, or UART0/1 to any valid GPIO pair via the `pinctrl` sysfs attributes (if implemented) or through custom `ioctl` calls provided by the MFD core.
* **Safety:** The driver validates pin-muxing requests to ensure that chosen pins support the requested peripheral (e.g., ensuring I2C is not assigned to a non-I2C capable pin group).

```c
// Example: Internal packet logic for switching pin 4 to I2C SDA
pkt->header.type = CMD_TYPE_CONFIG;
pkt->header.iface_idx = IFACE_MUX;
pkt->payload[0] = 4;    // Pin Number
pkt->payload[1] = 0x03; // Function ID (e.g., I2C)

```

## 📊 Performance & Reliability

* **DMA-Safe Buffers:** All USB transfers use heap-allocated memory (`kzalloc`) to comply with Linux kernel DMA mapping requirements, preventing "Transfer buffer on stack" oopses.
* **Synchronous IO:** `picolink_transfer` implements a wait-for-completion mechanism with a 1000ms timeout, ensuring that the kernel stays in sync with the physical hardware state.

## 🛠 Low-Level Control via `/dev/picolink`

While standard subsystems (GPIO, I2C) are preferred, you can communicate with the hardware directly using the character device node. This is useful for debugging or performance testing.

### Device Node Access

The MFD core registers a character device at `/dev/picolink`. You can send raw packets or use `ioctl` (if implemented) to trigger specific hardware functions.

### Example: Manual Pin Function Switching

To switch a pin's function (e.g., assigning a hardware block to a specific GPIO), you can echo binary data or use a simple Python script.

**Bash Example (Switching GPIO 4 to I2C Function):**

```bash
# Writing a configuration packet
echo -e "i2c 2 7" | sudo tee /dev/picolink
echo -e "i2c 4 5" | sudo tee /dev/picolink
echo -e "uart 8 9" | sudo tee /dev/picolink
```



---

## ⚠️ Hardware Constraints & Pinout Reference

When reconfiguring pins for **I2C**, **UART**, or **SPI**, it is mandatory to refer to the official **Raspberry Pi Pico Pinout**.

### Critical Notes:

* **Valid Muxing:** The RP2040 chip has specific "hard-wired" possibilities for its peripherals. For example, `I2C0` can only be mapped to specific pairs of pins (e.g., GPIO 0/1, 4/5, 8/9, etc.).
* **Conflict Prevention:** The driver does not currently block "impossible" muxing requests. **Always verify** that your target GPIO supports the desired function (e.g., UART TX, PWM, or ADC) by checking the pinout diagram.
* **Voltage Levels:** All GPIOs operate at **3.3V**. Connecting 5V signals directly will damage the Pico and potentially the host USB port.

## 📜 License

GPL v2.0


1. A **"Protocol Specification"** section describing the packet structure (Header/Payload)?
2. Instructions for the **Pico-side firmware** setup?
3. An **ADC support** roadmap?
