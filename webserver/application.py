from flask import Flask, render_template, request, jsonify
from flask_cors import CORS
import sqlite3
import json
import os
from dotenv import load_dotenv

load_dotenv()

app = Flask(__name__)
CORS(app)

DB_NAME = os.getenv('DB_NAME', 'data.db')

def get_db():
    conn = sqlite3.connect(DB_NAME)
    conn.row_factory = sqlite3.Row
    return conn

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/transmission', methods=['POST'])
def add_transmission():
    try:
        data = request.get_json()
        conn = get_db()
        cursor = conn.cursor()
        
        cursor.execute('''
            INSERT INTO transmissions (
                type, data, sf, bw, cr, rssi, snr, delay, datarate, latency, source, compression_ratio
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ''', (
            data['type'], data['data'], data['sf'], data['bw'], data['cr'],
            data['rssi'], data['snr'], data['delay'], data['datarate'],
            data['latency'], data.get('source', 'standard'), data.get('compressionRatio', 1.0)
        ))
        
        conn.commit()
        conn.close()
        return jsonify({'status': 'success'}), 201
    except Exception as e:
        return jsonify({'error': str(e)}), 400

@app.route('/api/reception', methods=['POST'])
def add_reception():
    try:
        data = request.get_json()
        conn = get_db()
        cursor = conn.cursor()
        
        cursor.execute('''
            INSERT INTO receptions (data, rssi, snr) VALUES (?, ?, ?)
        ''', (data['data'], data['rssi'], data['snr']))
        
        conn.commit()
        conn.close()
        return jsonify({'status': 'success'}), 201
    except Exception as e:
        return jsonify({'error': str(e)}), 400

@app.route('/api/stats')
def get_stats():
    conn = get_db()
    cursor = conn.cursor()
    
    # Get all transmission metrics with timestamp and type
    cursor.execute('''
        SELECT sf, bw, cr, type, timestamp, source, rssi as avg_rssi, snr as avg_snr,
        datarate as avg_datarate, latency as avg_latency, compression_ratio as avg_compression, data
        FROM transmissions
        ORDER BY timestamp DESC
    ''')
    
    transmission_stats = cursor.fetchall()
    
    # Get reception quality metrics
    cursor.execute('''
        SELECT AVG(rssi) as avg_rssi, AVG(snr) as avg_snr
        FROM receptions
        WHERE timestamp >= datetime('now', '-1 hour')
    ''')
    
    reception_stats = cursor.fetchone()
    
    # Get average data rate and latency by source
    cursor.execute('''
        SELECT source, AVG(datarate) as avg_datarate, AVG(latency) as avg_latency, 
        AVG(compression_ratio) as avg_compression
        FROM transmissions
        GROUP BY source
    ''')
    
    source_metrics = cursor.fetchall()
    
    # Get performance comparison between standard and enhanced
    cursor.execute('''
        SELECT 
            'comparison' as metric,
            (SELECT AVG(datarate) FROM transmissions WHERE source = 'enhanced') / 
            NULLIF((SELECT AVG(datarate) FROM transmissions WHERE source = 'standard'), 0) as datarate_improvement,
            (SELECT AVG(latency) FROM transmissions WHERE source = 'standard') / 
            NULLIF((SELECT AVG(latency) FROM transmissions WHERE source = 'enhanced'), 0) as latency_improvement,
            (SELECT AVG(compression_ratio) FROM transmissions WHERE source = 'enhanced') as compression_ratio
    ''')
    
    comparison = cursor.fetchone()
    
    conn.close()
    
    return jsonify({
        'transmission_stats': [dict(row) for row in transmission_stats],
        'reception_stats': dict(reception_stats) if reception_stats else {},
        'source_metrics': [dict(row) for row in source_metrics],
        'comparison': dict(comparison) if comparison else {}
    })

@app.route('/api/metrics')
def get_metrics():
    conn = get_db()
    cursor = conn.cursor()
    
    # Get metrics for different SF values by source
    cursor.execute('''
        SELECT sf, source, AVG(rssi) as avg_rssi, AVG(snr) as avg_snr,
        AVG(datarate) as avg_datarate, AVG(latency) as avg_latency,
        AVG(compression_ratio) as avg_compression,
        COUNT(*) as count
        FROM transmissions
        WHERE datarate > 0
        GROUP BY sf, source
        ORDER BY sf, source
    ''')
    
    sf_metrics = cursor.fetchall()
    
    # Get metrics for different BW values by source
    cursor.execute('''
        SELECT bw, source, AVG(rssi) as avg_rssi, AVG(snr) as avg_snr,
        AVG(datarate) as avg_datarate, AVG(latency) as avg_latency,
        AVG(compression_ratio) as avg_compression,
        COUNT(*) as count
        FROM transmissions
        WHERE datarate > 0
        GROUP BY bw, source
        ORDER BY bw, source
    ''')
    
    bw_metrics = cursor.fetchall()
    
    # Get historical performance data for optimization analysis
    cursor.execute('''
        SELECT 
            strftime('%Y-%m-%d %H:%M', timestamp) as time_period,
            source,
            AVG(sf) as avg_sf,
            AVG(bw) as avg_bw,
            AVG(cr) as avg_cr,
            AVG(datarate) as avg_datarate,
            AVG(latency) as avg_latency,
            COUNT(*) as transmission_count
        FROM transmissions
        GROUP BY time_period, source
        ORDER BY timestamp DESC
        LIMIT 24
    ''')
    
    historical_data = cursor.fetchall()
    
    conn.close()
    
    return jsonify({
        'sf_metrics': [dict(row) for row in sf_metrics],
        'bw_metrics': [dict(row) for row in bw_metrics],
        'historical_data': [dict(row) for row in historical_data]
    })

@app.route('/api/timeseries')
def get_timeseries():
    conn = get_db()
    cursor = conn.cursor()
    
    # Get all transmissions with timestamp for time-series analysis
    cursor.execute('''
        SELECT timestamp, source, rssi, snr, latency, datarate, compression_ratio
        FROM transmissions
        WHERE datarate > 0
        ORDER BY timestamp ASC
    ''')
    
    transmissions = cursor.fetchall()
    
    # Format the results
    result = {
        'timestamps': [],
        'standard': {
            'rssi': [],
            'snr': [],
            'latency': [],
            'datarate': [],
            'compression_ratio': []
        },
        'enhanced': {
            'rssi': [],
            'snr': [],
            'latency': [],
            'datarate': [],
            'compression_ratio': []
        }
    }
    
    # Process the data
    for row in transmissions:
        timestamp, source, rssi, snr, latency, datarate, compression_ratio = row
        
        # Only add timestamp once
        if timestamp not in result['timestamps']:
            result['timestamps'].append(timestamp)
        
        # Add data to appropriate source
        if source == 'standard':
            result['standard']['rssi'].append(rssi)
            result['standard']['snr'].append(snr)
            result['standard']['latency'].append(latency)
            result['standard']['datarate'].append(datarate)
            result['standard']['compression_ratio'].append(compression_ratio)
        elif source == 'enhanced':
            result['enhanced']['rssi'].append(rssi)
            result['enhanced']['snr'].append(snr)
            result['enhanced']['latency'].append(latency)
            result['enhanced']['datarate'].append(datarate)
            result['enhanced']['compression_ratio'].append(compression_ratio)
    
    return jsonify(result)

# This is required for AWS Elastic Beanstalk - it looks for an object named 'application'
application = app

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8000)
