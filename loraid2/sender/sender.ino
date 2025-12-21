#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "ml_handler.h"

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// LoRa pins for TTGO T3
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS 18
#define LORA_RST 23
#define LORA_DIO0 26

// Access Point settings
const char* AP_SSID = "LoRa-Monitor";
const char* AP_PASSWORD = "loramonitor123";

// Global variables
MLHandler mlHandler;
uint8_t spreadingFactor = 7;
uint8_t codingRate = 5;
long bandwidth = 125E3;
int8_t txPower = 17;
float batteryLevel = 1.0; // Simulated battery level

// Performance metrics
struct PerformanceMetrics {
    float rssi;
    float snr;
    uint32_t timeOnAir;
    uint32_t packetSize;
    uint8_t retries;
    bool success;
    float batteryVoltage;
} metrics;

AsyncWebServer server(80);
DNSServer dnsServer;
StaticJsonDocument<512> metricsJson;

// Function declarations
void setupDisplay();
void setupLoRa();
void setupWiFi();
void setupWebServer();
void updateLoRaParameters();
void updateMetrics(uint32_t startTime);
void displayMetrics();
float getBatteryVoltage();
void optimizeParameters(uint8_t payloadSize);

void setupDisplay() {
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.display();
}

void setupLoRa() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    
    if (!LoRa.begin(915E6)) {
        Serial.println("LoRa init failed!");
        while (1);
    }
    
    // Set initial LoRa parameters
    LoRa.setSpreadingFactor(spreadingFactor);
    LoRa.setCodingRate4(codingRate);
    LoRa.setSignalBandwidth(bandwidth);
    LoRa.setTxPower(txPower);
}

void setupWiFi() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    dnsServer.start(53, "*", WiFi.softAPIP());
}

void setupWebServer() {
    if(!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    // Serve static files
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // API endpoints
    server.on("/api/metrics", HTTP_GET, [](AsyncWebServerRequest *request) {
        String jsonString;
        serializeJson(metricsJson, jsonString);
        request->send(200, "application/json", jsonString);
    });
    
    server.on("/api/parameters", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Handle parameter updates
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, (char*)data);
        
        spreadingFactor = doc["sf"] | spreadingFactor;
        codingRate = doc["cr"] | codingRate;
        bandwidth = doc["bw"] | bandwidth;
        txPower = doc["tp"] | txPower;
        
        updateLoRaParameters();
        request->send(200);
    });

    server.begin();
}

void updateLoRaParameters() {
    LoRa.setSpreadingFactor(spreadingFactor);
    LoRa.setCodingRate4(codingRate);
    LoRa.setSignalBandwidth(bandwidth);
    LoRa.setTxPower(txPower);
    
    // Update JSON for web interface
    metricsJson["sf"] = spreadingFactor;
    metricsJson["cr"] = codingRate;
    metricsJson["bw"] = bandwidth;
    metricsJson["tp"] = txPower;
}

void updateMetrics(uint32_t startTime) {
    metrics.timeOnAir = millis() - startTime;
    metrics.rssi = LoRa.packetRssi();
    metrics.snr = LoRa.packetSnr();
    metrics.batteryVoltage = getBatteryVoltage();
    
    // Update JSON for web interface
    metricsJson["rssi"] = metrics.rssi;
    metricsJson["snr"] = metrics.snr;
    metricsJson["timeOnAir"] = metrics.timeOnAir;
    metricsJson["packetSize"] = metrics.packetSize;
    metricsJson["retries"] = metrics.retries;
    metricsJson["success"] = metrics.success;
    metricsJson["battery"] = metrics.batteryVoltage;
}

void displayMetrics() {
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("LoRa Metrics:");
    display.printf("SF:%d CR:4/%d\n", spreadingFactor, codingRate);
    display.printf("RSSI:%.1f SNR:%.1f\n", metrics.rssi, metrics.snr);
    display.printf("ToA:%dms\n", metrics.timeOnAir);
    display.display();
}

void setup() {
    Serial.begin(115200);
    setupDisplay();
    setupLoRa();
    setupWiFi();
    setupWebServer();
}

void loop() {
    dnsServer.processNextRequest();
    
    static uint32_t lastSendTime = 0;
    static uint16_t counter = 0;
    static uint32_t lastOptimizeTime = 0;
    
    // Update simulated battery level
    batteryLevel = getBatteryVoltage() / 4.2; // Assuming 4.2V is full charge
    
    // Optimize parameters every 30 seconds
    if (millis() - lastOptimizeTime > 30000) {
        optimizeParameters(sizeof(counter));
        lastOptimizeTime = millis();
    }
    
    if (millis() - lastSendTime > 5000) {
        uint32_t startTime = millis();
        metrics.packetSize = sizeof(counter);
        metrics.retries = 0;
        
        // Send packet
        LoRa.beginPacket();
        LoRa.write((uint8_t*)&counter, sizeof(counter));
        metrics.success = LoRa.endPacket();
        
        updateMetrics(startTime);
        displayMetrics();
        
        counter++;
        lastSendTime = millis();
    }
}

void optimizeParameters(uint8_t payloadSize) {
    uint8_t new_sf, new_cr;
    long new_bw;
    int8_t new_tp;
    
    // Get optimal parameters from ML model
    if (mlHandler.getOptimalParameters(
            metrics.rssi,
            metrics.snr,
            batteryLevel,
            payloadSize,
            new_sf, new_cr, new_bw, new_tp)) {
        
        // Update parameters if they've changed
        if (new_sf != spreadingFactor || new_cr != codingRate ||
            new_bw != bandwidth || new_tp != txPower) {
            
            spreadingFactor = new_sf;
            codingRate = new_cr;
            bandwidth = new_bw;
            txPower = new_tp;
            
            updateLoRaParameters();
            
            // Log changes
            Serial.println("Parameters optimized:");
            Serial.printf("SF: %d, CR: 4/%d, BW: %ld, TP: %d\n",
                        spreadingFactor, codingRate, bandwidth, txPower);
        }
    }
}

float getBatteryVoltage() {
    // Read battery voltage from ADC
    // This is a simplified example - adjust based on your hardware
    return analogRead(36) * 2 * 3.3 / 4095.0; // Assuming voltage divider
}
