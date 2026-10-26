# ESP-IDF Cpp GPIO Adapter
A Cpp GPIO driver for ESP32 using ESP-IDF, designed around a POSIX-style file interface and the **Adapter design pattern**.

### 1. Adapter Design Pattern
<img width="1024" height="595" alt="image" src="https://github.com/user-attachments/assets/11b4a6a0-924e-441f-8a04-f1c046474bea" />
The driver adapts ESP-IDF's GPIO API to a POSIX-inspired interface.The application uses the `esp_io` interface, while the adapter translates operations into ESP-IDF GPIO calls.

### 2. POSIX-Style Interface
The API is inspired by file operations:
- `open()` — configure and acquire a GPIO.
- `read()` — read an input GPIO.
- `write()` — control an output GPIO.
- `close()` — release a GPIO.
- 
The driver uses custom integer handles beginning at `3`. These are internal driver handles, not operating-system file descriptors.

###  Device Management
The driver maintains a fixed-size device table to track GPIO configuration, access mode, callback information, and device state. Device slot and generation values help identify queued events and reject stale events when a device slot is reused.

### Usage Example 
Examples are present in the examples folder that shows the usage of this 

