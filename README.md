# LoRa Optimization Framework

A Smart Framework for optimizing LoRa data transmission through data compression, adaptive parameter selection, and performance benchmarking.

## Project Overview

This project implements an intelligent system for optimizing LoRa data transmission by:
- Implementing data type and pattern-based payload compression
- Dynamically adjusting transmission parameters based on performance results
- Providing comprehensive benchmarking between standard and enhanced transmission methods
- Visualizing performance metrics through a web dashboard

## Repository Structure

- **Sender-Standard**: Standard LoRa sender implementation
- **Sender-Enhanced**: Enhanced LoRa sender with Smart Framework optimization
- **Receiver**: LoRa receiver that responds to both standard and enhanced senders
- **webserver**: Flask web application for data collection and visualization

## Getting Started

1. Upload the appropriate code to your LoRa devices:
   - `Sender-Standard/sender-s.ino` to the standard sender
   - `Sender-Enhanced/sender-e.ino` to the enhanced sender
   - `Receiver/receiver.ino` to the receiver

2. Start the web server:
   ```bash
   cd webserver
   python3 app.py
   ```

3. Access the dashboard at http://localhost:8000

## Data Export

To export transmission data to CSV:
```bash
cd webserver
python3 setup_db.py --extract filename.csv
```

## Hardware Configuration

The project uses LILYGO LoRa32 T3_V1.6.1 and T3_V1.0 boards with the following pin configurations:

### LoRa (SX1276/868/915)
- MOSI: 27
- SCLK: 5
- CS: 18
- DIO: 26
- RST: 14
