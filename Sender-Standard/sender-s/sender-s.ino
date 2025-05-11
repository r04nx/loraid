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

// LoRa pins for TTGO T1 without display
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26
#define SCK 5
#define MISO 19
#define MOSI 27

// Web server configuration
const char* captivePortal = "lorasender.local";
const char* apiEndpoint = "192.168.25.237:8000";

// LoRa configuration
const int frequency = 868E6; // 868MHz
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
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        :root {
            --primary: #2c3e50;
            --secondary: #3498db;
            --success: #27ae60;
            --warning: #f39c12;
            --danger: #e74c3c;
            --light: #ecf0f1;
            --dark: #34495e;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { 
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; 
            max-width: 600px; 
            margin: 0 auto; 
            padding: 10px; 
            font-size: 14px;
            color: #333;
            background: #f9f9f9;
        }
        h1 { 
            font-size: 1.2rem; 
            background: var(--primary); 
            color: white; 
            padding: 10px; 
            border-radius: 5px;
            margin-bottom: 10px;
        }
        h2 { 
            font-size: 1rem; 
            border-bottom: 2px solid var(--secondary); 
            padding-bottom: 5px; 
            margin: 10px 0 5px 0;
            color: var(--primary);
        }
        .flex { display: flex; gap: 5px; }
        .flex > * { flex: 1; }
        .panel {
            background: white;
            border-radius: 5px;
            padding: 10px;
            margin-bottom: 10px;
            box-shadow: 0 1px 3px rgba(0,0,0,0.1);
        }
        .badge {
            display: inline-block;
            padding: 2px 5px;
            border-radius: 3px;
            font-size: 0.7rem;
            font-weight: bold;
            color: white;
            background: var(--secondary);
        }
        .data-preview {
            background: var(--light);
            border-radius: 3px;
            padding: 5px;
            margin: 5px 0;
            font-family: monospace;
            font-size: 0.8rem;
            white-space: pre-wrap;
            word-break: break-all;
            max-height: 60px;
            overflow-y: auto;
        }
        select, input {
            width: 100%;
            padding: 8px;
            margin: 5px 0;
            border: 1px solid #ddd;
            border-radius: 3px;
        }
        button {
            width: 100%;
            padding: 8px;
            background: var(--secondary);
            color: white;
            border: none;
            border-radius: 3px;
            cursor: pointer;
            font-weight: bold;
            transition: background 0.2s;
        }
        button:hover { background: #2980b9; }
        button:active { transform: translateY(1px); }
        .loading {
            display: none;
            text-align: center;
            padding: 5px;
        }
        .loading::after {
            content: "⏳";
            animation: spin 1s linear infinite;
            display: inline-block;
        }
        @keyframes spin { 100% { transform: rotate(360deg); } }
        table {
            width: 100%;
            border-collapse: collapse;
            font-size: 0.8rem;
        }
        th, td {
            padding: 5px;
            text-align: left;
            border-bottom: 1px solid #eee;
        }
        th { font-weight: bold; color: var(--dark); }
        .status {
            padding: 8px;
            border-radius: 3px;
            margin: 5px 0;
            display: none;
        }
        .success { background: #d4edda; color: #155724; }
        .error { background: #f8d7da; color: #721c24; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .freq-badge {
            float: right;
            background: var(--primary);
            color: white;
            padding: 2px 5px;
            border-radius: 3px;
            font-size: 0.7rem;
        }
    </style>
</head>
<body>
    <h1>LoRa Sender <span class="freq-badge">868 MHz</span></h1>
    
    <div id="status" class="status"></div>
    
    <div class="panel">
        <h2>Send Data</h2>
        <form id="dataForm">
            <div class="flex">
                <select id="dataType" name="dataType" required onchange="updatePreview()">
                    <option value="text">Text Message</option>
                    <option value="image">Image URL</option>
                    <option value="audio">Audio URL</option>
                    <option value="repeat">Repeated Text</option>
                    <option value="random">Random Data</option>
                    <option value="ones">All Ones</option>
                    <option value="zeros">All Zeros</option>
                </select>
                <input type="number" id="length" name="length" placeholder="Length" min="1" style="display:none" onchange="updatePreview()">
            </div>
            
            <input type="text" id="data" name="data" placeholder="Enter data or URL" required onkeyup="updatePreview()">
            
            <div class="data-preview" id="preview">No data to preview</div>
            
            <button type="submit" id="sendBtn">Send Data</button>
            <div id="loading" class="loading">Sending data...</div>
        </form>
    </div>

    <div class="grid">
        <div class="panel">
            <h2>Parameters</h2>
            <table>
                <tr><td>SF:</td><td id="sf">-</td></tr>
                <tr><td>BW:</td><td id="bw">-</td></tr>
                <tr><td>CR:</td><td id="cr">-</td></tr>
                <tr><td>Freq:</td><td id="freq">-</td></tr>
            </table>
        </div>

        <div class="panel">
            <h2>Results</h2>
            <table>
                <tr><td>RSSI:</td><td id="rssi">-</td></tr>
                <tr><td>SNR:</td><td id="snr">-</td></tr>
                <tr><td>Delay:</td><td id="delay">-</td></tr>
                <tr><td>Rate:</td><td id="datarate">-</td></tr>
            </table>
        </div>
    </div>

    <div class="panel" id="ackPanel" style="display:none">
        <h2>Acknowledgment</h2>
        <div id="ackStatus"></div>
    </div>

    <script>
        // Initialize with frequency
        document.getElementById('freq').textContent = '868 MHz';
        
        function updatePreview() {
            const dataType = document.getElementById('dataType').value;
            const dataInput = document.getElementById('data').value;
            const lengthInput = document.getElementById('length');
            const preview = document.getElementById('preview');
            
            // Show/hide length field based on data type
            if (['repeat', 'random', 'ones', 'zeros'].includes(dataType)) {
                lengthInput.style.display = 'block';
            } else {
                lengthInput.style.display = 'none';
            }
            
            // Update placeholder based on type
            const dataField = document.getElementById('data');
            switch(dataType) {
                case 'text': dataField.placeholder = 'Enter your message'; break;
                case 'image': dataField.placeholder = 'Enter image URL'; break;
                case 'audio': dataField.placeholder = 'Enter audio URL'; break;
                case 'repeat': dataField.placeholder = 'Text to repeat'; break;
                case 'random': dataField.placeholder = 'Optional seed'; break;
                case 'ones': 
                case 'zeros': dataField.placeholder = 'Optional description'; break;
            }
            
            // Generate preview
            let previewText = 'No data to preview';
            if (dataInput) {
                const length = lengthInput.value || '?';
                switch(dataType) {
                    case 'text': previewText = `Text: "${dataInput}"`; break;
                    case 'image': previewText = `Image URL: ${dataInput}`; break;
                    case 'audio': previewText = `Audio URL: ${dataInput}`; break;
                    case 'repeat': 
                        const sample = dataInput.repeat(Math.min(3, length));
                        previewText = `Repeat (${length}x): "${sample}${length > 3 ? '...' : ''}"`; 
                        break;
                    case 'random': previewText = `Random (${length} chars)`; break;
                    case 'ones': previewText = `All Ones (${length} chars)`; break;
                    case 'zeros': previewText = `All Zeros (${length} chars)`; break;
                }
            }
            preview.textContent = previewText;
        }
        
        document.getElementById('dataForm').onsubmit = async function(e) {
            e.preventDefault();
            
            // Show loading, hide button
            document.getElementById('loading').style.display = 'block';
            document.getElementById('sendBtn').disabled = true;
            
            // Clear status
            const status = document.getElementById('status');
            status.style.display = 'none';
            status.className = 'status';
            
            try {
                const formData = new FormData(this);
                const response = await fetch('/send', {
                    method: 'POST',
                    body: formData
                });
                
                if (!response.ok) throw new Error(`HTTP error ${response.status}`);
                
                const result = await response.json();
                updateResults(result);
                
                // Show success message
                status.textContent = 'Data sent successfully!';
                status.className = 'status success';
                status.style.display = 'block';
                
                // Show acknowledgment panel
                const ackPanel = document.getElementById('ackPanel');
                ackPanel.style.display = 'block';
                document.getElementById('ackStatus').innerHTML = `
                    <p>✅ Acknowledgment received</p>
                    <p>RSSI: ${result.rssi} dBm | SNR: ${result.snr} dB</p>
                    <p>Received at: ${new Date().toLocaleTimeString()}</p>
                `;
            } catch (error) {
                console.error('Error:', error);
                
                // Show error message
                status.textContent = `Error: ${error.message}`;
                status.className = 'status error';
                status.style.display = 'block';
                
                // Show error in ack panel
                const ackPanel = document.getElementById('ackPanel');
                ackPanel.style.display = 'block';
                document.getElementById('ackStatus').innerHTML = `
                    <p>❌ No acknowledgment received</p>
                    <p>Error: ${error.message}</p>
                `;
            } finally {
                // Hide loading, enable button
                document.getElementById('loading').style.display = 'none';
                document.getElementById('sendBtn').disabled = false;
            }
        };

        function updateResults(data) {
            document.getElementById('sf').textContent = data.sf;
            document.getElementById('bw').textContent = formatBandwidth(data.bw);
            document.getElementById('cr').textContent = formatCodingRate(data.cr);
            document.getElementById('freq').textContent = formatFrequency(data.frequency);
            document.getElementById('rssi').textContent = `${data.rssi} dBm`;
            document.getElementById('snr').textContent = `${data.snr} dB`;
            document.getElementById('delay').textContent = `${data.delay} ms`;
            document.getElementById('datarate').textContent = `${data.datarate} bps`;
        }
        
        function formatBandwidth(bw) {
            return bw >= 1000000 ? `${bw/1000000} MHz` : `${bw/1000} kHz`;
        }
        
        function formatCodingRate(cr) {
            return `4/${cr}`;
        }
        
        function formatFrequency(freq) {
            return `${freq/1000000} MHz`;
        }
        
        // Initialize preview
        updatePreview();
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
    
    Serial.println("Sent packet: " + payload);
    Serial.println("Waiting for acknowledgment...");
    
    // Wait for ACK with timeout
    unsigned long ackTime = millis();
    String ackData = "";
    bool ackReceived = false;
    
    // Add a short delay to give the receiver time to process
    delay(50);
    
    while (millis() - ackTime < 2000) {
        if (LoRa.parsePacket()) {
            String ack = LoRa.readString();
            if (ack.startsWith("STANDARD_ACK:")) {
                ackData = ack.substring(13); // Remove the STANDARD_ACK: prefix
                ackReceived = true;
                Serial.println("ACK received: " + ackData);
                break;
            }
        }
        // Short delay to prevent tight loop
        delay(10);
    }
    
    if (!ackReceived) {
        Serial.println("No ACK received after timeout");
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
        
        // Create a more detailed JSON with all the metrics
        DynamicJsonDocument apiDoc(1024);
        apiDoc["type"] = dataType;
        apiDoc["data"] = payload;
        apiDoc["sf"] = sf;
        apiDoc["bw"] = bw;
        apiDoc["cr"] = cr;
        apiDoc["rssi"] = result.rssi;
        apiDoc["snr"] = result.snr;
        apiDoc["delay"] = result.delay;
        apiDoc["datarate"] = result.datarate;
        apiDoc["latency"] = result.latency;
        apiDoc["source"] = "standard";
        apiDoc["compressionRatio"] = 1.0;
        apiDoc["timestamp"] = millis();
        
        String apiJson;
        serializeJson(apiDoc, apiJson);
        
        Serial.println("Sending metrics to API: " + apiJson);
        
        int httpResponseCode = http.POST(apiJson);
        if (httpResponseCode > 0) {
            Serial.println("HTTP Response code: " + String(httpResponseCode));
            String response = http.getString();
            Serial.println("API Response: " + response);
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
    
    // Initialize SPI for LoRa
    SPI.begin(SCK, MISO, MOSI, LORA_SS);
    
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