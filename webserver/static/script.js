
let sfChart;
let bwChart;
let rssiChart;
let snrChart;
let latencyChart;
let dataRateChart;
let compressionChart;

async function updateStats() {
    try {
        const response = await fetch('/api/stats');
        const data = await response.json();

        // Update stats cards
        if (data.reception_stats && data.reception_stats.avg_rssi !== null) {
            document.getElementById('avg-rssi').textContent = 
                data.reception_stats.avg_rssi.toFixed(1);
        }
        if (data.reception_stats && data.reception_stats.avg_snr !== null) {
            document.getElementById('avg-snr').textContent = 
                data.reception_stats.avg_snr.toFixed(1);
        }
        
        // Calculate average data rate and latency across all sources
        let totalDataRate = 0;
        let totalLatency = 0;
        let sourceCount = 0;
        
        // Update source metrics
        if (data.source_metrics && data.source_metrics.length > 0) {
            data.source_metrics.forEach(row => {
                if (row.avg_datarate) totalDataRate += row.avg_datarate;
                if (row.avg_latency) totalLatency += row.avg_latency;
                sourceCount++;
                
                if (row.source === 'standard' || row.source === 'enhanced') {
                    const sourcePrefix = row.source === 'standard' ? 'std' : 'enh';
                    if (document.getElementById(`${sourcePrefix}-datarate`)) {
                        document.getElementById(`${sourcePrefix}-datarate`).textContent = 
                            row.avg_datarate.toFixed(1);
                    }
                    if (document.getElementById(`${sourcePrefix}-latency`)) {
                        document.getElementById(`${sourcePrefix}-latency`).textContent = 
                            row.avg_latency.toFixed(1);
                    }
                }
            });
            
            // Update average data rate and latency
            if (sourceCount > 0) {
                const avgDataRate = totalDataRate / sourceCount;
                const avgLatency = totalLatency / sourceCount;
                
                document.getElementById('avg-datarate').textContent = avgDataRate.toFixed(1);
                document.getElementById('avg-latency').textContent = avgLatency.toFixed(1);
            }
        }
        
        // Update comparison metrics
        if (data.comparison) {
            if (data.comparison.datarate_improvement) {
                document.getElementById('datarate-improvement').textContent = 
                    data.comparison.datarate_improvement.toFixed(2);
            }
            if (data.comparison.latency_improvement) {
                document.getElementById('latency-improvement').textContent = 
                    data.comparison.latency_improvement.toFixed(2);
            }
            if (data.comparison.compression_ratio) {
                document.getElementById('avg-compression').textContent = 
                    data.comparison.compression_ratio.toFixed(2) + 'x';
            }
        }

        // Update transmission stats table
        const tbody = document.getElementById('transmissions-table-body');
        tbody.innerHTML = '';
        data.transmission_stats.forEach(row => {
            const timestamp = row.timestamp ? new Date(row.timestamp).toLocaleString() : 'N/A';
            const type = row.type || 'N/A';
            const source = row.source || 'standard';
            const sourceClass = source === 'enhanced' ? 'text-success' : '';
            const compression = row.avg_compression ? row.avg_compression.toFixed(2) + 'x' : '1.00x';
            
            // Check if this was a failed transmission (no ACK received)
            const isFailedTransmission = row.avg_rssi <= -120 && row.avg_snr <= 0 && row.avg_datarate <= 0;
            const rowClass = isFailedTransmission ? 'bg-danger bg-opacity-25' : sourceClass;
            
            tbody.innerHTML += `
                <tr class="${rowClass}">
                    <td>${timestamp}</td>
                    <td>${type}</td>
                    <td>SF${row.sf}</td>
                    <td>${row.bw/1000} kHz</td>
                    <td>${row.avg_rssi.toFixed(1)} dBm</td>
                    <td>${row.avg_snr.toFixed(1)} dB</td>
                    <td>${row.avg_datarate.toFixed(1)} bps</td>
                    <td>${row.avg_latency.toFixed(1)} ms</td>
                    <td><span class="badge ${source === 'enhanced' ? 'bg-success' : 'bg-primary'}">${source}</span></td>
                    <td>${compression}</td>
                    <td>${isFailedTransmission ? '<span class="badge bg-danger">Failed</span>' : '<span class="badge bg-success">Success</span>'}</td>
                </tr>
            `;
        });

        // Update charts if they exist
        if (sfChart) {
            updateChart(sfChart, data.sf_metrics, 'sf');
        }
        if (bwChart) {
            updateChart(bwChart, data.bw_metrics, 'bw');
        }

    } catch (error) {
        console.error('Error updating stats:', error);
    }
}

async function updateCharts() {
    try {
        const response = await fetch('/api/metrics');
        const data = await response.json();

        // Prepare data for SF chart
        const sfValues = [...new Set(data.sf_metrics.map(m => m.sf))].sort((a, b) => a - b);
        const standardSfData = sfValues.map(sf => {
            const match = data.sf_metrics.find(m => m.sf === sf && m.source === 'standard');
            return match ? match.avg_datarate : null;
        });
        const enhancedSfData = sfValues.map(sf => {
            const match = data.sf_metrics.find(m => m.sf === sf && m.source === 'enhanced');
            return match ? match.avg_datarate : null;
        });
        const standardSfRssi = sfValues.map(sf => {
            const match = data.sf_metrics.find(m => m.sf === sf && m.source === 'standard');
            return match ? match.avg_rssi : null;
        });
        const enhancedSfRssi = sfValues.map(sf => {
            const match = data.sf_metrics.find(m => m.sf === sf && m.source === 'enhanced');
            return match ? match.avg_rssi : null;
        });
        
        // Create SF chart if not exists
        if (!sfChart) {
            const ctx = document.getElementById('sfChart').getContext('2d');
            sfChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: sfValues.map(sf => `SF${sf}`),
                    datasets: [
                        {
                            label: 'Standard - Data Rate (bps)',
                            data: standardSfData,
                            borderColor: 'rgb(54, 162, 235)',
                            backgroundColor: 'rgba(54, 162, 235, 0.1)',
                            tension: 0.1
                        },
                        {
                            label: 'Enhanced - Data Rate (bps)',
                            data: enhancedSfData,
                            borderColor: 'rgb(75, 192, 192)',
                            backgroundColor: 'rgba(75, 192, 192, 0.1)',
                            tension: 0.1
                        },
                        {
                            label: 'Standard - RSSI (dBm)',
                            data: standardSfRssi,
                            borderColor: 'rgb(255, 159, 64)',
                            backgroundColor: 'rgba(255, 159, 64, 0.1)',
                            tension: 0.1,
                            hidden: true
                        },
                        {
                            label: 'Enhanced - RSSI (dBm)',
                            data: enhancedSfRssi,
                            borderColor: 'rgb(255, 99, 132)',
                            backgroundColor: 'rgba(255, 99, 132, 0.1)',
                            tension: 0.1,
                            hidden: true
                        }
                    ]
                },
                options: {
                    responsive: true,
                    scales: {
                        y: {
                            beginAtZero: false
                        }
                    }
                }
            });
        } else {
            // Update existing chart
            sfChart.data.labels = sfValues.map(sf => `SF${sf}`);
            sfChart.data.datasets[0].data = standardSfData;
            sfChart.data.datasets[1].data = enhancedSfData;
            sfChart.data.datasets[2].data = standardSfRssi;
            sfChart.data.datasets[3].data = enhancedSfRssi;
            sfChart.update();
        }

        // Prepare data for BW chart
        const bwValues = [...new Set(data.bw_metrics.map(m => m.bw))].sort((a, b) => a - b);
        const standardBwData = bwValues.map(bw => {
            const match = data.bw_metrics.find(m => m.bw === bw && m.source === 'standard');
            return match ? match.avg_datarate : null;
        });
        const enhancedBwData = bwValues.map(bw => {
            const match = data.bw_metrics.find(m => m.bw === bw && m.source === 'enhanced');
            return match ? match.avg_datarate : null;
        });
        const standardBwLatency = bwValues.map(bw => {
            const match = data.bw_metrics.find(m => m.bw === bw && m.source === 'standard');
            return match ? match.avg_latency : null;
        });
        const enhancedBwLatency = bwValues.map(bw => {
            const match = data.bw_metrics.find(m => m.bw === bw && m.source === 'enhanced');
            return match ? match.avg_latency : null;
        });
        
        // Create BW chart if not exists
        if (!bwChart) {
            const ctx = document.getElementById('bwChart').getContext('2d');
            bwChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: bwValues.map(bw => `${bw/1000} kHz`),
                    datasets: [
                        {
                            label: 'Standard - Data Rate (bps)',
                            data: standardBwData,
                            borderColor: 'rgb(54, 162, 235)',
                            backgroundColor: 'rgba(54, 162, 235, 0.1)',
                            tension: 0.1
                        },
                        {
                            label: 'Enhanced - Data Rate (bps)',
                            data: enhancedBwData,
                            borderColor: 'rgb(75, 192, 192)',
                            backgroundColor: 'rgba(75, 192, 192, 0.1)',
                            tension: 0.1
                        },
                        {
                            label: 'Standard - Latency (ms)',
                            data: standardBwLatency,
                            borderColor: 'rgb(255, 159, 64)',
                            backgroundColor: 'rgba(255, 159, 64, 0.1)',
                            tension: 0.1,
                            hidden: true
                        },
                        {
                            label: 'Enhanced - Latency (ms)',
                            data: enhancedBwLatency,
                            borderColor: 'rgb(255, 99, 132)',
                            backgroundColor: 'rgba(255, 99, 132, 0.1)',
                            tension: 0.1,
                            hidden: true
                        }
                    ]
                },
                options: {
                    responsive: true,
                    scales: {
                        y: {
                            beginAtZero: true
                        }
                    }
                }
            });
        } else {
            // Update existing chart
            bwChart.data.labels = bwValues.map(bw => `${bw/1000} kHz`);
            bwChart.data.datasets[0].data = standardBwData;
            bwChart.data.datasets[1].data = enhancedBwData;
            bwChart.data.datasets[2].data = standardBwLatency;
            bwChart.data.datasets[3].data = enhancedBwLatency;
            bwChart.update();
        }

    } catch (error) {
        console.error('Error updating charts:', error);
    }
}

function updateChart(chart, metrics, type) {
    // Group metrics by source
    const standardMetrics = metrics.filter(m => m.source === 'standard');
    const enhancedMetrics = metrics.filter(m => m.source === 'enhanced');
    
    // Create labels based on type
    let labels = [];
    if (type === 'sf') {
        // Get unique SF values across both sources
        const sfValues = [...new Set([...standardMetrics.map(m => m.sf), ...enhancedMetrics.map(m => m.sf)])];
        sfValues.sort((a, b) => a - b);
        labels = sfValues.map(sf => `SF${sf}`);
        
        // Prepare data for each source
        chart.data.labels = labels;
        
        // Standard RSSI data
        chart.data.datasets[0].data = sfValues.map(sf => {
            const match = standardMetrics.find(m => m.sf === sf);
            return match ? match.avg_rssi : null;
        });
        
        // Enhanced RSSI data
        chart.data.datasets[1].data = sfValues.map(sf => {
            const match = enhancedMetrics.find(m => m.sf === sf);
            return match ? match.avg_rssi : null;
        });
        
        // Standard Data Rate data
        chart.data.datasets[2].data = sfValues.map(sf => {
            const match = standardMetrics.find(m => m.sf === sf);
            return match ? match.avg_datarate : null;
        });
        
        // Enhanced Data Rate data
        chart.data.datasets[3].data = sfValues.map(sf => {
            const match = enhancedMetrics.find(m => m.sf === sf);
            return match ? match.avg_datarate : null;
        });
    } else {
        // Get unique BW values across both sources
        const bwValues = [...new Set([...standardMetrics.map(m => m.bw), ...enhancedMetrics.map(m => m.bw)])];
        bwValues.sort((a, b) => a - b);
        labels = bwValues.map(bw => `${bw/1000} kHz`);
        
        // Prepare data for each source
        chart.data.labels = labels;
        
        // Standard RSSI data
        chart.data.datasets[0].data = bwValues.map(bw => {
            const match = standardMetrics.find(m => m.bw === bw);
            return match ? match.avg_rssi : null;
        });
        
        // Enhanced RSSI data
        chart.data.datasets[1].data = bwValues.map(bw => {
            const match = enhancedMetrics.find(m => m.bw === bw);
            return match ? match.avg_rssi : null;
        });
        
        // Standard Data Rate data
        chart.data.datasets[2].data = bwValues.map(bw => {
            const match = standardMetrics.find(m => m.bw === bw);
            return match ? match.avg_datarate : null;
        });
        
        // Enhanced Data Rate data
        chart.data.datasets[3].data = bwValues.map(bw => {
            const match = enhancedMetrics.find(m => m.bw === bw);
            return match ? match.avg_datarate : null;
        });
    }
    
    chart.update();
}

// Function to create and update time-series charts
async function updateTimeSeriesCharts() {
    try {
        const response = await fetch('/api/timeseries');
        const data = await response.json();
        
        // Format timestamps for display
        const formattedTimestamps = data.timestamps.map(ts => {
            const date = new Date(ts);
            return date.toLocaleTimeString();
        });
        
        // Create or update RSSI chart
        if (!rssiChart) {
            const ctx = document.getElementById('rssiChart').getContext('2d');
            rssiChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: formattedTimestamps,
                    datasets: [
                        {
                            label: 'Standard RSSI (dBm)',
                            data: data.standard.rssi,
                            borderColor: 'rgb(255, 99, 132)',
                            backgroundColor: 'rgba(255, 99, 132, 0.1)',
                            tension: 0.1
                        },
                        {
                            label: 'Enhanced RSSI (dBm)',
                            data: data.enhanced.rssi,
                            borderColor: 'rgb(75, 192, 192)',
                            backgroundColor: 'rgba(75, 192, 192, 0.1)',
                            tension: 0.1
                        }
                    ]
                },
                options: {
                    responsive: true,
                    plugins: {
                        title: {
                            display: true,
                            text: 'RSSI over Time'
                        }
                    },
                    scales: {
                        y: {
                            title: {
                                display: true,
                                text: 'RSSI (dBm)'
                            }
                        }
                    }
                }
            });
        } else {
            rssiChart.data.labels = formattedTimestamps;
            rssiChart.data.datasets[0].data = data.standard.rssi;
            rssiChart.data.datasets[1].data = data.enhanced.rssi;
            rssiChart.update();
        }
        
        // Create or update SNR chart
        if (!snrChart) {
            const ctx = document.getElementById('snrChart').getContext('2d');
            snrChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: formattedTimestamps,
                    datasets: [
                        {
                            label: 'Standard SNR (dB)',
                            data: data.standard.snr,
                            borderColor: 'rgb(255, 99, 132)',
                            backgroundColor: 'rgba(255, 99, 132, 0.1)',
                            tension: 0.1
                        },
                        {
                            label: 'Enhanced SNR (dB)',
                            data: data.enhanced.snr,
                            borderColor: 'rgb(75, 192, 192)',
                            backgroundColor: 'rgba(75, 192, 192, 0.1)',
                            tension: 0.1
                        }
                    ]
                },
                options: {
                    responsive: true,
                    plugins: {
                        title: {
                            display: true,
                            text: 'SNR over Time'
                        }
                    },
                    scales: {
                        y: {
                            title: {
                                display: true,
                                text: 'SNR (dB)'
                            }
                        }
                    }
                }
            });
        } else {
            snrChart.data.labels = formattedTimestamps;
            snrChart.data.datasets[0].data = data.standard.snr;
            snrChart.data.datasets[1].data = data.enhanced.snr;
            snrChart.update();
        }
        
        // Create or update Latency chart
        if (!latencyChart) {
            const ctx = document.getElementById('latencyChart').getContext('2d');
            latencyChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: formattedTimestamps,
                    datasets: [
                        {
                            label: 'Standard Latency (ms)',
                            data: data.standard.latency,
                            borderColor: 'rgb(255, 99, 132)',
                            backgroundColor: 'rgba(255, 99, 132, 0.1)',
                            tension: 0.1
                        },
                        {
                            label: 'Enhanced Latency (ms)',
                            data: data.enhanced.latency,
                            borderColor: 'rgb(75, 192, 192)',
                            backgroundColor: 'rgba(75, 192, 192, 0.1)',
                            tension: 0.1
                        }
                    ]
                },
                options: {
                    responsive: true,
                    plugins: {
                        title: {
                            display: true,
                            text: 'Latency over Time'
                        }
                    },
                    scales: {
                        y: {
                            title: {
                                display: true,
                                text: 'Latency (ms)'
                            }
                        }
                    }
                }
            });
        } else {
            latencyChart.data.labels = formattedTimestamps;
            latencyChart.data.datasets[0].data = data.standard.latency;
            latencyChart.data.datasets[1].data = data.enhanced.latency;
            latencyChart.update();
        }
        
        // Create or update Data Rate chart
        if (!dataRateChart) {
            const ctx = document.getElementById('dataRateChart').getContext('2d');
            dataRateChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: formattedTimestamps,
                    datasets: [
                        {
                            label: 'Standard Data Rate (bps)',
                            data: data.standard.datarate,
                            borderColor: 'rgb(255, 99, 132)',
                            backgroundColor: 'rgba(255, 99, 132, 0.1)',
                            tension: 0.1
                        },
                        {
                            label: 'Enhanced Data Rate (bps)',
                            data: data.enhanced.datarate,
                            borderColor: 'rgb(75, 192, 192)',
                            backgroundColor: 'rgba(75, 192, 192, 0.1)',
                            tension: 0.1
                        }
                    ]
                },
                options: {
                    responsive: true,
                    plugins: {
                        title: {
                            display: true,
                            text: 'Data Rate over Time'
                        }
                    },
                    scales: {
                        y: {
                            title: {
                                display: true,
                                text: 'Data Rate (bps)'
                            }
                        }
                    }
                }
            });
        } else {
            dataRateChart.data.labels = formattedTimestamps;
            dataRateChart.data.datasets[0].data = data.standard.datarate;
            dataRateChart.data.datasets[1].data = data.enhanced.datarate;
            dataRateChart.update();
        }
        
        // Create or update Compression Ratio chart (enhanced only)
        if (!compressionChart) {
            const ctx = document.getElementById('compressionChart').getContext('2d');
            compressionChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: formattedTimestamps,
                    datasets: [
                        {
                            label: 'Compression Ratio',
                            data: data.enhanced.compression_ratio,
                            borderColor: 'rgb(153, 102, 255)',
                            backgroundColor: 'rgba(153, 102, 255, 0.1)',
                            tension: 0.1
                        }
                    ]
                },
                options: {
                    responsive: true,
                    plugins: {
                        title: {
                            display: true,
                            text: 'Compression Ratio over Time (Enhanced Only)'
                        }
                    },
                    scales: {
                        y: {
                            title: {
                                display: true,
                                text: 'Compression Ratio'
                            },
                            beginAtZero: true
                        }
                    }
                }
            });
        } else {
            compressionChart.data.labels = formattedTimestamps;
            compressionChart.data.datasets[0].data = data.enhanced.compression_ratio;
            compressionChart.update();
        }
        
    } catch (error) {
        console.error('Error updating time-series charts:', error);
    }
}

// Initial data load
updateStats();
updateCharts();
updateTimeSeriesCharts();

// Update data every 10 seconds for more real-time experience
setInterval(updateStats, 10000);
setInterval(updateCharts, 10000);
setInterval(updateTimeSeriesCharts, 10000);
