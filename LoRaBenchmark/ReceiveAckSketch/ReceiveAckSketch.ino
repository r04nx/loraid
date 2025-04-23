/*
 * LoRa Benchmarking Tool - Receiver/ACK Sketch
 * 
 * This sketch runs on a second TTGO LoRa T1 board and acts as a receiver for
 * the benchmark tests, sending acknowledgments back to the sender.
 * 
 * This is the companion sketch to the main LoRaBenchmark.ino.
 */

// Libraries
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// LoRa Pin definitions for TTGO T1
#define LORA_SCK    5    // GPIO5
#define LORA_MISO   19   // GPIO19
#define LORA_MOSI   27   // GPIO27
#define LORA_CS     18   // GPIO18
#define LORA_RST    14   // GPIO14
#define LORA_IRQ    26   // GPIO26

// OLED Display pins and settings
#define OLED_SDA    4
#define OLED_SCL    15
#define OLED_RST    16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// LED Pin
#define LED_PIN     2

// LoRa frequency - MUST MATCH THE SENDER
const long FREQUENCY = 868E6;

// OLED Display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

// Statistics
unsigned long packetsReceived = 0;
unsigned long bytesReceived = 0;
unsigned long lastPacketTime = 0;
unsigned long startTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("LoRa Receiver / ACK Sketch");
  
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  // Initialize OLED Display
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("LoRa Receiver");
    display.println("Initializing...");
    display.display();
  }
  
  // Initialize LoRa
  Serial.println("Initializing LoRa...");
  
  // Override the default pins for TTGO LoRa T1
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  // Initialize LoRa at specified frequency
  if (!LoRa.begin(FREQUENCY)) {
    Serial.println("LoRa initialization failed!");
    displayError("LoRa init failed!");
    while (1);
  }
  
  // Set LoRa parameters (should match sender)
  LoRa.setSpreadingFactor(7);  // default SF7
  LoRa.setSignalBandwidth(125E3);  // 125 kHz
  LoRa.setCodingRate4(5);  // 4/5 coding rate
  LoRa.enableCrc();
  
  Serial.println("LoRa initialized successfully");
  Serial.println("Waiting for packets...");
  
  // Put in receive mode
  LoRa.receive();
  
  // Record start time
  startTime = millis();
  
  // Update display
  updateDisplay();
}

void loop() {
  // Check if there's an incoming packet
  int packetSize = LoRa.parsePacket();
  
  if (packetSize) {
    // Turn on LED to indicate packet received
    digitalWrite(LED_PIN, LOW);
    
    // Read sequence number
    byte seqNum = LoRa.read();
    
    // Read the rest of the packet
    int dataLength = packetSize - 1;  // -1 for the sequence number
    bytesReceived += dataLength;
    packetsReceived++;
    lastPacketTime = millis();
    
    // Discard the rest of the data (we just count it)
    for (int i = 0; i < dataLength; i++) {
      LoRa.read();
    }
    
    // Log reception
    Serial.print("Received packet: seq=");
    Serial.print(seqNum);
    Serial.print(", size=");
    Serial.print(dataLength);
    Serial.print(" bytes, RSSI=");
    Serial.print(LoRa.packetRssi());
    Serial.print(", SNR=");
    Serial.println(LoRa.packetSnr());
    
    // Send acknowledgment packet
    LoRa.beginPacket();
    LoRa.write(seqNum);  // Echo back the sequence number
    LoRa.endPacket();
    
    // Update display
    updateDisplay();
    
    // Turn off LED
    digitalWrite(LED_PIN, HIGH);
  }
  
  // Blink LED to indicate system is running (if no packets for a while)
  static unsigned long previousMillis = 0;
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastPacketTime > 5000) {  // If no packet for 5 seconds
    if (currentMillis - previousMillis >= 1000) {
      previousMillis = currentMillis;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }
}

// Update OLED display with stats
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("LoRa Receiver");
  display.println("---------------");
  
  display.print("Packets: ");
  display.println(packetsReceived);
  
  display.print("Bytes: ");
  display.println(bytesReceived);
  
  // Calculate throughput
  unsigned long runTime = (lastPacketTime > 0) ? (lastPacketTime - startTime) / 1000 : 1;
  if (runTime < 1) runTime = 1;  // Avoid division by zero
  float kbps = (bytesReceived * 8.0) / (runTime * 1000.0);
  
  display.print("Runtime: ");
  display.print(runTime);
  display.println("s");
  
  display.print("Throughput: ");
  display.print(kbps, 2);
  display.println(" kbps");
  
  // Show RSSI of last packet
  display.print("RSSI: ");
  display.print(LoRa.packetRssi());
  display.println(" dBm");
  
  display.print("SNR: ");
  display.print(LoRa.packetSnr(), 1);
  display.println(" dB");
  
  display.display();
}

// Display error message
void displayError(String message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("ERROR:");
  display.println(message);
  display.display();
}
