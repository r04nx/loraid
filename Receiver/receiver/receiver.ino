#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>

// LoRa pins (adjust according to your board)
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26

// LoRa configuration
const int frequency = 868E6; // 868MHz

// Structure to store reception metrics
struct ReceptionMetrics {
    int rssi;
    float snr;
    unsigned long receiveTime;
    String source;
    String data;
};

void setup() {
    Serial.begin(115200);
    Serial.println("Starting LoRa Receiver...");
    
    // Initialize LoRa
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(frequency)) {
        Serial.println("LoRa initialization failed!");
        while (1);
    }
    
    Serial.println("LoRa initialized successfully at 868MHz");
    Serial.println("Receiver ready for packets");
}

void loop() {
    // Check for incoming LoRa packets
    if (LoRa.parsePacket()) {
        // Record reception time
        unsigned long receiveTime = millis();
        
        // Read packet
        String received = LoRa.readString();
        Serial.println("Received: " + received);
        
        // Determine source (enhanced or standard)
        String source = "standard";
        if (received.startsWith("ENHANCED:")) {
            source = "enhanced";
            received = received.substring(9); // Remove the ENHANCED: prefix
        } else if (received.startsWith("STANDARD:")) {
            source = "standard";
            received = received.substring(9); // Remove the STANDARD: prefix
        }
        
        // Get reception metrics
        ReceptionMetrics metrics;
        metrics.rssi = LoRa.packetRssi();
        metrics.snr = LoRa.packetSnr();
        metrics.receiveTime = receiveTime;
        metrics.source = source;
        metrics.data = received;
        
        // Send ACK with metrics
        sendAcknowledgement(metrics);
        
        // Print metrics to Serial
        Serial.println("RSSI: " + String(metrics.rssi) + " dBm");
        Serial.println("SNR: " + String(metrics.snr) + " dB");
    }
}

// Extract LoRa parameters from enhanced packet metadata if present
bool extractLoRaParameters(String& data, int& sf, int& bw, int& cr) {
    // Check if the data contains LoRa parameter metadata
    int metaStart = data.indexOf("<META:");
    if (metaStart >= 0) {
        int metaEnd = data.indexOf(">", metaStart);
        if (metaEnd > metaStart) {
            String meta = data.substring(metaStart + 6, metaEnd);
            // Parse parameters: format is <META:SF7,BW125,CR5>
            int sfPos = meta.indexOf("SF");
            int bwPos = meta.indexOf("BW");
            int crPos = meta.indexOf("CR");
            
            if (sfPos >= 0) {
                sf = meta.substring(sfPos + 2, bwPos > 0 ? meta.indexOf(",", sfPos) : meta.length()).toInt();
            }
            
            if (bwPos >= 0) {
                String bwStr = meta.substring(bwPos + 2, crPos > 0 ? meta.indexOf(",", bwPos) : meta.length());
                bw = bwStr.toInt() * 1000; // Convert from kHz to Hz
            }
            
            if (crPos >= 0) {
                cr = meta.substring(crPos + 2).toInt();
            }
            
            // Remove metadata from the actual data
            data = data.substring(0, metaStart) + data.substring(metaEnd + 1);
            
            Serial.println("Extracted parameters: SF" + String(sf) + ", BW" + String(bw) + ", CR" + String(cr));
            return true;
        }
    }
    return false;
}

void sendAcknowledgement(ReceptionMetrics metrics) {
    // Extract and adapt to LoRa parameters if present in enhanced packets
    int sf = 7; // Default
    int bw = 125E3; // Default
    int cr = 5; // Default
    
    if (metrics.source == "enhanced") {
        // Try to extract parameters from the data
        bool paramsFound = extractLoRaParameters(metrics.data, sf, bw, cr);
        
        // Adapt receiver to these parameters for the acknowledgment if found
        if (paramsFound) {
            LoRa.setSpreadingFactor(sf);
            LoRa.setSignalBandwidth(bw);
            LoRa.setCodingRate4(cr);
            Serial.println("Using extracted parameters for ACK: SF" + String(sf) + ", BW" + String(bw) + ", CR" + String(cr));
        }
    }
    
    // Build ACK message with metrics
    String ackPrefix = metrics.source == "enhanced" ? "ENHANCED_ACK:" : "STANDARD_ACK:";
    String ack = ackPrefix + "{";
    ack += "\"rssi\":" + String(metrics.rssi) + ",";
    ack += "\"snr\":" + String(metrics.snr) + ",";
    ack += "\"timestamp\":" + String(metrics.receiveTime) + ",";
    ack += "\"data\":\"" + metrics.data + "\"";
    ack += "}";
    
    // Send ACK
    LoRa.beginPacket();
    LoRa.print(ack);
    LoRa.endPacket();
    
    Serial.println("Sent ACK: " + ack);
    
    // Reset to default parameters for receiving
    if (metrics.source == "enhanced") {
        LoRa.setSpreadingFactor(7);
        LoRa.setSignalBandwidth(125E3);
        LoRa.setCodingRate4(5);
    }
}

// WiFi and API functionality removed