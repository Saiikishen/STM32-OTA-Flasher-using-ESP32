
# ESP32 to STM32 Wireless Flasher

This project implements a wireless programming bridge using an ESP32 to flash firmware onto an STM32 microcontroller. It downloads a binary file (`.bin`) from a local HTTP server and programs the STM32 via UART using the built-in system memory bootloader protocol.

This solution is ideal for Over-the-Air (OTA) updates where the STM32 does not have native network connectivity.

## Features

*   **HTTP Firmware Fetch:** Downloads firmware from a specified IP/Port.
*   **Automatic Bootloader Entry:** Controls `BOOT0` and `NRST` pins to force the STM32 into bootloader mode.
*   **Reliable UART Protocol:** Implements the STM32 UART Bootloader protocol (AN3155) with:
    *   Auto-baud rate synchronization (`0x7F`).
    *   Extended Erase command with increased timeouts (40s) for reliability.
    *   Write Memory command with XOR checksum verification.
    *   `SERIAL_8E1` (Even Parity) configuration required by STM32.
*   **Dynamic Buffering:** Buffers firmware in ESP32 RAM before flashing to ensure network latency doesn't interrupt the UART flow.

## Hardware Requirements

*   **Host:** ESP32 Development Board (e.g., ESP32 DevKit V1)
*   **Target:** STM32 Microcontroller (tested on STM32F4 series, but compatible with most STM32s supporting UART bootloading).
*   **Power:** Ensure both boards share a **Common Ground**.

### Pin Configuration

| ESP32 Pin | STM32 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| GPIO 17 (TX2) | UART RX (e.g., PA10) | UART Data | Connect ESP TX to STM RX |
| GPIO 16 (RX2) | UART TX (e.g., PA9) | UART Data | Connect ESP RX to STM TX |
| GPIO 25 | BOOT0 | Boot Mode | High = Bootloader, Low = Flash |
| GPIO 26 | NRST | Reset | Active Low |
| GND | GND | Ground | **Required** |
| 3V3 | 3V3 | Power | If powering STM32 from ESP32 |

> **Note:** The UART pins on the STM32 side depend on the specific Bootloader implementation of your chip. Consult AN2606 to confirm which USART pins are active in boot mode for your specific MCU.

## Configuration

Before flashing the ESP32, modify the following constants in the source code to match your network environment:

```cpp
// Network Credentials
const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASS = "your_wifi_password";

// Firmware Server URL
// Ensure this IP is reachable by the ESP32
const char* FW_URL = "http://192.168.1.100:8000/firmware.bin";

// Buffer Limit (Adjust based on available ESP32 heap)
#define MAX_FIRMWARE_SIZE 100000 
```

## Setup & Usage

### 1. Host the Firmware
You need a simple HTTP server to host your `.bin` file. You can use Python for a quick local test:

1.  Compile your STM32 project to generate a binary file (e.g., `LED_Blink.bin`).
2.  Open a terminal in the folder containing the binary.
3.  Run:
    ```bash
    python3 -m http.server 8000
    ```
4.  Update `FW_URL` in the ESP32 code with your computer's local IP address.

### 2. Flash the ESP32
1.  Open the project in Arduino IDE 
2.  Select your ESP32 board model.
3.  Upload the code.

### 3. Execution Flow
Once the ESP32 boots, it performs the following automatic sequence:
1.  **Connect:** Joins the Wi-Fi network.
2.  **Download:** Fetches the `.bin` file into RAM.
3.  **Enter Bootloader:** Pulses `NRST` low while holding `BOOT0` high.
4.  **Sync:** Sends `0x7F` over UART to synchronize baud rate.
5.  **Erase:** Issues a global mass erase (Wait time: up to 40s).
6.  **Flash:** Writes data to address `0x08000000` in 128-byte chunks.
7.  **Reset:** Pulses `NRST` low while holding `BOOT0` low to boot the new application.

## Troubleshooting

*   **"Sync Failed"**:
    *   Check wiring (TX/RX swapped?).
    *   Ensure STM32 is powered.
    *   Verify `BOOT0` is actually going HIGH (3.3V) during the reset sequence.
*   **"Erase Timeout"**:
    *   Large flash memories take time to erase. The code currently waits 40 seconds. If your chip is larger (e.g., F429/F7), increase the `stm32_wait_ack` timeout in `stm32_extended_erase`.
*   **"Bad Size"**:
    *   The binary is larger than `MAX_FIRMWARE_SIZE`. Increase the limit if your ESP32 has PSRAM or sufficient free heap.
*   **HTTP Failures**:
    *   Ensure the ESP32 and the Python server are on the same subnet.
    *   Check firewall settings on the host computer.
