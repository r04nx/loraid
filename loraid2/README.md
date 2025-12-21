# LoRa Edge ML Optimization System

This project implements a machine learning-based approach to optimize LoRa communication parameters using edge devices (TTGO T1/T3 boards).

## Hardware Requirements

- 2x TTGO T1/T3 boards with LoRa modules
- USB cables for programming
- Computer with Arduino IDE installed

## Software Requirements

### Arduino IDE Setup
1. Install Arduino IDE (version 1.8.x or later)
2. Add ESP32 board support:
   - Open Preferences
   - Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Additional Boards Manager URLs
   - Install "ESP32" from Boards Manager

### Required Libraries
Install the following libraries through Arduino Library Manager:
- LoRa (by Sandeep Mistry)
- Adafruit GFX Library
- Adafruit SSD1306
- ArduinoJson
- ESPAsyncWebServer
- AsyncTCP
- DNSServer

## Installation Steps

1. Clone this repository
2. Open Arduino IDE
3. Install required libraries
4. Upload the web interface files to SPIFFS:
   - Open Arduino IDE
   - Select Tools > ESP32 Sketch Data Upload
   - Wait for upload to complete

## Deployment

### Sender (TTGO T3)
1. Open `sender/sender.ino` in Arduino IDE
2. Select board: "TTGO T1"
3. Select correct port
4. Upload code

### Receiver (TTGO T1)
1. Open `receiver/receiver.ino` in Arduino IDE
2. Select board: "TTGO T1"
3. Select correct port
4. Upload code

## Usage

1. Power on both devices
2. Connect to WiFi network "LoRa-Monitor" (password: "loramonitor123")
3. Open web browser and navigate to `192.168.4.1`
4. Monitor performance metrics and adjust parameters as needed

## Monitoring Interface

The web interface provides:
- Real-time RSSI and SNR monitoring
- Time on Air measurements
- Packet Delivery Rate statistics
- LoRa parameter adjustment
- Performance visualization

## Troubleshooting

1. If LoRa initialization fails:
   - Check wiring connections
   - Verify correct pin definitions
   - Ensure proper power supply

2. If web interface is not accessible:
   - Verify WiFi connection
   - Check if SPIFFS upload was successful
   - Restart the device

3. If performance metrics are not updating:
   - Check serial monitor for error messages
   - Verify LoRa communication between devices
   - Ensure both devices are using same frequency
