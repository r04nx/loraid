#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>

// LoRa pins for TTGO LoRa32 V2.1
#define SCK     5    // GPIO5  -- SX1278's SCK
#define MISO    19   // GPIO19 -- SX1278's MISO
#define MOSI    27   // GPIO27 -- SX1278's MOSI
#define SS      18   // GPIO18 -- SX1278's CS
#define RST     14   // GPIO14 -- SX1278's RESET
#define DI0     26   // GPIO26 -- SX1278's IRQ(Interrupt Request)

// Default LoRa parameters
int sf = 7;
int bw = 125E3;
int cr = 5;
int preambleLength = 8;
int syncWord = 0x12;
int power = 20;       // dBm

// Structure to hold reception metrics
struct ReceptionMetrics {
  String source;      // "standard" or "enhanced"
  String data;        // The received data
  float rssi;         // Signal strength
  float snr;          // Signal-to-noise ratio
  float freqError;    // Frequency error
  int packetSize;     // Size of the packet
  unsigned long receiveTime; // Timestamp when the packet was received
};

// Structure to hold parameter history
struct ParameterHistory {
  float rssi;
  float snr;
  int sf;
  int bw;
  int cr;
  bool success;
  unsigned long timestamp;
};

// Keep track of recent signal metrics for optimization
#define HISTORY_SIZE 10
ParameterHistory signalHistory[HISTORY_SIZE];
int historyIndex = 0;

// Timeout for parameter reset (ms)
#define PARAM_RESET_TIMEOUT 30000
unsigned long lastParameterChange = 0;
bool usingCustomParameters = false;

// SNR thresholds for different SF values
float snrThresholds[] = {-7.5, -10, -12.5, -15, -17.5, -20}; // For SF7-SF12

void setup() {
    Serial.begin(115200);
    Serial.println("Starting LoRa Receiver...");
    
    // Initialize LoRa
    LoRa.setPins(SS, RST, DI0);
    if (!LoRa.begin(868E6)) {
        Serial.println("LoRa initialization failed!");
        while (1);
    }
    
    // Set initial LoRa parameters
    LoRa.setSpreadingFactor(sf);
    LoRa.setSignalBandwidth(bw);
    LoRa.setCodingRate4(cr);
    
    Serial.println("LoRa initialized successfully at 868MHz");
    Serial.println("Receiver ready for packets");
}

void loop() {
    if (LoRa.parsePacket()) {
        unsigned long receiveTime = millis();
        String received = LoRa.readString();
        Serial.println("Received: " + received);
        
        // Use global variables for current parameters
        int currentSf = sf;
        int currentBw = bw;
        int currentCr = cr;
        
        String source = "standard";
        if (received.startsWith("ENHANCED:")) {
            source = "enhanced";
            received = received.substring(9);
            
            int packetSf = currentSf;
            int packetBw = currentBw;
            int packetCr = currentCr;
            
            if (extractLoRaParameters(received, packetSf, packetBw, packetCr)) {
                // Validate parameters
                if (packetSf >= 7 && packetSf <= 12 && 
                    (packetBw == 125E3 || packetBw == 250E3 || packetBw == 500E3) && 
                    packetCr >= 5 && packetCr <= 8) {
                    LoRa.setSpreadingFactor(packetSf);
                    LoRa.setSignalBandwidth(packetBw);
                    LoRa.setCodingRate4(packetCr);
                    // Update global variables to track current settings
                    sf = packetSf;
                    bw = packetBw;
                    cr = packetCr;
                }
            }
        }
        
        ReceptionMetrics metrics;
        metrics.rssi = LoRa.packetRssi();
        metrics.snr = LoRa.packetSnr();
        metrics.receiveTime = receiveTime;
        metrics.source = source;
        metrics.data = received;
        
        sendAcknowledgement(metrics);
        
        // Reset to default parameters
        LoRa.setSpreadingFactor(7);
        LoRa.setSignalBandwidth(125E3);
        LoRa.setCodingRate4(5);
        // Update global variables to track current settings
        sf = 7;
        bw = 125E3;
        cr = 5;
        LoRa.receive();
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

// Calculate link budget based on RSSI and receiver sensitivity
float calculateLinkBudget(float rssi, int sf, int bw) {
    // Base sensitivity for SF7, BW 125kHz
    float baseSensitivity = -123.0;
    
    // Sensitivity improves by ~2.5dB per SF step
    float sfAdjustment = (sf - 7) * 2.5;
    
    // Sensitivity changes by 3dB when BW doubles
    float bwAdjustment = 10 * log10((float)bw / 125000.0);
    
    // Calculate adjusted sensitivity
    float sensitivity = baseSensitivity - sfAdjustment + bwAdjustment;
    
    // Return link margin
    return rssi - sensitivity;
}

// Calculate time on air (transmission time) in milliseconds
float calculateTimeOnAir(int sf, int cr, int bw, int payloadSize) {
    // Number of symbols in preamble
    int nPreamble = 8;
    
    // Calculate symbol duration (ms)
    float tSym = (pow(2, sf) / (bw / 1000.0)) * 1000;
    
    // Calculate number of payload symbols (simplified formula)
    int nPayload = 8 + max(ceil((8.0 * payloadSize - 4.0 * sf + 28) / (4.0 * (sf - 2))) * (cr + 4), 0.0);
    
    // Calculate time on air
    float tPacket = (nPreamble + 4.25) * tSym + nPayload * tSym;
    
    return tPacket;
}

// Calculate data rate in bits per second
float calculateDataRate(int sf, int cr, int bw) {
    // Raw bit rate calculation
    float rb = sf * ((bw / pow(2, sf)));
    
    // Effective bit rate with coding rate
    return rb * (4.0 / cr);
}

// Calculate reliability score based on SNR, SF and CR
float getReliabilityScore(float snr, int sf, int cr) {
    // SNR threshold for different SF values (SF7-SF12)
    float snrThreshold = -7.5 - ((sf - 7) * 2.5);
    
    // SNR margin
    float snrMargin = snr - snrThreshold;
    
    // CR factor (higher CR = more reliability)
    float crFactor = (cr - 4.0) / 4.0;
    
    // Combine factors (limited to range 0-1)
    float reliability = min(max((snrMargin / 10.0) + crFactor, 0.0), 1.0);
    
    return reliability;
}

// Find optimal parameters based on signal metrics
void optimizeParameters(float rssi, float snr, int payloadSize, int& optSf, int& optBw, int& optCr) {
    float bestScore = -1000.0;
    
    // Default parameters if optimization fails
    optSf = 7;
    optBw = 125E3;
    optCr = 5;
    
    // Try different parameter combinations
    for (int sf = 7; sf <= 12; sf++) {
        // Skip SF values that require better SNR than available
        if (snr < snrThresholds[sf-7]) {
            continue;
        }
        
        for (int crIndex = 0; crIndex < 4; crIndex++) {
            int cr = 5 + crIndex; // CR 5-8 (4/5 to 4/8)
            
            for (int bwIndex = 0; bwIndex < 3; bwIndex++) {
                int bw = (bwIndex == 0) ? 125E3 : (bwIndex == 1) ? 250E3 : 500E3;
                
                // Calculate metrics for this parameter set
                float linkMargin = calculateLinkBudget(rssi, sf, bw);
                float reliability = getReliabilityScore(snr, sf, cr);
                float dataRate = calculateDataRate(sf, cr, bw);
                float timeOnAir = calculateTimeOnAir(sf, cr, bw, payloadSize);
                
                // Skip if link margin is too small
                if (linkMargin < 0) {
                    continue;
                }
                
                // Calculate score - balance between reliability and data rate
                // Prioritize reliability when signal is weak
                float reliabilityWeight = (rssi < -100 || snr < 5) ? 0.8 : 0.5;
                float score;
                
                if (reliability < 0.7) { // Minimum reliability threshold
                    continue;
                }
                
                score = (reliability * reliabilityWeight) + 
                        (dataRate / 10000.0 * (1 - reliabilityWeight)) - 
                        (timeOnAir / 10000.0); // Penalize long airtime
                
                if (score > bestScore) {
                    bestScore = score;
                    optSf = sf;
                    optBw = bw;
                    optCr = cr;
                }
            }
        }
    }
    
    // Add to history
    signalHistory[historyIndex].rssi = rssi;
    signalHistory[historyIndex].snr = snr;
    signalHistory[historyIndex].sf = optSf;
    signalHistory[historyIndex].bw = optBw;
    signalHistory[historyIndex].cr = optCr;
    signalHistory[historyIndex].timestamp = millis();
    
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    
    Serial.println("Optimized parameters: SF" + String(optSf) + ", BW" + String(optBw) + ", CR" + String(optCr));
}

// Adaptive optimization based on historical signal metrics
void adaptiveOptimize(float rssi, float snr, int payloadSize, int& optSf, int& optBw, int& optCr) {
    // Calculate average and trend of recent metrics
    float avgRssi = 0;
    float avgSnr = 0;
    int count = 0;
    
    // Use up to last 5 entries in history
    for (int i = 0; i < min(5, HISTORY_SIZE); i++) {
        // Only use entries from the last 60 seconds
        if (millis() - signalHistory[i].timestamp < 60000) {
            avgRssi += signalHistory[i].rssi;
            avgSnr += signalHistory[i].snr;
            count++;
        }
    }
    
    // If we have historical data, use it to adjust current readings
    if (count > 0) {
        avgRssi /= count;
        avgSnr /= count;
        
        // Add margin if signal is unstable
        float rssiDiff = abs(rssi - avgRssi);
        float snrDiff = abs(snr - avgSnr);
        
        // If signal is unstable, be more conservative
        if (rssiDiff > 10 || snrDiff > 3) {
            rssi = min(rssi, avgRssi) - 5; // Be pessimistic
            snr = min(snr, avgSnr) - 2;    // Be pessimistic
        }
    }
    
    // Call regular optimization with adjusted values
    optimizeParameters(rssi, snr, payloadSize, optSf, optBw, optCr);
}

void sendAcknowledgement(ReceptionMetrics metrics) {
    // Calculate optimal parameters based on signal quality
    int optSf = 7;
    int optBw = 125E3;
    int optCr = 5;
    
    if (metrics.source == "enhanced") {
        adaptiveOptimize(metrics.rssi, metrics.snr, metrics.data.length(), optSf, optBw, optCr);
    }
    
    // Build ACK message with metrics and optimal parameters
    String ackPrefix = metrics.source == "enhanced" ? "ENHANCED_ACK:" : "STANDARD_ACK:";
    String ack = ackPrefix + "{";
    ack += "\"rssi\":" + String(metrics.rssi) + ",";
    ack += "\"snr\":" + String(metrics.snr) + ",";
    
    if (metrics.source == "enhanced") {
        ack += "\"opt_sf\":" + String(optSf) + ",";
        ack += "\"opt_bw\":" + String(optBw) + ",";
        ack += "\"opt_cr\":" + String(optCr) + ",";
    }
    
    ack += "\"timestamp\":" + String(millis()) + ",";
    ack += "\"data\":\"" + metrics.data + "\"";
    ack += "}";
    
    // Send ACK
    LoRa.beginPacket();
    LoRa.print(ack);
    LoRa.endPacket();
    
    Serial.println("Sent ACK: " + ack);
    
    // Ensure we're in receive mode after sending
    LoRa.receive();
}

// WiFi and API functionality removed