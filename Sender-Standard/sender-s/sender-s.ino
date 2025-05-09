#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "Dedsec";
const char* password = "asdfghjkl";

// LoRa pins (adjust according to your board)
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26

// Web server configuration
const char* captivePortal = "lorasender.local";
const char* apiEndpoint = "192.168.25.237:8000";

// LoRa configuration
const int frequency = 915E6; // 915MHz
const int sf = 7; // Spreading Factor
const int bw = 125E3; // Bandwidth
const int cr = 5; // Coding Rate

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
    <title>LoRa Sender Control Panel</title>
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
    </style>
</head>
<body>
    <h1>LoRa Sender Control Panel (Standard)</h1>
    
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
        <h2>Transmission Parameters</h2>
        <table>
            <tr><th>Parameter</th><th>Value</th></tr>
            <tr><td>Spreading Factor (SF)</td><td id="sf">-</td></tr>
            <tr><td>Bandwidth (BW)</td><td id="bw">-</td></tr>
            <tr><td>Coding Rate (CR)</td><td id="cr">-</td></tr>
            <tr><td>Frequency</td><td id="freq">-</td></tr>
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
            document.getElementById('compression').textContent = (data.compressionRatio || 1.0).toFixed(2) + 'x';
            document.getElementById('latency').textContent = data.latency + ' ms';
        }
    </script>
</body>
</html>
)rawliteral";

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

    // Send via LoRa with standard tag
    unsigned long startTime = millis();
    LoRa.beginPacket();
    LoRa.print("STANDARD:" + payload);
    LoRa.endPacket();
    
    // Wait for ACK
    unsigned long ackTime = millis();
    String ackData = "";
    bool ackReceived = false;
    
    while (millis() - ackTime < 2000) {
        if (LoRa.parsePacket()) {
            String ack = LoRa.readString();
            if (ack.startsWith("STANDARD_ACK:")) {
                ackData = ack.substring(13); // Remove the STANDARD_ACK: prefix
                ackReceived = true;
                break;
            }
        }
    }
    
    // Calculate metrics
    TransmissionData result;
    result.type = dataType;
    result.data = payload;
    result.sf = sf;
    result.bw = bw;
    result.cr = cr;
    result.source = "standard";
    result.compression_ratio = 1.0; // Standard sender doesn't use compression
    
    if (ackReceived) {
        // Parse ACK JSON data
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, ackData);
        
        result.rssi = doc["rssi"];
        result.snr = doc["snr"];
        unsigned long ackTimestamp = doc["timestamp"];
        result.delay = (millis() - startTime);
        result.datarate = (payload.length() * 8) / (result.delay / 1000.0);
        result.latency = result.delay;
    } else {
        // If no ACK received, use local measurements
        result.rssi = -120; // Very poor signal as placeholder
        result.snr = 0;
        result.delay = 2000; // Timeout value
        result.datarate = 0; // Failed transmission
        result.latency = 2000;
    }
    
    // Prepare JSON response for both API and web interface
    String json = "{";
    json += "\"type\":\"" + dataType + "\",";
    json += "\"data\":\"" + payload + "\",";
    json += "\"sf\":" + String(sf) + ",";
    json += "\"bw\":" + String(bw) + ",";
    json += "\"cr\":" + String(cr) + ",";
    json += "\"rssi\":" + String(result.rssi) + ",";
    json += "\"snr\":" + String(result.snr) + ",";
    json += "\"delay\":" + String(result.delay) + ",";
    json += "\"datarate\":" + String(result.datarate) + ",";
    json += "\"latency\":" + String(result.latency) + ",";
    json += "\"source\":\"standard\",";
    json += "\"compressionRatio\":1.0";
    json += "}";

    // Send results to API
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(String("http://") + apiEndpoint + "/api/transmission");
        http.addHeader("Content-Type", "application/json");
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

    // Send response to web interface
    server.send(200, "application/json", json);
}

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
    
    // Start DNS server for captive portal
    dnsServer.start(53, "*", WiFi.localIP());
    
    // Initialize LoRa
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    if (!LoRa.begin(frequency)) {
        Serial.println("LoRa initialization failed!");
        while (1);
    }
    
    // Configure LoRa
    LoRa.setSpreadingFactor(sf);
    LoRa.setSignalBandwidth(bw);
    LoRa.setCodingRate4(cr);
    
    // Set up web server
    server.on("/", handleRoot);
    server.on("/send", HTTP_POST, handleSend);
    server.begin();
    
    Serial.println("Standard Sender initialized");
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Handle serial commands
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        if (command.startsWith("send:")) {
            String data = command.substring(5);
            LoRa.beginPacket();
            LoRa.print("STANDARD:" + data);
            LoRa.endPacket();
            Serial.println("Sent: " + data);
        }
    }
}