#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>

// LoRa pins (adjust according to your board)
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26

// LoRa configuration
const int frequency = 915E6; // 915MHz

// WiFi credentials
const char* ssid = "Dedsec";
const char* password = "asdfghjkl";

// API endpoint
const char* apiEndpoint = "192.168.25.237:8000";

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
    
    // Initialize WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
    Serial.println("IP address: " + WiFi.localIP().toString());
    
    // Initialize LoRa
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(frequency)) {
        Serial.println("LoRa initialization failed!");
        while (1);
    }
    
    Serial.println("Receiver initialized");
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
        
        // Send reception metrics to API
        sendMetricsToAPI(metrics);
    }
}

void sendAcknowledgement(ReceptionMetrics metrics) {
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
}

void sendMetricsToAPI(ReceptionMetrics metrics) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(String("http://") + apiEndpoint + "/api/reception");
        http.addHeader("Content-Type", "application/json");
        
        String json = "{";
        json += "\"data\":\"" + metrics.data + "\",";
        json += "\"rssi\":" + String(metrics.rssi) + ",";
        json += "\"snr\":" + String(metrics.snr) + ",";
        json += "\"timestamp\":" + String(metrics.receiveTime) + ",";
        json += "\"source\":\"" + metrics.source + "\"";
        json += "}";
        
        int httpResponseCode = http.POST(json);
        if (httpResponseCode > 0) {
            Serial.println("HTTP Response code: " + String(httpResponseCode));
        } else {
            Serial.println("Error sending HTTP request: " + http.errorToString(httpResponseCode));
        }
        
        http.end();
    } else {
        Serial.println("WiFi not connected. Cannot send metrics to API.");
    }
}