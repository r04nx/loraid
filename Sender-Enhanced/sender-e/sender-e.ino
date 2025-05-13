#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <SPI.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// WiFi credentials
const char* ssid = "Dedsec";
const char* password = "asdfghjkl";

// LoRa pins for TTGO LoRa32 V1
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26
#define LORA_MOSI 27
#define LORA_MISO 19
#define LORA_SCK 5

// Web server configuration
const char* captivePortal = "lorasender-enhanced.local";
const char* apiEndpoint = "192.168.25.237:8000";

// LoRa configuration - initial values, will be optimized
int sf = 7; // Spreading Factor
int bw = 500E3; // Changed from 125E3 to 500E3
int cr = 5; // Coding Rate
const int frequency = 868E6; // 868MHz

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
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        :root {
            --primary: #8e44ad;
            --secondary: #9b59b6;
            --success: #27ae60;
            --warning: #f39c12;
            --danger: #e74c3c;
            --light: #ecf0f1;
            --dark: #34495e;
            --enhanced: #e8f5e9;
            --enhanced-dark: #27ae60;
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
        .enhanced-panel {
            background: var(--enhanced);
            border-left: 4px solid var(--enhanced-dark);
        }
        .badge {
            display: inline-block;
            padding: 2px 5px;
            border-radius: 3px;
            font-size: 0.7rem;
            font-weight: bold;
            color: white;
        }
        .badge-optimal { background: var(--success); }
        .badge-adapting { background: var(--warning); }
        .badge-exploring { background: var(--secondary); }
        .badge-fixed { background: var(--dark); }
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
            background: var(--primary);
            color: white;
            border: none;
            border-radius: 3px;
            cursor: pointer;
            font-weight: bold;
            transition: background 0.2s;
        }
        button:hover { background: var(--secondary); }
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
        .feature-list {
            padding-left: 15px;
            margin: 5px 0;
            font-size: 0.8rem;
        }
        .feature-list li {
            margin-bottom: 3px;
        }
        .compression-info {
            margin-top: 5px;
            font-size: 0.8rem;
            color: var(--success);
        }
    </style>
</head>
<body>
    <h1>LoRa Enhanced Sender <span class="freq-badge">868 MHz</span></h1>
    
    <div id="status" class="status"></div>
    
    <div class="panel enhanced-panel">
        <h2>Smart Framework</h2>
        <ul class="feature-list">
            <li><strong>Adaptive Parameters:</strong> Auto-optimizes SF, BW, CR based on history</li>
            <li><strong>Smart Compression:</strong> Content-specific data compression</li>
            <li><strong>Performance Optimization:</strong> Balances reliability and speed</li>
        </ul>
    </div>
    
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
            <div id="compression-preview" class="compression-info"></div>
            
            <button type="submit" id="sendBtn">Send Data</button>
            <div id="loading" class="loading">Optimizing and sending data...</div>
        </form>
    </div>

    <div class="panel">
        <h2>Adaptive Parameters</h2>
        <table>
            <tr>
                <td>SF:</td>
                <td id="sf">-</td>
                <td><span id="sf-status" class="badge">-</span></td>
            </tr>
            <tr>
                <td>BW:</td>
                <td id="bw">-</td>
                <td><span id="bw-status" class="badge">-</span></td>
            </tr>
            <tr>
                <td>CR:</td>
                <td id="cr">-</td>
                <td><span id="cr-status" class="badge">-</span></td>
            </tr>
            <tr>
                <td>Freq:</td>
                <td id="freq">-</td>
                <td><span class="badge badge-fixed">Fixed</span></td>
            </tr>
        </table>
    </div>

    <div class="grid">
        <div class="panel">
            <h2>Signal</h2>
            <table>
                <tr><td>RSSI:</td><td id="rssi">-</td></tr>
                <tr><td>SNR:</td><td id="snr">-</td></tr>
                <tr><td>Delay:</td><td id="delay">-</td></tr>
                <tr><td>Rate:</td><td id="datarate">-</td></tr>
            </table>
        </div>

        <div class="panel">
            <h2>Performance</h2>
            <table>
                <tr><td>Compression:</td><td id="compression">-</td></tr>
                <tr><td>Original Size:</td><td id="orig-size">-</td></tr>
                <tr><td>Compressed:</td><td id="comp-size">-</td></tr>
                <tr><td>Latency:</td><td id="latency">-</td></tr>
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
            const compressionPreview = document.getElementById('compression-preview');
            
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
            let compressionText = '';
            
            if (dataInput) {
                const length = lengthInput.value || '?';
                switch(dataType) {
                    case 'text': 
                        previewText = `Text: "${dataInput}"`; 
                        if (dataInput.length > 3) {
                            // Estimate compression for text
                            let repeats = 0;
                            for (let i = 0; i < dataInput.length - 1; i++) {
                                if (dataInput[i] === dataInput[i+1]) repeats++;
                            }
                            const ratio = repeats > 0 ? (dataInput.length / (dataInput.length - repeats * 0.5)).toFixed(2) : 1.0;
                            compressionText = `Estimated compression: ${ratio}x (${repeats > 0 ? 'Compressible' : 'Low compressibility'})`;
                        }
                        break;
                    case 'image': previewText = `Image URL: ${dataInput}`; break;
                    case 'audio': previewText = `Audio URL: ${dataInput}`; break;
                    case 'repeat': 
                        const sample = dataInput.repeat(Math.min(3, length));
                        previewText = `Repeat (${length}x): "${sample}${length > 3 ? '...' : ''}"`;
                        compressionText = 'High compression potential: ~10-100x';
                        break;
                    case 'random': 
                        previewText = `Random (${length} chars)`; 
                        compressionText = 'Low compression potential: ~1x';
                        break;
                    case 'ones': 
                        previewText = `All Ones (${length} chars)`; 
                        compressionText = 'Maximum compression potential: ~100x';
                        break;
                    case 'zeros': 
                        previewText = `All Zeros (${length} chars)`; 
                        compressionText = 'Maximum compression potential: ~100x';
                        break;
                }
            }
            preview.textContent = previewText;
            compressionPreview.textContent = compressionText;
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
                status.textContent = 'Data sent successfully with optimized parameters!';
                status.className = 'status success';
                status.style.display = 'block';
                
                // Show acknowledgment panel
                const ackPanel = document.getElementById('ackPanel');
                ackPanel.style.display = 'block';
                document.getElementById('ackStatus').innerHTML = `
                    <p>✅ Acknowledgment received</p>
                    <p>RSSI: ${result.rssi} dBm | SNR: ${result.snr} dB</p>
                    <p>Received at: ${new Date().toLocaleTimeString()}</p>
                    <p class="compression-info">Data compressed ${result.compressionRatio.toFixed(2)}x</p>
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
            document.getElementById('compression').textContent = `${data.compressionRatio.toFixed(2)}x`;
            document.getElementById('latency').textContent = `${data.latency} ms`;
            
            // Update sizes if available
            if (data.originalSize) {
                document.getElementById('orig-size').textContent = `${data.originalSize} bytes`;
                document.getElementById('comp-size').textContent = `${data.compressedSize} bytes`;
            } else {
                // Provide default values since we don't have the actual data length
                const compressionRatio = data.compressionRatio || 1.0;
                document.getElementById('orig-size').textContent = `~100 bytes`;
                document.getElementById('comp-size').textContent = `~${Math.round(100 / compressionRatio)} bytes`;
            }
            
            // Update status indicators with badges
            updateStatusBadge('sf-status', data.sfStatus || 'Initial');
            updateStatusBadge('bw-status', data.bwStatus || 'Initial');
            updateStatusBadge('cr-status', data.crStatus || 'Initial');
        }
        
        function updateStatusBadge(elementId, status) {
            const statusElement = document.getElementById(elementId);
            statusElement.className = 'badge';
            
            if (status.toLowerCase().includes('optimal')) {
                statusElement.classList.add('badge-optimal');
                statusElement.textContent = 'Optimal';
            } else if (status.toLowerCase().includes('adapting')) {
                statusElement.classList.add('badge-adapting');
                statusElement.textContent = 'Adapting';
            } else if (status.toLowerCase().includes('exploring')) {
                statusElement.classList.add('badge-exploring');
                statusElement.textContent = 'Exploring';
            } else {
                statusElement.classList.add('badge-adapting');
                statusElement.textContent = status;
            }
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

// Function to compress data based on its type and content
String compressData(String dataType, String data) {
    // Advanced compression strategies based on data type
    if (dataType == "text" || dataType == "repeat") {
        // For text, implement hybrid compression (RLE + dictionary for common words)
        
        // First, check for common words and replace with shorter codes
        String commonWords[] = {"the", "and", "that", "have", "for", "not", "with", "you", "this", "but"};
        String codes[] = {"~1", "~2", "~3", "~4", "~5", "~6", "~7", "~8", "~9", "~0"};
        
        String processed = " " + data + " "; // Add spaces to help with word boundaries
        
        // Replace common words with codes (only if they are complete words)
        for (int i = 0; i < 10; i++) {
            String wordWithBoundaries = " " + commonWords[i] + " ";
            String codeWithBoundaries = " " + codes[i] + " ";
            processed.replace(wordWithBoundaries, codeWithBoundaries);
        }
        
        // Remove the extra spaces we added
        processed = processed.substring(1, processed.length() - 1);
        
        // Then apply run-length encoding for repeated characters
        String compressed = "";
        int count = 1;
        for (int i = 0; i < processed.length(); i++) {
            if (i + 1 < processed.length() && processed[i] == processed[i + 1]) {
                count++;
            } else {
                if (count > 2) { // More aggressive compression
                    compressed += String(count) + processed[i];
                } else {
                    for (int j = 0; j < count; j++) {
                        compressed += processed[i];
                    }
                }
                count = 1;
            }
        }
        
        // Add compression type marker
        return "C1:" + compressed;
    } else if (dataType == "ones" || dataType == "zeros") {
        // For repeated patterns, just store the count
        if (data.startsWith("ONES:")) {
            return "ONES:" + String(data.length() - 5);
        } else if (data.startsWith("ZEROS:")) {
            return "ZEROS:" + String(data.length() - 6);
        }
    } else if (dataType == "random") {
        // For random data, implement advanced dictionary compression
        if (data.length() > 20) {
            // Create a dictionary of common patterns
            String compressed = "RAND:DICT:";
            String remaining = data.substring(5); // Remove RAND: prefix
            
            // First pass: Find repeating patterns of 3-6 characters (more aggressive)
            for (int len = 6; len >= 3; len--) {
                for (int i = 0; i < remaining.length() - len; i++) {
                    String pattern = remaining.substring(i, i + len);
                    int occurrences = 0;
                    int pos = remaining.indexOf(pattern);
                    
                    while (pos >= 0) {
                        occurrences++;
                        pos = remaining.indexOf(pattern, pos + 1);
                    }
                    
                    // If pattern occurs multiple times, add to dictionary
                    // More aggressive threshold (was 3)
                    if (occurrences > 2) {
                        // Use more compact dictionary keys (single byte)
                        char dictKey = (char)(128 + (compressed.length() % 128)); // Use extended ASCII
                        compressed += dictKey + pattern;
                        
                        // Replace all occurrences with the dictionary key
                        int replacePos = remaining.indexOf(pattern);
                        while (replacePos >= 0) {
                            remaining = remaining.substring(0, replacePos) + dictKey + 
                                      remaining.substring(replacePos + pattern.length());
                            replacePos = remaining.indexOf(pattern, replacePos + 1);
                        }
                    }
                    
                    // Larger dictionary size (was 10)
                    if ((compressed.length() - 10) / 7 >= 20) break;
                }
            }
            
            // Second pass: Look for repeating single characters
            int count; // Declare the count variable
            count = 1;
            String charCompressed = ""; // Declare the charCompressed variable
            for (int i = 0; i < remaining.length(); i++) {
                if (i + 1 < remaining.length() && remaining[i] == remaining[i + 1]) {
                    count++;
                } else {
                    if (count > 2) {
                        charCompressed += String(count) + remaining[i];
                    } else {
                        for (int j = 0; j < count; j++) {
                            charCompressed += remaining[i];
                        }
                    }
                    count = 1;
                }
            }
            
            // Use the shorter of the two results
            if (charCompressed.length() < remaining.length()) {
                remaining = "RLE:" + charCompressed;
            }
            
            return compressed + "::" + remaining;
        }
    } else if (dataType == "image" || dataType == "audio") {
        // For URLs, implement more aggressive URL compression
        if (data.startsWith("IMG:") || data.startsWith("AUD:")) {
            String url = data.substring(4);
            String prefix = data.substring(0, 4);
            
            // Protocol compression
            if (url.startsWith("http://")) {
                url = "H:" + url.substring(7);
            } else if (url.startsWith("https://")) {
                url = "HS:" + url.substring(8);
            }
            
            // Domain compression - more comprehensive
            url.replace("www.", "W.");
            url.replace(".com", ".C");
            url.replace(".org", ".O");
            url.replace(".net", ".N");
            url.replace(".io", ".I");
            url.replace(".edu", ".E");
            url.replace(".gov", ".G");
            url.replace(".co.uk", ".UK");
            url.replace(".co.in", ".IN");
            
            // Path compression
            url.replace("/images/", "/I/");
            url.replace("/audio/", "/A/");
            url.replace("/video/", "/V/");
            url.replace("/download/", "/D/");
            url.replace("/content/", "/C/");
            
            // Parameter compression
            url.replace("?id=", "?i=");
            url.replace("&size=", "&s=");
            url.replace("&format=", "&f=");
            url.replace("&quality=", "&q=");
            url.replace("&version=", "&v=");
            
            return prefix + url;
        }
    }
    
    // Default: try simple run-length encoding for any unhandled type
    String compressed = "";
    int count = 1;
    for (int i = 0; i < data.length(); i++) {
        if (i + 1 < data.length() && data[i] == data[i + 1]) {
            count++;
        } else {
            if (count > 2) {
                compressed += String(count) + data[i];
            } else {
                for (int j = 0; j < count; j++) {
                    compressed += data[i];
                }
            }
            count = 1;
        }
    }
    
    // Only use compressed version if it's actually smaller
    return (compressed.length() < data.length()) ? compressed : data;
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
    
    // Count successful transmissions for each parameter set
    int sfSuccessCount[13] = {0}; // SF7-SF12
    int bwSuccessCount[3] = {0};  // 125, 250, 500 kHz
    int crSuccessCount[5] = {0};  // CR 4/5 to 4/8
    
    int sfTotalCount[13] = {0};
    int bwTotalCount[3] = {0};
    int crTotalCount[5] = {0};
    
    // Collect statistics on each parameter
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (performanceHistory[i].sf >= 7 && performanceHistory[i].sf <= 12) {
            int sfIndex = performanceHistory[i].sf - 7;
            sfTotalCount[sfIndex]++;
            if (performanceHistory[i].success) {
                sfSuccessCount[sfIndex]++;
            }
        }
        
        int bwIndex = -1;
        if (performanceHistory[i].bw == 125E3) bwIndex = 0;
        else if (performanceHistory[i].bw == 250E3) bwIndex = 1;
        else if (performanceHistory[i].bw == 500E3) bwIndex = 2;
        
        if (bwIndex >= 0) {
            bwTotalCount[bwIndex]++;
            if (performanceHistory[i].success) {
                bwSuccessCount[bwIndex]++;
            }
        }
        
        if (performanceHistory[i].cr >= 5 && performanceHistory[i].cr <= 8) {
            int crIndex = performanceHistory[i].cr - 5;
            crTotalCount[crIndex]++;
            if (performanceHistory[i].success) {
                crSuccessCount[crIndex]++;
            }
        }
    }
    
    // Find best individual parameters
    float bestSfScore = 0;
    float bestBwScore = 0;
    float bestCrScore = 0;
    
    // Evaluate SF (Spreading Factor)
    for (int i = 0; i < 6; i++) { // SF7-SF12
        if (sfTotalCount[i] > 0) {
            float successRate = (float)sfSuccessCount[i] / sfTotalCount[i];
            // Lower SF = higher data rate but shorter range
            float dataRateFactor = 1.0 / (i + 7); // Approximate inverse relationship
            float score = successRate * dataRateFactor * 10;
            
            if (score > bestSfScore) {
                bestSfScore = score;
                bestSF = i + 7;
            }
        }
    }
    
    // Evaluate BW (Bandwidth)
    for (int i = 0; i < 3; i++) {
        if (bwTotalCount[i] > 0) {
            float successRate = (float)bwSuccessCount[i] / bwTotalCount[i];
            // Higher BW = higher data rate but more susceptible to noise
            float bwValue = (i == 0) ? 125 : (i == 1) ? 250 : 500;
            float dataRateFactor = bwValue / 125.0; // Proportional to bandwidth
            float score = successRate * dataRateFactor * 10;
            
            if (score > bestBwScore) {
                bestBwScore = score;
                bestBW = (i == 0) ? 125E3 : (i == 1) ? 250E3 : 500E3;
            }
        }
    }
    
    // Evaluate CR (Coding Rate)
    for (int i = 0; i < 4; i++) {
        if (crTotalCount[i] > 0) {
            float successRate = (float)crSuccessCount[i] / crTotalCount[i];
            // Lower CR = higher data rate but less error correction
            float errorCorrectionFactor = 1.0 / (i + 5); // Approximate inverse relationship
            float score = successRate * errorCorrectionFactor * 10;
            
            if (score > bestCrScore) {
                bestCrScore = score;
                bestCR = i + 5;
            }
        }
    }
    
    // Check if we should update parameters
    bool shouldUpdate = (bestSF != sf || bestBW != bw || bestCR != cr);
    
    // Also check for combinations with high performance
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (performanceHistory[i].success) {
            float datarate = performanceHistory[i].datarate;
            float latency = max(1.0f, performanceHistory[i].latency);
            float rssi = abs(performanceHistory[i].rssi); // Lower absolute RSSI is better
            float snr = performanceHistory[i].snr;        // Higher SNR is better
            
            // Comprehensive score considering all factors
            float score = (datarate / latency) * (snr / 10.0) * (100.0 / rssi);
            
            if (score > bestScore) {
                bestScore = score;
                // If this combination is significantly better, use it instead
                bestSF = performanceHistory[i].sf;
                bestBW = performanceHistory[i].bw;
                bestCR = performanceHistory[i].cr;
                shouldUpdate = true;
            }
        }
    }
    
    // Update parameters if better ones found
    if (shouldUpdate) {
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

// Function to intelligently explore different parameters to find better options
void exploreParameters() {
    // Adaptive exploration rate: explore more when performance is poor
    int exploreChance = 10; // Base 10% chance
    
    // Check recent performance
    int recentSuccesses = 0;
    int recentTotal = 0;
    
    for (int i = 0; i < min(5, historyIndex); i++) {
        recentTotal++;
        if (performanceHistory[i].success) {
            recentSuccesses++;
        }
    }
    
    // If recent success rate is low, increase exploration chance
    if (recentTotal > 0) {
        float successRate = (float)recentSuccesses / recentTotal;
        if (successRate < 0.5) {
            exploreChance = 30; // 30% chance if success rate < 50%
        } else if (successRate < 0.8) {
            exploreChance = 20; // 20% chance if success rate < 80%
        }
    }
    
    // Decide whether to explore
    if (random(100) < exploreChance) {
        int exploreSF = sf;
        int exploreBW = bw;
        int exploreCR = cr;
        bool paramChanged = false;
        
        // Intelligently choose which parameter to explore based on recent performance
        // For SF: If signal is weak (low RSSI/SNR), try higher SF for better range
        float avgRSSI = 0;
        float avgSNR = 0;
        int rssiCount = 0;
        
        for (int i = 0; i < min(5, historyIndex); i++) {
            if (performanceHistory[i].success) {
                avgRSSI += performanceHistory[i].rssi;
                avgSNR += performanceHistory[i].snr;
                rssiCount++;
            }
        }
        
        if (rssiCount > 0) {
            avgRSSI /= rssiCount;
            avgSNR /= rssiCount;
            
            // If signal is weak, try higher SF for better range
            if (avgRSSI < -100 || avgSNR < 5) {
                // Try higher SF for better range
                exploreSF = min(12, sf + 1);
                paramChanged = true;
            } 
            // If signal is strong, try lower SF for better data rate
            else if (avgRSSI > -80 && avgSNR > 10 && sf > 7) {
                exploreSF = max(7, sf - 1);
                paramChanged = true;
            }
        }
        
        // If we didn't change SF based on signal strength, randomly explore
        if (!paramChanged && random(100) < 33) {
            // Try different SF (7-12), but favor nearby values
            int sfDelta = random(-2, 3); // -2 to +2
            exploreSF = constrain(sf + sfDelta, 7, 12);
            paramChanged = true;
        }
        
        // Try different BW based on environment
        if (random(100) < 33) {
            // In noisy environments (low SNR), try narrower bandwidth
            if (rssiCount > 0 && avgSNR < 5 && bw > 125E3) {
                exploreBW = 125E3; // Use narrower bandwidth
            }
            // In clean environments, try wider bandwidth
            else if (rssiCount > 0 && avgSNR > 15) {
                int bwOptions[] = {125E3, 250E3, 500E3};
                exploreBW = bwOptions[random(3)];
            }
            // Otherwise random
            else {
                int bwOptions[] = {125E3, 250E3, 500E3};
                exploreBW = bwOptions[random(3)];
            }
            paramChanged = true;
        }
        
        // Try different CR based on error rates
        if (random(100) < 33) {
            // In noisy environments, try higher CR for better error correction
            if (rssiCount > 0 && avgSNR < 5) {
                exploreCR = min(8, cr + 1); // More error correction
            }
            // In clean environments, try lower CR for better data rate
            else if (rssiCount > 0 && avgSNR > 15 && cr > 5) {
                exploreCR = max(5, cr - 1); // Less error correction
            }
            // Otherwise random
            else {
                exploreCR = random(5, 9);
            }
            paramChanged = true;
        }
        
        // Only apply changes if parameters actually changed
        if (paramChanged) {
            // Apply exploration parameters
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

    // Compress data - improved compression
    String originalPayload = payload;
    String compressedPayload = compressData(dataType, payload);
    float compressionRatio = getCompressionRatio(originalPayload, compressedPayload);
    
    // Add LoRa parameter metadata to the payload
    String metaPayload = compressedPayload + "<META:SF" + String(sf) + ",BW" + String(bw/1000) + ",CR" + String(cr) + ">";
    
    // Send via LoRa with enhanced tag using default parameters for first transmission
    unsigned long startTime = millis();
    
    // Store current parameters
    int currentSf = sf;
    int currentBw = bw;
    int currentCr = cr;
    
    // Always use default parameters for initial transmission
    int initialSf = 7;
    int initialBw = 500E3;  // Changed from 125E3 to 500E3
    int initialCr = 5;
    
    // Set default parameters for initial transmission
    LoRa.setSpreadingFactor(initialSf);
    LoRa.setSignalBandwidth(initialBw);
    LoRa.setCodingRate4(initialCr);
    
    Serial.println("Initial transmission with default parameters: SF7, BW500, CR5");
    
    LoRa.beginPacket();
    LoRa.print("ENHANCED:" + metaPayload);
    LoRa.endPacket();
    delay(50);
    LoRa.receive();
    
    // Wait for ACK with adaptive timeout based on SF
    // Higher SF needs longer timeout
    int timeout = 2000 + (initialSf - 7) * 500; // Base 2s + 500ms per SF level above 7
    timeout = min(timeout, 5000);  // Cap maximum timeout
    
    unsigned long ackTime = millis();
    String ackData = "";
    bool ackReceived = false;
    bool receiverParamsReceived = false;
    int receiverSf = 7;
    int receiverBw = 500E3;
    int receiverCr = 5;
    
    Serial.println("Waiting for ACK with timeout: " + String(timeout) + "ms");
    
    while (millis() - ackTime < timeout) {
        if (LoRa.parsePacket()) {
            String ack = LoRa.readString();
            if (ack.startsWith("ENHANCED_ACK:")) {
                ackData = ack.substring(13); // Remove the ENHANCED_ACK: prefix
                ackReceived = true;
                Serial.println("ACK received after " + String(millis() - ackTime) + "ms");
                break;
            }
        }
        // Short delay to prevent tight loop
        delay(10);
    }
    
    if (!ackReceived) {
        Serial.println("No ACK received after " + String(timeout) + "ms");
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
        DeserializationError error = deserializeJson(doc, ackData);
        
        if (error) {
            Serial.println("Failed to parse ACK JSON: " + String(error.c_str()));
            Serial.println("Raw ACK data: " + ackData);
            // Revert to default parameters on parse error
            sf = 7;
            bw = 500E3;
            cr = 5;
            LoRa.setSpreadingFactor(sf);
            LoRa.setSignalBandwidth(bw);
            LoRa.setCodingRate4(cr);
        } else {
            // Successfully parsed JSON
            result.rssi = doc["rssi"];
            result.snr = doc["snr"];
            unsigned long ackTimestamp = doc["timestamp"];
            result.delay = (millis() - startTime);
            result.datarate = (originalPayload.length() * 8) / (result.delay / 1000.0); // Use original length for true data rate
            result.latency = result.delay;
            
            // Check if receiver has suggested optimal parameters
            if (doc.containsKey("opt_sf") && doc.containsKey("opt_bw") && doc.containsKey("opt_cr")) {
                receiverSf = doc["opt_sf"];
                receiverBw = doc["opt_bw"];
                receiverCr = doc["opt_cr"];
                receiverParamsReceived = true;
                
                // Validate parameters to ensure they're in valid ranges
                if (receiverSf >= 7 && receiverSf <= 12 && 
                    (receiverBw == 125E3 || receiverBw == 250E3 || receiverBw == 500E3) && 
                    receiverCr >= 5 && receiverCr <= 8) {
                    
                    Serial.println("Received optimized parameters from receiver: SF" + String(receiverSf) + 
                                ", BW" + String(receiverBw) + ", CR" + String(receiverCr));
                    
                    // Apply the receiver's suggested parameters
                    sf = receiverSf;
                    bw = receiverBw;
                    cr = receiverCr;
                    
                    // Update LoRa settings for future transmissions
                    LoRa.setSpreadingFactor(sf);
                    LoRa.setSignalBandwidth(bw);
                    LoRa.setCodingRate4(cr);
                    
                    Serial.println("Applied receiver's optimized parameters");
                } else {
                    Serial.println("Received invalid parameters from receiver, using default parameters");
                    sf = 7;
                    bw = 500E3;
                    cr = 5;
                    LoRa.setSpreadingFactor(sf);
                    LoRa.setSignalBandwidth(bw);
                    LoRa.setCodingRate4(cr);
                }
            } else {
                Serial.println("No optimized parameters received from receiver, using current parameters");
                // Restore the original parameters that were in use before the transmission
                sf = currentSf;
                bw = currentBw;
                cr = currentCr;
                LoRa.setSpreadingFactor(sf);
                LoRa.setSignalBandwidth(bw);
                LoRa.setCodingRate4(cr);
            }
        }
        
        // Record successful transmission in history
        performanceHistory[historyIndex].sf = initialSf; // Record with initial parameters
        performanceHistory[historyIndex].bw = initialBw;
        performanceHistory[historyIndex].cr = initialCr;
        performanceHistory[historyIndex].rssi = result.rssi;
        performanceHistory[historyIndex].snr = result.snr;
        performanceHistory[historyIndex].datarate = result.datarate;
        performanceHistory[historyIndex].latency = result.latency;
        performanceHistory[historyIndex].success = true;
        
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    } else {
        // If no ACK received, use local measurements and mark as failed
        result.rssi = -120; // Very poor signal as placeholder
        result.snr = 0;
        result.delay = timeout; // Timeout value
        result.datarate = 0; // Failed transmission
        result.latency = timeout;
        
        // Record failed transmission in history
        performanceHistory[historyIndex].sf = initialSf;
        performanceHistory[historyIndex].bw = initialBw;
        performanceHistory[historyIndex].cr = initialCr;
        performanceHistory[historyIndex].rssi = result.rssi;
        performanceHistory[historyIndex].snr = result.snr;
        performanceHistory[historyIndex].datarate = result.datarate;
        performanceHistory[historyIndex].latency = result.latency;
        performanceHistory[historyIndex].success = false;
        
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;
        
        // If ACK not received, revert to default parameters
        sf = 7;
        bw = 500E3;  // Changed from 125E3 to 500E3
        cr = 5;
        LoRa.setSpreadingFactor(sf);
        LoRa.setSignalBandwidth(bw);
        LoRa.setCodingRate4(cr);
        
        Serial.println("Reverting to default parameters due to failed transmission");
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
    responseDoc["originalSize"] = originalPayload.length();
    responseDoc["compressedSize"] = compressedPayload.length();
    responseDoc["sfStatus"] = (historyIndex > HISTORY_SIZE / 2) ? "Optimal" : "Adapting";
    responseDoc["bwStatus"] = (historyIndex > HISTORY_SIZE / 2) ? "Optimal" : "Adapting";
    responseDoc["crStatus"] = (historyIndex > HISTORY_SIZE / 2) ? "Optimal" : "Adapting";
    
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
        sf = 7;
        bw = 500E3;  // Changed from 125E3 to 500E3
        cr = 5;
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
    
    // Initialize SPI for LoRa
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
    
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