#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// WiFi credentials
const char* ssid = "Dedsec";
const char* password = "asdfghjkl";

// LoRa pins (adjust according to your board)
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26

// Web server configuration
const char* captivePortal = "lorasender-enhanced.local";
const char* apiEndpoint = "192.168.25.237:8000";

// LoRa configuration - initial values, will be optimized
int sf = 7; // Spreading Factor
int bw = 125E3; // Bandwidth
int cr = 5; // Coding Rate
const int frequency = 915E6; // 915MHz

// EEPROM addresses for storing optimized parameters
#define EEPROM_SF_ADDR 0
#define EEPROM_BW_ADDR 4
#define EEPROM_CR_ADDR 8
#define EEPROM_INITIALIZED_ADDR 12

// Performance history for adaptive optimization
const int HISTORY_SIZE = 10;
struct PerformanceRecord {
    int sf;
    int bw;
    int cr;
    float rssi;
    float snr;
    float datarate;
    float latency;
    bool success;
};

PerformanceRecord performanceHistory[HISTORY_SIZE];
int historyIndex = 0;

// Transmission data
struct TransmissionData {
    String type;
    String data;
    int sf;
    int bw;
    int cr;
    int rssi;
    float snr;
    float delay;
    float datarate;
    float latency;
    String source;
    float compression_ratio;
};

// Web server and DNS server
WebServer server(80);
DNSServer dnsServer;

// HTML content for captive portal
const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>LoRa Enhanced Sender</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }
        .container { margin-bottom: 20px; }
        .transmission-params { margin-top: 20px; }
        .results { margin-top: 20px; }
        input { width: 100%; padding: 8px; margin: 5px 0; }
        button { padding: 10px 20px; background: #4CAF50; color: white; border: none; cursor: pointer; }
        button:hover { background: #45a049; }
        table { width: 100%; border-collapse: collapse; margin-top: 20px; }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        th { background-color: #f4f4f4; }
        .enhanced { background-color: #e8f5e9; }
    </style>
</head>
<body>
    <h1>LoRa Sender Control Panel (Enhanced)</h1>
    
    <div class="container enhanced">
        <h2>Smart Framework Features</h2>
        <ul>
            <li>Adaptive parameter selection based on transmission history</li>
            <li>Data compression based on content type</li>
            <li>Optimized for reliability and speed</li>
        </ul>
    </div>
    
    <div class="container">
        <h2>Send Data</h2>
        <form id="dataForm">
            <select name="dataType" required>
                <option value="text">Text</option>
                <option value="image">Image URL</option>
                <option value="audio">Audio URL</option>
                <option value="repeat">Text Repeat</option>
                <option value="random">Random Data</option>
                <option value="ones">All Ones</option>
                <option value="zeros">All Zeros</option>
            </select>
            <br>
            <input type="text" name="data" placeholder="Enter data or URL" required>
            <br>
            <input type="number" name="length" placeholder="Length (for repeat/random)" min="1">
            <br>
            <button type="submit">Send Data</button>
        </form>
    </div>

    <div class="transmission-params">
        <h2>Current Transmission Parameters</h2>
        <table>
            <tr><th>Parameter</th><th>Value</th><th>Status</th></tr>
            <tr><td>Spreading Factor (SF)</td><td id="sf">-</td><td id="sf-status">-</td></tr>
            <tr><td>Bandwidth (BW)</td><td id="bw">-</td><td id="bw-status">-</td></tr>
            <tr><td>Coding Rate (CR)</td><td id="cr">-</td><td id="cr-status">-</td></tr>
            <tr><td>Frequency</td><td id="freq">-</td><td>Fixed</td></tr>
        </table>
    </div>

    <div class="results">
        <h2>Transmission Results</h2>
        <table>
            <tr><th>Parameter</th><th>Value</th></tr>
            <tr><td>RSSI</td><td id="rssi">-</td></tr>
            <tr><td>SNR</td><td id="snr">-</td></tr>
            <tr><td>Delay</td><td id="delay">-</td></tr>
            <tr><td>Data Rate</td><td id="datarate">-</td></tr>
            <tr><td>Compression Ratio</td><td id="compression">-</td></tr>
            <tr><td>Latency</td><td id="latency">-</td></tr>
        </table>
    </div>

    <script>
        document.getElementById('dataForm').onsubmit = async function(e) {
            e.preventDefault();
            const formData = new FormData(this);
            const response = await fetch('/send', {
                method: 'POST',
                body: formData
            });
            const result = await response.json();
            updateResults(result);
        };

        function updateResults(data) {
            document.getElementById('sf').textContent = data.sf;
            document.getElementById('bw').textContent = data.bw;
            document.getElementById('cr').textContent = data.cr;
            document.getElementById('freq').textContent = data.frequency;
            document.getElementById('rssi').textContent = data.rssi;
            document.getElementById('snr').textContent = data.snr;
            document.getElementById('delay').textContent = data.delay + ' ms';
            document.getElementById('datarate').textContent = data.datarate + ' bps';
            document.getElementById('compression').textContent = data.compressionRatio.toFixed(2) + 'x';
            document.getElementById('latency').textContent = data.latency + ' ms';
            
            // Update status indicators
            document.getElementById('sf-status').textContent = data.sfOptimal ? 'Optimal' : 'Adapting';
            document.getElementById('bw-status').textContent = data.bwOptimal ? 'Optimal' : 'Adapting';
            document.getElementById('cr-status').textContent = data.crOptimal ? 'Optimal' : 'Adapting';
        }
    </script>
</body>
</html>
)rawliteral";

// Function to compress data based on its type and content
String compressData(String dataType, String data) {
    if (dataType == "text" || dataType.startsWith("TXT:")) {
        // Simple run-length encoding for text
        String compressed = "";
        int count = 1;
        for (int i = 0; i < data.length(); i++) {
            if (i + 1 < data.length() && data[i] == data[i + 1]) {
                count++;
            } else {
                if (count > 3) {
                    compressed += String(count) + data[i];
                } else {
                    for (int j = 0; j < count; j++) {
                        compressed += data[i];
                    }
                }
                count = 1;
            }
        }
        return compressed;
    } else if (dataType == "ones" || dataType.startsWith("ONES:")) {
        // Highly compressible
        int length = data.length() - 5; // Remove "ONES:" prefix
        return "ONES:" + String(length);
    } else if (dataType == "zeros" || dataType.startsWith("ZEROS:")) {
        // Highly compressible
        int length = data.length() - 6; // Remove "ZEROS:" prefix
        return "ZEROS:" + String(length);
    } else if (dataType == "random" || dataType.startsWith("RAND:")) {
        // Not compressible
        return data;
    } else {
        // Default, no compression
        return data;
    }
}

// Function to calculate compression ratio
float getCompressionRatio(String original, String compressed) {
    if (compressed.length() == 0) return 1.0;
    return (float)original.length() / compressed.length();
}

// Function to adaptively select optimal LoRa parameters based on history
void optimizeLoRaParameters() {
    // Only optimize if we have enough history
    if (historyIndex < HISTORY_SIZE / 2) return;
    
    // Calculate success rate for each SF, BW, CR combination used
    int bestSF = sf;
    int bestBW = bw;
    int bestCR = cr;
    float bestScore = 0;
    
    // Simple scoring: datarate * success_rate / latency
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (performanceHistory[i].success) {
            float score = performanceHistory[i].datarate / max(1.0f, performanceHistory[i].latency);
            if (score > bestScore) {
                bestScore = score;
                bestSF = performanceHistory[i].sf;
                bestBW = performanceHistory[i].bw;
                bestCR = performanceHistory[i].cr;
            }
        }
    }
    
    // Update parameters if better ones found
    if (bestScore > 0) {
        sf = bestSF;
        bw = bestBW;
        cr = bestCR;
        
        // Apply new parameters
        LoRa.setSpreadingFactor(sf);
        LoRa.setSignalBandwidth(bw);
        LoRa.setCodingRate4(cr);
        
        // Save to EEPROM
        EEPROM.put(EEPROM_SF_ADDR, sf);
        EEPROM.put(EEPROM_BW_ADDR, bw);
        EEPROM.put(EEPROM_CR_ADDR, cr);
        EEPROM.commit();
        
        Serial.println("Optimized parameters: SF" + String(sf) + ", BW:" + String(bw) + ", CR:" + String(cr));
    }
}

// Function to occasionally try different parameters to explore better options
void exploreParameters() {
    // 10% chance to explore new parameters
    if (random(100) < 10) {
        int exploreSF = sf;
        int exploreBW = bw;
        int exploreCR = cr;
        
        // Try different SF (7-12)
        if (random(100) < 33) {
            exploreSF = random(7, 13);
        }
        
        // Try different BW (125, 250, 500 kHz)
        if (random(100) < 33) {
            int bwOptions[] = {125E3, 250E3, 500E3};
            exploreBW = bwOptions[random(3)];
        }
        
        // Try different CR (5-8, which maps to 4/5 to 4/8)
        if (random(100) < 33) {
            exploreCR = random(5, 9);
        }
        
        // Apply exploration parameters temporarily
        LoRa.setSpreadingFactor(exploreSF);
        LoRa.setSignalBandwidth(exploreBW);
        LoRa.setCodingRate4(exploreCR);
        
        Serial.println("Exploring parameters: SF" + String(exploreSF) + ", BW:" + String(exploreBW) + ", CR:" + String(exploreCR));
        
        // Store current exploration parameters
        sf = exploreSF;
        bw = exploreBW;
        cr = exploreCR;
    }
}

void handleRoot() {
    server.send(200, "text/html", html);
}

void handleSend() {
    String dataType = server.arg("dataType");
    String data = server.arg("data");
    int length = server.arg("length").toInt();
    
    // Prepare transmission data based on type
    String payload;
    if (dataType == "image") {
        payload = "IMG:" + data;
    } else if (dataType == "audio") {
        payload = "AUD:" + data;
    } else if (dataType == "repeat") {
        payload = "";
        for (int i = 0; i < length; i++) {
            payload += data;
        }
    } else if (dataType == "random") {
        payload = "RAND:";
        for (int i = 0; i < length; i++) {
            payload += String(random(256), HEX);
        }
    } else if (dataType == "ones") {
        payload = "ONES:";
        for (int i = 0; i < length; i++) {
            payload += "1";
        }
    } else if (dataType == "zeros") {
        payload = "ZEROS:";
        for (int i = 0; i < length; i++) {
            payload += "0";
        }
    } else {
        payload = "TXT:" + data;
    }

    // Compress data
    String originalPayload = payload;
    String compressedPayload = compressData(dataType, payload);
    float compressionRatio = getCompressionRatio(originalPayload, compressedPayload);
    
    // Occasionally explore new parameters
    exploreParameters();
    
    // Send via LoRa with enhanced tag
    unsigned long startTime = millis();
    LoRa.beginPacket();
    LoRa.print("ENHANCED:" + compressedPayload);
    LoRa.endPacket();
    
    // Wait for ACK
    unsigned long ackTime = millis();
    String ackData = "";
    bool ackReceived = false;
    
    while (millis() - ackTime < 2000) {
        if (LoRa.parsePacket()) {
            String ack = LoRa.readString();
            if (ack.startsWith("ENHANCED_ACK:")) {
                ackData = ack.substring(13); // Remove the ENHANCED_ACK: prefix
                ackReceived = true;
                break;
            }
        }
    }
    
    // Calculate metrics
    TransmissionData result;
    result.type = dataType;
    result.data = compressedPayload;
    result.sf = sf;
    result.bw = bw;
    result.cr = cr;
    result.source = "enhanced";
    result.compression_ratio = compressionRatio;
    
    if (ackReceived) {
        // Parse ACK JSON data
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, ackData);
        
        result.rssi = doc["rssi"];
        result.snr = doc["snr"];
        unsigned long ackTimestamp = doc["timestamp"];
        result.delay = (millis() - startTime);
        result.datarate = (originalPayload.length() * 8) / (result.delay / 1000.0); // Use original length for true data rate
        result.latency = result.delay;
        
        // Record successful transmission in history
        performanceHistory[historyIndex].sf = sf;
        performanceHistory[historyIndex].bw = bw;
        performanceHistory[historyIndex].cr = cr;
        performanceHistory[historyIndex].rssi = result.rssi;
        performanceHistory[historyIndex].snr = result.snr;
        performanceHistory[historyIndex].datarate = result.datarate;
        performanceHistory[historyIndex].latency = result.latency;
        performanceHistory[historyIndex].success = true;
        
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;
        
        // Periodically optimize parameters
        if (historyIndex == 0) {
            optimizeLoRaParameters();
        }
    } else {
        // If no ACK received, use local measurements and mark as failed
        result.rssi = -120; // Very poor signal as placeholder
        result.snr = 0;
        result.delay = 2000; // Timeout value
        result.datarate = 0; // Failed transmission
        result.latency = 2000;
        
        // Record failed transmission in history
        performanceHistory[historyIndex].sf = sf;
        performanceHistory[historyIndex].bw = bw;
        performanceHistory[historyIndex].cr = cr;
        performanceHistory[historyIndex].rssi = result.rssi;
        performanceHistory[historyIndex].snr = result.snr;
        performanceHistory[historyIndex].datarate = result.datarate;
        performanceHistory[historyIndex].latency = result.latency;
        performanceHistory[historyIndex].success = false;
        
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    }

    // Send results to API
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(String("http://") + apiEndpoint + "/api/transmission");
        http.addHeader("Content-Type", "application/json");
        String json = "{";
        json += "\"type\":\"" + dataType + "\",";
        json += "\"data\":\"" + compressedPayload + "\",";
        json += "\"sf\":" + String(sf) + ",";
        json += "\"bw\":" + String(bw) + ",";
        json += "\"cr\":" + String(cr) + ",";
        json += "\"rssi\":" + String(result.rssi) + ",";
        json += "\"snr\":" + String(result.snr) + ",";
        json += "\"delay\":" + String(result.delay) + ",";
        json += "\"datarate\":" + String(result.datarate) + ",";
        json += "\"latency\":" + String(result.latency) + ",";
        json += "\"source\":\"enhanced\",";
        json += "\"compressionRatio\":" + String(compressionRatio);
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

    // Add optimization status for UI
    DynamicJsonDocument responseDoc(1024);
    responseDoc["sf"] = sf;
    responseDoc["bw"] = bw;
    responseDoc["cr"] = cr;
    responseDoc["frequency"] = frequency;
    responseDoc["rssi"] = result.rssi;
    responseDoc["snr"] = result.snr;
    responseDoc["delay"] = result.delay;
    responseDoc["datarate"] = result.datarate;
    responseDoc["compressionRatio"] = compressionRatio;
    responseDoc["latency"] = result.latency;
    responseDoc["sfOptimal"] = (historyIndex > HISTORY_SIZE / 2); // Consider optimal after half history filled
    responseDoc["bwOptimal"] = (historyIndex > HISTORY_SIZE / 2);
    responseDoc["crOptimal"] = (historyIndex > HISTORY_SIZE / 2);
    
    String responseJson;
    serializeJson(responseDoc, responseJson);
    
    // Send response to web interface
    server.send(200, "application/json", responseJson);
}

void setup() {
    Serial.begin(115200);
    
    // Initialize EEPROM
    EEPROM.begin(512);
    
    // Check if EEPROM has been initialized
    int initialized;
    EEPROM.get(EEPROM_INITIALIZED_ADDR, initialized);
    
    if (initialized == 0xABCD) {
        // Load parameters from EEPROM
        EEPROM.get(EEPROM_SF_ADDR, sf);
        EEPROM.get(EEPROM_BW_ADDR, bw);
        EEPROM.get(EEPROM_CR_ADDR, cr);
        Serial.println("Loaded parameters from EEPROM: SF" + String(sf) + ", BW:" + String(bw) + ", CR:" + String(cr));
    } else {
        // Initialize EEPROM with default values
        EEPROM.put(EEPROM_SF_ADDR, sf);
        EEPROM.put(EEPROM_BW_ADDR, bw);
        EEPROM.put(EEPROM_CR_ADDR, cr);
        EEPROM.put(EEPROM_INITIALIZED_ADDR, 0xABCD);
        EEPROM.commit();
        Serial.println("Initialized EEPROM with default parameters");
    }
    
    // Initialize performance history
    for (int i = 0; i < HISTORY_SIZE; i++) {
        performanceHistory[i].success = false;
    }
    
    // Initialize WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
    Serial.println("IP address: " + WiFi.localIP().toString());
    
    // Start DNS server for captive portal
    dnsServer.start(53, "*", WiFi.localIP());
    
    // Initialize LoRa
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(frequency)) {
        Serial.println("LoRa initialization failed!");
        while (1);
    }
    
    // Configure LoRa with current parameters
    LoRa.setSpreadingFactor(sf);
    LoRa.setSignalBandwidth(bw);
    LoRa.setCodingRate4(cr);
    
    // Set up web server
    server.on("/", handleRoot);
    server.on("/send", HTTP_POST, handleSend);
    server.begin();
    
    Serial.println("Enhanced Sender initialized with Smart Framework");
    Serial.println("Current parameters: SF" + String(sf) + ", BW:" + String(bw) + ", CR:" + String(cr));
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Handle serial commands
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        if (command.startsWith("send:")) {
            String data = command.substring(5);
            String compressedData = compressData("text", data);
            float ratio = getCompressionRatio(data, compressedData);
            
            Serial.println("Original: " + data);
            Serial.println("Compressed: " + compressedData);
            Serial.println("Compression ratio: " + String(ratio));
            
            LoRa.beginPacket();
            LoRa.print("ENHANCED:" + compressedData);
            LoRa.endPacket();
            Serial.println("Sent enhanced data");
        } else if (command == "status") {
            Serial.println("Current parameters: SF" + String(sf) + ", BW:" + String(bw) + ", CR:" + String(cr));
        }
    }
}