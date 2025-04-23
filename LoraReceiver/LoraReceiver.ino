/*
 * LoRa Receiver Node for Benchmarking Tool
 * Hardware: TTGO LoRa T1 (ESP32 + SX1276)
 * 
 * This sketch acts as the receiver node for the LoRa benchmarking tool,
 * receiving packets and sending acknowledgments back to the sender.
 * It also displays statistics on the OLED display.
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pin definitions from the diagram
#define LORA_SCK    5    // GPIO5
#define LORA_MISO   19   // GPIO19
#define LORA_MOSI   27   // GPIO27
#define LORA_CS     18   // GPIO18
#define LORA_RST    14   // GPIO14
#define LORA_IRQ    26   // GPIO26

// OLED Display pins
#define OLED_SDA    21   // GPIO21 (SDA)
#define OLED_SCL    22   // GPIO22 (SCL)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1   // Reset pin not used

// LED Pin
#define LED_PIN     2

// LoRa frequency - MUST MATCH THE SENDER
const long FREQUENCY = 868E6;  // 868MHz for Europe

// OLED Display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Statistics
unsigned long packetsReceived = 0;
unsigned long bytesReceived = 0;
unsigned long lastPacketTime = 0;
unsigned long startTime = 0;
float lastRssi = 0;
float lastSnr = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("LoRa Receiver Node Starting");

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // Initialize OLED Display
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }
  
  // Show initial display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("LoRa Receiver");
  display.println("Initializing...");
  display.display();

  // Initialize LoRa
  Serial.println("Initializing LoRa...");
  
  // Initialize SPI for LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  if (!LoRa.begin(FREQUENCY)) {
    Serial.println("LoRa init failed!");
    displayError("LoRa init failed!");
    while (1);
  }

  // Set LoRa parameters (matching sender)
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  
  Serial.println("LoRa initialized successfully!");
  startTime = millis();
  updateDisplay();
}

void loop() {
  int packetSize = LoRa.parsePacket();
  
  if (packetSize) {
    // Packet received - turn on LED
    digitalWrite(LED_PIN, LOW);
    
    // Read sequence number
    byte seqNum = LoRa.read();
    
    // Read packet data
    int dataLength = packetSize - 1; // Subtract sequence number byte
    bytesReceived += dataLength;
    packetsReceived++;
    lastPacketTime = millis();
    lastRssi = LoRa.packetRssi();
    lastSnr = LoRa.packetSnr();
    
    // Read and discard remaining data
    while (LoRa.available()) {
      LoRa.read();
    }
    
    // Send acknowledgment
    LoRa.beginPacket();
    LoRa.write(seqNum);
    LoRa.endPacket();
    
    // Log packet info
    Serial.printf("Packet: #%d, Size: %d bytes, RSSI: %.1f dBm, SNR: %.1f dB\n",
                 seqNum, dataLength, lastRssi, lastSnr);
    
    updateDisplay();
    digitalWrite(LED_PIN, HIGH);
  }
  
  // Blink LED if no packets received for a while
  if (millis() - lastPacketTime > 5000) {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink >= 1000) {
      lastBlink = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("LoRa Receiver");
  display.println("-------------");
  
  // Show packet stats
  display.printf("Packets: %lu\n", packetsReceived);
  display.printf("Bytes: %lu\n", bytesReceived);
  
  // Calculate and show throughput
  float runTime = (lastPacketTime - startTime) / 1000.0;
  if (runTime < 1) runTime = 1;
  float kbps = (bytesReceived * 8.0) / (runTime * 1000.0);
  display.printf("Rate: %.2f kbps\n", kbps);
  
  // Show last packet info
  display.printf("RSSI: %.1f dBm\n", lastRssi);
  display.printf("SNR: %.1f dB\n", lastSnr);
  
  display.display();
}

void displayError(const char* message) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("ERROR:");
  display.println(message);
  display.display();
}
