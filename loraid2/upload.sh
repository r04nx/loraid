#!/bin/bash

# Check if Arduino IDE is installed
if ! command -v arduino &> /dev/null; then
    echo "Arduino IDE not found. Please install it first."
    exit 1
fi

# Function to upload to device
upload_to_device() {
    local sketch=$1
    local board=$2
    local port=$3
    
    echo "Uploading $sketch to $board on $port..."
    arduino --upload --board $board --port $port $sketch
}

# Function to upload SPIFFS data
upload_spiffs() {
    local port=$1
    echo "Uploading SPIFFS data to $port..."
    arduino --upload --board esp32:esp32:ttgo-t1 --port $port --pref build.path=./build data/
}

# Main script
echo "LoRa Edge ML System Upload Script"
echo "================================="

# Upload SPIFFS data first
read -p "Enter the port for the sender device (e.g., /dev/ttyUSB0): " sender_port
upload_spiffs $sender_port

# Upload sender code
echo "Uploading sender code..."
upload_to_device "sender/sender.ino" "esp32:esp32:ttgo-t1" $sender_port

# Upload receiver code
read -p "Enter the port for the receiver device (e.g., /dev/ttyUSB1): " receiver_port
echo "Uploading receiver code..."
upload_to_device "receiver/receiver.ino" "esp32:esp32:ttgo-t1" $receiver_port

echo "Upload complete!"
echo "Please check the serial monitor for any errors." 