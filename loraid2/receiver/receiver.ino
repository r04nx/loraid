#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// TTGO T1 LoRa pins
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26

// Circular buffer for performance metrics
#define METRICS_BUFFER_SIZE 100
struct PacketMetrics {
    float rssi;
    float snr;
    uint32_t timestamp;
    uint16_t counter;
    bool valid;
} metricsBuffer[METRICS_BUFFER_SIZE];

int bufferIndex = 0;
uint32_t totalPackets = 0;
uint32_t validPackets = 0;

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
    
    // Match sender's initial parameters
    LoRa.setSpreadingFactor(7);
    LoRa.setCodingRate4(5);
    LoRa.setSignalBandwidth(125E3);
}

void updateMetrics(uint16_t counter) {
    metricsBuffer[bufferIndex].rssi = LoRa.packetRssi();
    metricsBuffer[bufferIndex].snr = LoRa.packetSnr();
    metricsBuffer[bufferIndex].timestamp = millis();
    metricsBuffer[bufferIndex].counter = counter;
    metricsBuffer[bufferIndex].valid = true;
    
    bufferIndex = (bufferIndex + 1) % METRICS_BUFFER_SIZE;
    totalPackets++;
    validPackets++;
}

void displayMetrics() {
    float avgRssi = 0;
    float avgSnr = 0;
    int validCount = 0;
    
    // Calculate averages from buffer
    for(int i = 0; i < METRICS_BUFFER_SIZE; i++) {
        if(metricsBuffer[i].valid) {
            avgRssi += metricsBuffer[i].rssi;
            avgSnr += metricsBuffer[i].snr;
            validCount++;
        }
    }
    
    if(validCount > 0) {
        avgRssi /= validCount;
        avgSnr /= validCount;
    }
    
    // Update display
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("LoRa Receiver Stats:");
    display.printf("Packets: %d/%d\n", validPackets, totalPackets);
    display.printf("Avg RSSI: %.1f dBm\n", avgRssi);
    display.printf("Avg SNR: %.1f dB\n", avgSnr);
    display.printf("PDR: %.1f%%\n", (validPackets * 100.0) / totalPackets);
    display.display();
}

void setup() {
    Serial.begin(115200);
    setupDisplay();
    setupLoRa();
    
    // Initialize metrics buffer
    for(int i = 0; i < METRICS_BUFFER_SIZE; i++) {
        metricsBuffer[i].valid = false;
    }
}

void loop() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        uint16_t counter;
        if(LoRa.readBytes((uint8_t*)&counter, sizeof(counter)) == sizeof(counter)) {
            updateMetrics(counter);
            displayMetrics();
            
            // Output JSON format for serial monitoring
            StaticJsonDocument<256> doc;
            doc["counter"] = counter;
            doc["rssi"] = LoRa.packetRssi();
            doc["snr"] = LoRa.packetSnr();
            doc["pdr"] = (validPackets * 100.0) / totalPackets;
            
            String jsonString;
            serializeJson(doc, jsonString);
            Serial.println(jsonString);
        }
    }
} 