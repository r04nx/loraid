/*
 * LoRa Benchmarking Tool for TTGO LoRa T1 Board
 * 
 * This sketch sets up a WiFi AP with a captive portal that allows
 * users to input various data for LoRa benchmarking tests.
 * 
 * Features:
 * - WiFi AP with SSID "lora node1"
 * - Modern web-based captive portal UI
 * - LoRa transmission on 868MHz
 * - Benchmark measurements for various data sizes
 * - Performance metrics display with charts
 * 
 * Hardware: TTGO LoRa T1 (ESP32 + SX1276)
 */

// Libraries for WiFi and Web Server
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include "esp_task_wdt.h"

// LoRa Libraries
#include <SPI.h>
#include <LoRa.h>

// LoRa Pin definitions for LILYGO LORA V1.0
#define LORA_SCK    5    // GPIO5  - SPI Clock
#define LORA_MISO   19   // GPIO19 - SPI MISO
#define LORA_MOSI   27   // GPIO27 - SPI MOSI
#define LORA_CS     18   // GPIO18 - SPI Chip Select
#define LORA_RST    14   // GPIO14 - RESET
#define LORA_IRQ    26   // GPIO26 - Interrupt Request

// LED Pin
#define LED_PIN     2    // GPIO2 - Onboard LED

// WiFi AP settings
const char* AP_SSID = "lora node1";
const char* AP_PASSWORD = "lorabenchmark";
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// DNS server for captive portal
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer webServer(80);

// LoRa frequency - 868MHz for Europe, 915MHz for US
const long FREQUENCY = 868E6;

// For storing benchmark results
struct BenchmarkResult {
  int dataSize;
  float transmissionTime;
  float latency;
  int errorRate;
};

#define MAX_RESULTS 10
BenchmarkResult results[MAX_RESULTS];
int resultCount = 0;
// Benchmark parameters
int packetSize = 32;            // Default packet size in bytes
int packetCount = 10;           // Number of packets to send
int transmissionDelay = 200;    // Delay between transmissions in ms
String dataType = "random";     // Type of data to send (random, zeros, ones, etc.)
bool acknowledgment = true;     // Whether to wait for acknowledgment
int spreadingFactor = 7;        // LoRa spreading factor (7-12)
int bandwidth = 125;            // LoRa bandwidth (kHz)
int codingRate = 5;             // LoRa coding rate (5-8)

// Benchmark status tracking
volatile bool benchmarkRunning = false;
volatile int benchmarkProgress = 0;
volatile bool benchmarkComplete = false;
String benchmarkStatus = "Ready to start benchmarking";
SemaphoreHandle_t resultsMutex = NULL;

// For communication with benchmark task
TaskHandle_t benchmarkTaskHandle = NULL;
// Forward declarations
void setupAP();
void setupLoRa();
void setupDNS();
void setupWebServer();
void handleRoot();
void handleNotFound();
void handleStartBenchmark();
void handleGetResults();
void handleSettings();
void runBenchmark();
void generatePayload(uint8_t* buffer, int size, String type);
void updateStatus(String message);
void captivePortalResponse();
void handleBenchmarkStatus();
void displayError(String message);

// Make sure we have a FreeRTOS compatible task declaration for the benchmark task
void benchmarkTaskSimple(void * parameter);

// At the top of the file, after includes
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"

void setup() {
  // Initialize serial
  Serial.begin(115200);
  delay(1000);
  Serial.println("LoRa Benchmarking Tool Starting");
  
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    delay(1000);
  }
  
  // Setup LoRa first, before WiFi
  setupLoRaEnhanced();
  delay(1000);
  
  // Initialize WiFi
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);
  
  // Setup AP
  WiFi.mode(WIFI_AP);
  delay(500);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(500);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  delay(500);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // Setup DNS server
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("DNS server started");
  
  // Setup Web Server
  webServer.on("/", handleRoot);
  webServer.on("/start-benchmark", HTTP_ANY, handleStartBenchmarkEnhanced);
  webServer.on("/results", handleGetResults);
  webServer.on("/settings", HTTP_ANY, handleSettings);
  webServer.on("/benchmark-status", handleBenchmarkStatus);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("Web server started");
  
  // Create mutex for results
  resultsMutex = xSemaphoreCreateMutex();
  if (resultsMutex == NULL) {
    Serial.println("Failed to create mutex!");
    delay(1000);
    ESP.restart();
  }
  
  Serial.println("Setup complete");
  updateStatus("Ready for benchmarking!");
}

void createHtmlFile() {
  File file = SPIFFS.open("/index.html", FILE_WRITE);
  if (file) {
    const char* html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>LoRa Benchmark</title>
    <style>
        :root {
            --primary-color: #4285f4;
            --secondary-color: #34a853;
            --accent-color: #ea4335;
            --background-color: #f8f9fa;
            --card-color: #ffffff;
            --text-color: #202124;
        }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 0;
            background-color: var(--background-color);
            color: var(--text-color);
        }
        header {
            background-color: var(--primary-color);
            color: white;
            padding: 1rem;
            text-align: center;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 1rem;
        }
        .card {
            background-color: var(--card-color);
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            padding: 1.5rem;
            margin-bottom: 1.5rem;
        }
        h1, h2, h3 {
            margin-top: 0;
        }
        .tab-container {
            display: flex;
            margin-bottom: 1rem;
        }
        .tab {
            padding: 0.5rem 1rem;
            cursor: pointer;
            border-bottom: 2px solid transparent;
            transition: all 0.3s;
        }
        .tab.active {
            border-bottom: 2px solid var(--primary-color);
            color: var(--primary-color);
            font-weight: bold;
        }
        .tab-content {
            display: none;
        }
        .tab-content.active {
            display: block;
        }
        .form-group {
            margin-bottom: 1rem;
        }
        label {
            display: block;
            margin-bottom: 0.5rem;
            font-weight: 500;
        }
        input, select {
            width: 100%;
            padding: 0.5rem;
            border: 1px solid #ddd;
            border-radius: 4px;
            box-sizing: border-box;
        }
        input[type="number"] {
            width: 100%;
        }
        button {
            background-color: var(--primary-color);
            color: white;
            border: none;
            padding: 0.75rem 1.5rem;
            border-radius: 4px;
            cursor: pointer;
            font-weight: bold;
            transition: background-color 0.3s;
        }
        button:hover {
            background-color: #3367d6;
        }
        .results-container {
            overflow-x: auto;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 1rem;
        }
        th, td {
            padding: 0.75rem;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background-color: #f2f2f2;
        }
        .charts-container {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(450px, 1fr));
            gap: 1.5rem;
            margin-top: 2rem;
        }
        .chart-wrapper {
            background-color: #ffffff;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            padding: 1rem;
        }
        .chart-wrapper h3 {
            text-align: center;
            margin-top: 0;
            margin-bottom: 1rem;
            font-size: 1rem;
            color: var(--primary-color);
        }
        .chart-container {
            height: 250px;
            position: relative;
        }
        @media (max-width: 768px) {
            .charts-container {
                grid-template-columns: 1fr;
        }
        }
        .status {
            padding: 1rem;
            border-radius: 4px;
            margin-bottom: 1rem;
            display: none;
        }
        .status.success {
            background-color: #d4edda;
            color: #155724;
            display: block;
        }
        .status.error {
            background-color: #f8d7da;
            color: #721c24;
            display: block;
        }
        .loading {
            display: inline-block;
            width: 20px;
            height: 20px;
            border: 3px solid rgba(0, 0, 0, 0.1);
            border-radius: 50%;
            border-top-color: var(--primary-color);
            animation: spin 1s ease-in-out infinite;
            margin-right: 10px;
        }
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
        .advanced-toggle {
            color: var(--primary-color);
            cursor: pointer;
            margin-bottom: 1rem;
            display: inline-block;
        }
        .advanced-settings {
            display: none;
            background-color: #f9f9f9;
            padding: 1rem;
            border-radius: 4px;
            margin-bottom: 1rem;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
            gap: 1rem;
        }
        @media (max-width: 768px) {
            .grid {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <header>
        <h1>LoRa Benchmarking Tool</h1>
        <p>Test and measure LoRa communication performance</p>
    </header>

    <div class="container">
        <div class="tab-container">
            <div class="tab active" data-tab="benchmark">Benchmark</div>
            <div class="tab" data-tab="results">Results</div>
            <div class="tab" data-tab="settings">Settings</div>
        </div>

        <div id="status" class="status"></div>

        <div id="benchmark-tab" class="tab-content active">
            <div class="card">
                <h2>LoRa Benchmark Configuration</h2>
                <div class="form-group">
                    <label for="packet-size">Packet Size (bytes)</label>
                    <input type="number" id="packet-size" min="1" max="255" value="32">
                </div>
                <div class="form-group">
                    <label for="packet-count">Number of Packets</label>
                    <input type="number" id="packet-count" min="1" max="1000" value="10">
                </div>
                <div class="form-group">
                    <label for="data-type">Data Type</label>
                    <select id="data-type">
                        <option value="random">Random Data</option>
                        <option value="zeros">All Zeros</option>
                        <option value="ones">All Ones</option>
                        <option value="incremental">Incremental</option>
                        <option value="alternating">Alternating 0/1</option>
                    </select>
                </div>
                <div class="form-group">
                    <label for="ack">Request Acknowledgment</label>
                    <select id="ack">
                        <option value="true">Yes</option>
                        <option value="false">No</option>
                    </select>
                </div>

                <div class="advanced-toggle">+ Advanced LoRa Settings</div>
                <div class="advanced-settings">
                    <div class="grid">
                        <div class="form-group">
                            <label for="sf">Spreading Factor</label>
                            <select id="sf">
                                <option value="7">SF7</option>
                                <option value="8">SF8</option>
                                <option value="9">SF9</option>
                                <option value="10">SF10</option>
                                <option value="11">SF11</option>
                                <option value="12">SF12</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label for="bw">Bandwidth (kHz)</label>
                            <select id="bw">
                                <option value="125">125</option>
                                <option value="250">250</option>
                                <option value="500">500</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label for="cr">Coding Rate</label>
                            <select id="cr">
                                <option value="5">4/5</option>
                                <option value="6">4/6</option>
                                <option value="7">4/7</option>
                                <option value="8">4/8</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label for="delay">Delay Between Packets (ms)</label>
                            <input type="number" id="delay" min="0" max="5000" value="200">
                        </div>
                    </div>
                </div>
                
                <button id="start-benchmark-btn">Start Benchmark</button>
            </div>

            <div class="card">
                <h2>Current Status</h2>
                <p id="benchmark-status">Ready to start benchmarking</p>
                <div id="progress-container" style="display: none;">
                    <p>Progress: <span id="progress">0%</span></p>
                    <div style="background-color: #eee; border-radius: 4px; height: 20px; margin-top: 10px;">
                        <div id="progress-bar" style="background-color: var(--primary-color); height: 100%; width: 0%; border-radius: 4px;"></div>
                    </div>
                </div>
            </div>
        </div>

        <div id="results-tab" class="tab-content">
            <div class="card">
                <h2>Benchmark Results</h2>
                <div class="results-container">
                    <table id="results-table">
                        <thead>
                            <tr>
                                <th>Packet Size (bytes)</th>
                                <th>Transmission Time (ms)</th>
                                <th>Latency (ms)</th>
                                <th>Error Rate (%)</th>
                                <th>Throughput (bytes/s)</th>
                            </tr>
                        </thead>
                        <tbody id="results-body">
                            <!-- Results will be inserted here by JavaScript -->
                        </tbody>
                    </table>
                </div>
                
                <div class="charts-container">
                    <div class="chart-wrapper">
                        <h3>Transmission Time vs Packet Size</h3>
                        <div class="chart-container">
                            <canvas id="time-chart"></canvas>
                        </div>
                    </div>
                    <div class="chart-wrapper">
                        <h3>Throughput vs Packet Size</h3>
                        <div class="chart-container">
                            <canvas id="throughput-chart"></canvas>
                        </div>
                    </div>
                    <div class="chart-wrapper">
                        <h3>Latency vs Packet Size</h3>
                        <div class="chart-container">
                            <canvas id="latency-chart"></canvas>
                        </div>
                    </div>
                    <div class="chart-wrapper">
                        <h3>Error Rate Comparison</h3>
                        <div class="chart-container">
                            <canvas id="error-chart"></canvas>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div id="settings-tab" class="tab-content">
            <div class="card">
                <h2>LoRa Radio Settings</h2>
                <form id="settings-form">
                    <div class="grid">
                        <div class="form-group">
                            <label for="setting-sf">Default Spreading Factor</label>
                            <select id="setting-sf" name="sf">
                                <option value="7">SF7</option>
                                <option value="8">SF8</option>
                                <option value="9">SF9</option>
                                <option value="10">SF10</option>
                                <option value="11">SF11</option>
                                <option value="12">SF12</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label for="setting-bw">Default Bandwidth (kHz)</label>
                            <select id="setting-bw" name="bw">
                                <option value="125">125</option>
                                <option value="250">250</option>
                                <option value="500">500</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label for="setting-cr">Default Coding Rate</label>
                            <select id="setting-cr" name="cr">
                                <option value="5">4/5</option>
                                <option value="6">4/6</option>
                                <option value="7">4/7</option>
                                <option value="8">4/8</option>
                            </select>
                        </div>
                    </div>
                    <button type="submit">Save Settings</button>
                </form>
            </div>
        </div>
    </div>

    <!-- Replace Chart.js CDN with inline Chart.js -->
    <script>
    // Minimal Chart.js implementation
    (function() {
      window.Chart = function(ctx) {
        this.ctx = ctx;
        this.type = 'line';
        this.data = null;
        this.options = null;
        this.datasets = [];
      };
      
      Chart.prototype.destroy = function() {
        var canvas = this.ctx.canvas;
        canvas.width = canvas.width; // Clear canvas
      };
      
      Chart.prototype.update = function() {
        this.render();
      };
      
      Chart.prototype.render = function() {
        if (!this.data || !this.data.datasets || !this.ctx) return;
        
        var canvas = this.ctx.canvas;
        var ctx = this.ctx;
        
        // Clear canvas
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        
        // Set dimensions
        var chartWidth = canvas.width;
        var chartHeight = canvas.height;
        var padding = 40;
        var plotWidth = chartWidth - (padding * 2);
        var plotHeight = chartHeight - (padding * 2);
        
        // Draw background
        ctx.fillStyle = '#f8f9fa';
        ctx.fillRect(0, 0, chartWidth, chartHeight);
        
        // Draw axis
        ctx.beginPath();
        ctx.moveTo(padding, padding);
        ctx.lineTo(padding, chartHeight - padding);
        ctx.lineTo(chartWidth - padding, chartHeight - padding);
        ctx.strokeStyle = '#000';
        ctx.lineWidth = 1;
        ctx.stroke();
        
        // Draw data
        var datasets = this.data.datasets;
        var labels = this.data.labels || [];
        
        for (var i = 0; i < datasets.length; i++) {
          var dataset = datasets[i];
          var data = dataset.data || [];
          
          // Find min/max values for scaling
          var min = Math.min.apply(null, data);
          var max = Math.max.apply(null, data);
          
          if (min === max) {
            min = 0;
            max = max * 2 || 10;
          }
          
          if (this.options && this.options.scales && this.options.scales.y) {
            if (typeof this.options.scales.y.min !== 'undefined') min = this.options.scales.y.min;
            if (typeof this.options.scales.y.max !== 'undefined') max = this.options.scales.y.max;
          }
          
          // Draw the data points and lines
          ctx.beginPath();
          
          for (var j = 0; j < data.length; j++) {
            var x = padding + (j * (plotWidth / (data.length - 1 || 1)));
            var y = chartHeight - padding - ((data[j] - min) / (max - min) * plotHeight);
            
            if (j === 0) {
              ctx.moveTo(x, y);
            } else {
              ctx.lineTo(x, y);
            }
            
            // Draw point
            ctx.fillStyle = dataset.borderColor || '#4285f4';
            ctx.beginPath();
            ctx.arc(x, y, 3, 0, Math.PI * 2);
            ctx.fill();
          }
          
          // Stroke the line
          ctx.strokeStyle = dataset.borderColor || '#4285f4';
          ctx.lineWidth = 2;
          ctx.stroke();
          
          // Fill under the line if needed
          if (dataset.fill) {
            ctx.lineTo(padding + plotWidth, chartHeight - padding);
            ctx.lineTo(padding, chartHeight - padding);
            ctx.fillStyle = dataset.backgroundColor || 'rgba(66, 133, 244, 0.2)';
            ctx.fill();
          }
        }
        
        // Draw y-axis labels
        ctx.fillStyle = '#000';
        ctx.font = '10px sans-serif';
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        
        for (var k = 0; k <= 5; k++) {
          var labelY = chartHeight - padding - (k * plotHeight / 5);
          var labelValue = min + (k * (max - min) / 5);
          ctx.fillText(labelValue.toFixed(1), padding - 5, labelY);
        }
        
        // Draw x-axis labels
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';
        
        for (var l = 0; l < labels.length; l++) {
          var labelX = padding + (l * plotWidth / (labels.length - 1 || 1));
          ctx.fillText(labels[l], labelX, chartHeight - padding + 5);
        }
        
        // Draw title if available
        if (this.options && this.options.title && this.options.title.text) {
          ctx.textAlign = 'center';
          ctx.font = 'bold 12px sans-serif';
          ctx.fillText(this.options.title.text, chartWidth / 2, 15);
        }
      };
    })();
    
    </script>
    <script>
    // Tab switching functionality
    document.querySelectorAll('.tab').forEach(tab => {
        tab.addEventListener('click', () => {
            // Remove active class from all tabs
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            
            // Add active class to clicked tab and corresponding content
            tab.classList.add('active');
            const tabId = tab.getAttribute('data-tab');
            document.getElementById(tabId + '-tab').classList.add('active');
        });
    });
    
    // Toggle advanced settings
    document.querySelector('.advanced-toggle').addEventListener('click', function() {
        const advancedSettings = document.querySelector('.advanced-settings');
        if (advancedSettings.style.display === 'block') {
            advancedSettings.style.display = 'none';
            this.textContent = '+ Advanced LoRa Settings';
        } else {
            advancedSettings.style.display = 'block';
            this.textContent = '- Advanced LoRa Settings';
        }
    });
    
    // Start benchmark button
    document.getElementById('start-benchmark-btn').addEventListener('click', function() {
        const packetSize = document.getElementById('packet-size').value;
        const packetCount = document.getElementById('packet-count').value;
        const dataType = document.getElementById('data-type').value;
        const ack = document.getElementById('ack').value;
        const sf = document.getElementById('sf').value;
        const bw = document.getElementById('bw').value;
        const cr = document.getElementById('cr').value;
        const delay = document.getElementById('delay').value;
        
        // Display status
        const status = document.getElementById('status');
        status.textContent = 'Starting benchmark...';
        status.className = 'status success';
        
        // Show progress container
        document.getElementById('progress-container').style.display = 'block';
        
        // Send benchmark request to server
        fetch('/start-benchmark', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `packetSize=${packetSize}&packetCount=${packetCount}&dataType=${dataType}&ack=${ack}&sf=${sf}&bw=${bw}&cr=${cr}&delay=${delay}`
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                pollBenchmarkStatus();
            } else {
                status.textContent = 'Error: ' + data.message;
                status.className = 'status error';
            }
        })
        .catch(error => {
            status.textContent = 'Connection error: ' + error;
            status.className = 'status error';
        });
    });
    
    // Poll benchmark status
    function pollBenchmarkStatus() {
        const statusElem = document.getElementById('benchmark-status');
        const progressElem = document.getElementById('progress');
        const progressBarElem = document.getElementById('progress-bar');
        
        const pollInterval = setInterval(() => {
            fetch('/benchmark-status')
            .then(response => response.json())
            .then(data => {
                statusElem.textContent = data.status;
                progressElem.textContent = data.progress + '%';
                progressBarElem.style.width = data.progress + '%';
                
                if (data.complete) {
                    clearInterval(pollInterval);
                    document.getElementById('status').textContent = 'Benchmark completed successfully!';
                    fetchResults();
                    // Switch to results tab
                    document.querySelector('.tab[data-tab="results"]').click();
                }
            })
            .catch(error => {
                clearInterval(pollInterval);
                document.getElementById('status').textContent = 'Error polling status: ' + error;
                document.getElementById('status').className = 'status error';
            });
        }, 500);
    }
    
    // Fetch results
    function fetchResults() {
        fetch('/results')
        .then(response => response.json())
        .then(data => {
            const resultsBody = document.getElementById('results-body');
            resultsBody.innerHTML = '';
            
            data.forEach(result => {
                const row = document.createElement('tr');
                const throughput = result.dataSize / (result.transmissionTime / 1000);
                
                row.innerHTML = `
                    <td>${result.dataSize}</td>
                    <td>${result.transmissionTime.toFixed(2)}</td>
                    <td>${result.latency.toFixed(2)}</td>
                    <td>${result.errorRate.toFixed(2)}</td>
                    <td>${throughput.toFixed(2)}</td>
                `;
                resultsBody.appendChild(row);
            });
            
            // Create charts with the data
            createCharts(data);
        })
        .catch(error => {
            document.getElementById('status').textContent = 'Error fetching results: ' + error;
            document.getElementById('status').className = 'status error';
        });
    }
    
    // Save settings form
    document.getElementById('settings-form').addEventListener('submit', function(e) {
        e.preventDefault();
        
        const sf = document.getElementById('setting-sf').value;
        const bw = document.getElementById('setting-bw').value;
        const cr = document.getElementById('setting-cr').value;
        
        fetch('/settings', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `sf=${sf}&bw=${bw}&cr=${cr}`
        })
        .then(response => response.json())
        .then(data => {
            const status = document.getElementById('status');
            if (data.success) {
                status.textContent = 'Settings saved successfully!';
                status.className = 'status success';
            } else {
                status.textContent = 'Error: ' + data.message;
                status.className = 'status error';
            }
        })
        .catch(error => {
            const status = document.getElementById('status');
            status.textContent = 'Connection error: ' + error;
            status.className = 'status error';
        });
    });
    
    // Chart objects
    let timeChart = null;
    let throughputChart = null;
    let latencyChart = null;
    let errorChart = null;
    
    // Chart colors
    const chartColors = {
        primary: '#4285f4',
        secondary: '#34a853',
        accent: '#ea4335',
        neutral: '#fbbc05',
        lightPrimary: 'rgba(66, 133, 244, 0.2)',
        lightSecondary: 'rgba(52, 168, 83, 0.2)',
        lightAccent: 'rgba(234, 67, 53, 0.2)',
        lightNeutral: 'rgba(251, 188, 5, 0.2)'
    };
    
    function createCharts(data) {
        // Prepare data for charts
        const packetSizes = data.map(result => result.dataSize);
        const transmissionTimes = data.map(result => result.transmissionTime);
        const latencies = data.map(result => result.latency);
        const errorRates = data.map(result => result.errorRate);
        const throughputs = data.map(result => result.dataSize / (result.transmissionTime / 1000));
        
        // Create/update transmission time chart
        const timeCtx = document.getElementById('time-chart').getContext('2d');
        if (timeChart) {
            timeChart.destroy();
        }
        timeChart = new Chart(timeCtx);
        timeChart.type = 'line';
        timeChart.data = {
            labels: packetSizes,
            datasets: [{
                label: 'Transmission Time (ms)',
                data: transmissionTimes,
                backgroundColor: chartColors.lightPrimary,
                borderColor: chartColors.primary,
                borderWidth: 2,
                tension: 0.3,
                pointBackgroundColor: chartColors.primary,
                fill: true
            }]
        };
        timeChart.options = {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Packet Size (bytes)'
                    }
                },
                y: {
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Time (ms)'
                    }
                }
            }
        };
        timeChart.render();
        
        // Create/update throughput chart
        const throughputCtx = document.getElementById('throughput-chart').getContext('2d');
        if (throughputChart) {
            throughputChart.destroy();
        }
        throughputChart = new Chart(throughputCtx);
        throughputChart.type = 'bar';
        throughputChart.data = {
            labels: packetSizes,
            datasets: [{
                label: 'Throughput (bytes/s)',
                data: throughputs,
                backgroundColor: chartColors.lightSecondary,
                borderColor: chartColors.secondary,
                borderWidth: 2
            }]
        };
        throughputChart.options = {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Packet Size (bytes)'
                    }
                },
                y: {
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Bytes/second'
                    }
                }
            }
        };
        throughputChart.render();
        
        // Create/update latency chart
        const latencyCtx = document.getElementById('latency-chart').getContext('2d');
        if (latencyChart) {
            latencyChart.destroy();
        }
        latencyChart = new Chart(latencyCtx);
        latencyChart.type = 'line';
        latencyChart.data = {
            labels: packetSizes,
            datasets: [{
                label: 'Latency (ms)',
                data: latencies,
                backgroundColor: chartColors.lightAccent,
                borderColor: chartColors.accent,
                borderWidth: 2,
                tension: 0.3,
                pointBackgroundColor: chartColors.accent,
                fill: true
            }]
        };
        latencyChart.options = {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Packet Size (bytes)'
                    }
                },
                y: {
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Latency (ms)'
                    }
                }
            }
        };
        latencyChart.render();
        
        // Create/update error rate chart
        const errorCtx = document.getElementById('error-chart').getContext('2d');
        if (errorChart) {
            errorChart.destroy();
        }
        errorChart = new Chart(errorCtx);
        errorChart.type = 'bar';
        errorChart.data = {
            labels: packetSizes,
            datasets: [{
                label: 'Error Rate (%)',
                data: errorRates,
                backgroundColor: chartColors.lightNeutral,
                borderColor: chartColors.neutral,
                borderWidth: 2
            }]
        };
        errorChart.options = {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    title: {
                        display: true,
                        text: 'Packet Size (bytes)'
                    }
                },
                y: {
                    min: 0,
                    max: 100,
                    title: {
                        display: true,
                        text: 'Error Rate (%)'
                    }
                }
            }
        };
        errorChart.render();
    }
    
    // Initial fetch of results when page loads
    fetchResults();
    </script>
</body>
</html>
)rawliteral";
    
    file.print(html);
    file.close();
  }
}

void handleRoot() {
  // Serve the HTML file from SPIFFS
  if (SPIFFS.exists("/index.html")) {
    File file = SPIFFS.open("/index.html", "r");
    if (file) {
      webServer.streamFile(file, "text/html");
      file.close();
      return;
    }
  }
  // If file doesn't exist, create it
  createHtmlFile();
  handleRoot();  // Try again after creating the file
}

void captivePortalResponse() {
  if (webServer.hostHeader() != String(apIP.toString())) {
    webServer.sendHeader("Location", String("http://") + apIP.toString(), true);
    webServer.send(302, "text/plain", "");
  } else {
    handleRoot();
  }
}

void displayError(String message) {
  Serial.println("Error: " + message);
}

void handleNotFound() {
  // For captive portal, redirect any request to the root
  captivePortalResponse();
}

void handleStartBenchmarkEnhanced() {
  // Extract parameters
  if (webServer.hasArg("packetSize")) packetSize = webServer.arg("packetSize").toInt();
  if (webServer.hasArg("packetCount")) packetCount = webServer.arg("packetCount").toInt();
  if (webServer.hasArg("dataType")) dataType = webServer.arg("dataType");
  if (webServer.hasArg("ack")) acknowledgment = true; // Always enabled to match receiver
  if (webServer.hasArg("delay")) transmissionDelay = webServer.arg("delay").toInt();
  
  // Frontend may send SF/BW/CR params, but we override them for receiver compatibility
  spreadingFactor = 7;  // Fixed to match receiver
  bandwidth = 125;      // Fixed to match receiver
  codingRate = 5;       // Fixed to match receiver
  
  // Limit parameters to safe values
  if (packetSize > 31) packetSize = 31; // Reduced from 32 to match receiver (1 byte seq + 31 payload)
  if (packetCount > 50) packetCount = 50;
  if (transmissionDelay < 200) transmissionDelay = 200;
  
  // Update LoRa settings to match receiver
  LoRa.idle();
  delay(100);
  
  Serial.println("Applying LoRa settings compatible with receiver:");
  Serial.printf("SF: %d, BW: %d kHz, CR: 4/%d\n", spreadingFactor, bandwidth, codingRate);
  
  // Reset the radio first for clean state
  LoRa.sleep();
  delay(50);
  
  // Apply settings
  LoRa.setSpreadingFactor(spreadingFactor);
  LoRa.setSignalBandwidth(bandwidth * 1000);
  LoRa.setCodingRate4(codingRate);
  LoRa.enableCrc();
  LoRa.setPreambleLength(8);  // Default value, matches receiver
  LoRa.setTxPower(14);        // Standard power, matches receiver
  
  // Wake up and prepare for transmission
  LoRa.idle();
  delay(100);
  
  // Respond to client
  webServer.send(200, "application/json", "{\"success\":true,\"message\":\"Benchmark starting with receiver-compatible settings\"}");
  
  // Start benchmark directly
  runBenchmark();
  
  // Update status
  benchmarkRunning = false;
  benchmarkComplete = true;
  benchmarkStatus = "Benchmark completed";
}

void handleGetResults() {
  if (xSemaphoreTake(resultsMutex, portMAX_DELAY)) {
    String json = "[";
    for (int i = 0; i < resultCount; i++) {
      if (i > 0) json += ",";
      json += "{";
      json += "\"dataSize\":" + String(results[i].dataSize) + ",";
      json += "\"transmissionTime\":" + String(results[i].transmissionTime) + ",";
      json += "\"latency\":" + String(results[i].latency) + ",";
      json += "\"errorRate\":" + String(results[i].errorRate);
      json += "}";
    }
    json += "]";
    xSemaphoreGive(resultsMutex);
    webServer.send(200, "application/json", json);
  } else {
    webServer.send(500, "application/json", "{\"success\":false,\"message\":\"Mutex timeout\"}");
  }
}

void handleSettings() {
  if (!webServer.hasArg("sf") || !webServer.hasArg("bw") || !webServer.hasArg("cr")) {
    webServer.send(400, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
    return;
  }
  
  // Parse settings
  int sf = webServer.arg("sf").toInt();
  int bw = webServer.arg("bw").toInt();
  int cr = webServer.arg("cr").toInt();
  
  // Validate settings
  if (sf < 7 || sf > 12 || (bw != 125 && bw != 250 && bw != 500) || cr < 5 || cr > 8) {
    webServer.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid settings\"}");
    return;
  }
  
  // Update global settings
  spreadingFactor = sf;
  bandwidth = bw;
  codingRate = cr;
  
  // Apply to LoRa radio if not in benchmark
  if (!benchmarkRunning) {
    LoRa.setSpreadingFactor(spreadingFactor);
    LoRa.setSignalBandwidth(bandwidth * 1000);
    LoRa.setCodingRate4(codingRate);
  }
  
  webServer.send(200, "application/json", "{\"success\":true,\"message\":\"Settings saved\"}");
}

// Handle benchmark status polling
void handleBenchmarkStatus() {
  String json = "{";
  json += "\"status\":\"" + benchmarkStatus + "\",";
  json += "\"progress\":" + String(benchmarkProgress) + ",";
  json += "\"complete\":" + String(benchmarkComplete ? "true" : "false");
  json += "}";
  webServer.send(200, "application/json", json);
}

// Generate payload data according to the specified type and size
void generatePayload(uint8_t* buffer, int size, String type) {
  if (type == "zeros") {
    memset(buffer, 0, size);
  } 
  else if (type == "ones") {
    memset(buffer, 0xFF, size);
  } 
  else if (type == "incremental") {
    for (int i = 0; i < size; i++) {
      buffer[i] = i % 256;
    }
  } 
  else if (type == "alternating") {
    for (int i = 0; i < size; i++) {
      buffer[i] = (i % 2) ? 0xFF : 0x00;
    }
  } 
  else { // default to random
    for (int i = 0; i < size; i++) {
      buffer[i] = random(0, 256);
    }
  }
}

// Main benchmark task without try-catch for better Arduino compatibility
void benchmarkTaskSimple(void * parameter) {
  if (benchmarkRunning) {
    vTaskDelete(NULL);
    return;
  }
  
  // Reset benchmark state
  benchmarkRunning = true;
  benchmarkProgress = 0;
  benchmarkComplete = false;
  benchmarkStatus = "Starting benchmark...";
  
  // Clear previous results
  if (xSemaphoreTake(resultsMutex, portMAX_DELAY)) {
    resultCount = 0;
    xSemaphoreGive(resultsMutex);
  }
  
  updateStatus("Starting benchmark");
  Serial.println("Benchmark task started");
  
  // Add delay before starting actual benchmark
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  // Run the benchmark with simple error checking
  Serial.println("Running benchmark...");
  runBenchmark();
  
  // Check if benchmark completed successfully
  if (benchmarkProgress < 100) {
    Serial.println("Warning: Benchmark might not have completed fully");
    benchmarkStatus = "Benchmark partially completed";
  } else {
    benchmarkStatus = "Benchmark completed";
  }
  
  // Update status
  benchmarkRunning = false;
  benchmarkComplete = true;
  updateStatus(benchmarkStatus);
  
  // Clean up and delete task
  vTaskDelay(pdMS_TO_TICKS(100));
  vTaskDelete(NULL);
}

void runBenchmark() {
  const int bufferSize = 31;  // Reduce from 32 to match receiver's expectation (1 byte seq + 31 payload)
  uint8_t payload[31];
  
  int successCount = 0;
  int totalPackets = packetCount;
  unsigned long totalTxTime = 0;
  unsigned long totalLatency = 0;
  
  Serial.println("Starting LoRa transmission test");
  Serial.println("Sending to compatible receiver...");
  generatePayload(payload, bufferSize, dataType);
  
  BenchmarkResult result;
  result.dataSize = bufferSize;
  result.transmissionTime = 0;
  result.latency = 0;
  result.errorRate = 0;
  
  // Set LoRa to maximum power for improved reliability (but not too high)
  LoRa.setTxPower(14);  // Same as the receiver's expected power
  
  // Simple LoRa transmission loop
  for (int i = 0; i < packetCount; i++) {
    Serial.printf("Sending packet %d/%d\n", i + 1, packetCount);
    benchmarkProgress = ((i + 1) * 100) / packetCount;
    
    bool packetSent = false;
    int retryCount = 0;
    const int maxRetries = 3;
    
    while (!packetSent && retryCount < maxRetries) {
      unsigned long startTime = millis();
      
      // Start packet with retry logic
      bool beginPacketSuccess = false;
      for (int attempt = 0; attempt < 3; attempt++) {
        if (LoRa.beginPacket()) {
          beginPacketSuccess = true;
          break;
        }
        Serial.printf("Attempt %d to start packet failed, retrying...\n", attempt + 1);
        delay(50);
      }
      
      if (!beginPacketSuccess) {
        Serial.println("Failed to start packet after multiple attempts");
        retryCount++;
        delay(100);
        continue;
      }
      
      // Add sequence number (this is what receiver expects first)
      uint8_t seqNum = (i & 0xFF);
      LoRa.write(seqNum);
      
      // Add payload - must match what receiver expects
      LoRa.write(payload, bufferSize);
      
      // End packet (synchronous mode for better reliability)
      bool endPacketSuccess = LoRa.endPacket(false);  // Non-async mode for reliability
      
      if (!endPacketSuccess) {
        Serial.println("Failed to end packet");
        retryCount++;
        delay(100);
        continue;
      }
      
      unsigned long txEndTime = millis();
      unsigned long txTime = txEndTime - startTime;
      totalTxTime += txTime;
      
      // Wait for acknowledgment if enabled - THE RECEIVER ALWAYS SENDS AN ACK
      LoRa.receive();
      unsigned long ackTimeout = 1000; // 1 second timeout for ACK
      unsigned long ackStartTime = millis();
      bool ackReceived = false;
      
      while (millis() - ackStartTime < ackTimeout) {
        int packetSize = LoRa.parsePacket();
        if (packetSize) {
          byte receivedSeqNum = LoRa.read();
          if (receivedSeqNum == seqNum) {
            unsigned long latency = millis() - txEndTime;
            totalLatency += latency;
            ackReceived = true;
            successCount++;
            Serial.printf("ACK received for packet %d, latency: %lu ms\n", i, latency);
            break;
          } else {
            Serial.printf("Received wrong ACK: expected %d, got %d\n", seqNum, receivedSeqNum);
          }
        }
        delay(10);
      }
      
      if (!ackReceived) {
        Serial.printf("Acknowledgment timeout for packet %d\n", i);
        retryCount++;
        if (retryCount < maxRetries) {
          Serial.printf("Retrying packet %d (attempt %d/%d)\n", i, retryCount + 1, maxRetries);
          continue;
        }
      } else {
        packetSent = true;
        break;
      }
    }
    
    // If all retries failed
    if (!packetSent) {
      Serial.printf("Failed to send packet %d after %d attempts\n", i, maxRetries);
    }
    
    // Fixed delay between packets for reliable transmission
    delay(200);  // Fixed 200ms delay - same as default in receiver code
  }
  
  // Return to normal power setting
  LoRa.setTxPower(14);
  
  // Calculate and log results
  if (successCount > 0) {
    result.transmissionTime = (float)totalTxTime / successCount;
    result.latency = (float)totalLatency / successCount;
  }
  
  result.errorRate = ((packetCount - successCount) * 100) / packetCount;
  
  // Log results
  Serial.println("\nBenchmark Results:");
  Serial.printf("Total packets sent: %d\n", packetCount);
  Serial.printf("Successful transmissions: %d\n", successCount);
  Serial.printf("Average transmission time: %.2f ms\n", result.transmissionTime);
  Serial.printf("Average latency: %.2f ms\n", result.latency);
  Serial.printf("Error rate: %.2f%%\n", result.errorRate);
  
  // Store results
  if (xSemaphoreTake(resultsMutex, portMAX_DELAY)) {
    if (resultCount < MAX_RESULTS) {
      results[resultCount++] = result;
    }
    xSemaphoreGive(resultsMutex);
  }
}

void loop() {
  static unsigned long lastDNS = 0;
  static unsigned long lastBlink = 0;
  const unsigned long DNS_INTERVAL = 100;
  const unsigned long BLINK_INTERVAL = 1000;
  
  unsigned long currentMillis = millis();
  
  // Handle DNS requests with rate limiting
  if (currentMillis - lastDNS >= DNS_INTERVAL) {
    dnsServer.processNextRequest();
    lastDNS = currentMillis;
  }
  
  // Handle web server requests
  webServer.handleClient();
  
  // Blink LED with rate limiting
  if (currentMillis - lastBlink >= BLINK_INTERVAL) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = currentMillis;
  }
  
  // Small delay to prevent watchdog timeout
  delay(1);
}

void setupLoRaEnhanced() {
  Serial.println("Initializing enhanced LoRa setup...");
  
  // Hardware reset the LoRa module first
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(200);  // Longer reset time for better reliability
  digitalWrite(LORA_RST, HIGH);
  delay(500);  // Give the module more time to stabilize
  
  // Initialize SPI the same way as the receiver
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  // Don't set SPI frequency/mode to ensure compatibility with receiver
  delay(200);
  
  // Configure LoRa pins
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  delay(100);
  
  // Multiple initialization attempts with increasing delays
  bool initSuccess = false;
  for(int i = 0; i < 5; i++) { // Increase max attempts to 5
    Serial.printf("Enhanced LoRa init attempt %d/5...\n", i + 1);
    
    if (LoRa.begin(FREQUENCY)) {
      // Set exactly the same parameters as the receiver uses
      LoRa.setSpreadingFactor(7);  // Same as receiver
      LoRa.setSignalBandwidth(125E3); // Same as receiver
      LoRa.setCodingRate4(5);  // Same as receiver
      LoRa.enableCrc();  // Same as receiver
      delay(100);
      
      // Test packet to verify functionality
      Serial.println("Sending test packet...");
      bool testSuccess = false;
      
      for (int test = 0; test < 3; test++) {
        if (LoRa.beginPacket()) {
          LoRa.write((uint8_t)0xFF); // Test byte
          if (LoRa.endPacket(true)) {
            delay(200);
            testSuccess = true;
            break;
          }
        }
        delay(100);
      }
      
      if (testSuccess) {
        // Configure user settings but preserve compatibility with receiver
        // Store user settings for later use
        spreadingFactor = 7;  // Match receiver
        bandwidth = 125;      // Match receiver
        codingRate = 5;       // Match receiver
        
        // Set default power
        LoRa.setTxPower(14);
        
        Serial.println("Enhanced LoRa setup successful!");
        initSuccess = true;
        break;
      } else {
        Serial.println("Test packet transmission failed");
      }
    }
    
    // More thorough reset between attempts
    Serial.printf("Attempt %d failed, resetting more thoroughly...\n", i + 1);
    LoRa.end();
    delay(100);
    digitalWrite(LORA_RST, LOW);
    delay(200 * (i + 1)); // Progressively longer reset
    digitalWrite(LORA_RST, HIGH);
    delay(500 * (i + 1)); // Progressively longer stabilization
  }
  
  if (!initSuccess) {
    Serial.println("CRITICAL: LoRa initialization failed after 5 attempts");
    Serial.println("Application will continue but LoRa features may not work");
    // Flash LED to indicate error
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(100);
    }
    return;
  }
  
  // Final verification
  Serial.println("Enhanced LoRa setup complete and verified");
  Serial.println("Using compatible settings with receiver:");
  Serial.printf("SF: %d, BW: %d kHz, CR: 4/%d\n", spreadingFactor, bandwidth, codingRate);
  
  // Success indicator flash
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(50);
    digitalWrite(LED_PIN, HIGH);
    delay(50);
  }
}

void setupAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
}

bool setupDNSWithCheck() {
  dnsServer.stop();  // Stop any existing DNS server
  delay(100);
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("DNS server started");
  return true;
}

bool setupWebServerWithCheck() {
  // Set up web server routes with more delay between each
  webServer.on("/", handleRoot);
  delay(50);
  webServer.on("/start-benchmark", HTTP_ANY, handleStartBenchmarkEnhanced);
  delay(50);
  webServer.on("/results", handleGetResults);
  delay(50);
  webServer.on("/settings", HTTP_ANY, handleSettings);
  delay(50);
  webServer.on("/benchmark-status", handleBenchmarkStatus);
  delay(50);
  webServer.onNotFound(handleNotFound);
  delay(50);
  
  // Start web server
  webServer.begin();
  Serial.println("Web server started");
  delay(100);
  
  // Start mDNS responder
  if (!MDNS.begin("lora")) {
    Serial.println("MDNS responder failed");
    return false;
  }
  Serial.println("MDNS responder started");
  return true;
}

void updateStatus(String message) {
  Serial.println(message);
  benchmarkStatus = message;
}
