import sqlite3
import os
import sys
from datetime import datetime
import pandas as pd
import matplotlib.pyplot as plt
from io import StringIO

def create_tables():
    conn = sqlite3.connect('data.db')
    cursor = conn.cursor()

    # Create transmission table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS transmissions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            type TEXT NOT NULL,
            data TEXT NOT NULL,
            sf INTEGER NOT NULL,
            bw INTEGER NOT NULL,
            cr INTEGER NOT NULL,
            rssi INTEGER,
            snr REAL,
            delay INTEGER,
            datarate REAL,
            latency INTEGER,
            source TEXT NOT NULL,
            compression_ratio REAL
        )
    ''')

    # Create reception table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS receptions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            data TEXT NOT NULL,
            rssi INTEGER,
            snr REAL
        )
    ''')

    # Create index for faster queries
    cursor.execute('''
        CREATE INDEX IF NOT EXISTS idx_transmissions_timestamp ON transmissions(timestamp)
    ''')
    cursor.execute('''
        CREATE INDEX IF NOT EXISTS idx_receptions_timestamp ON receptions(timestamp)
    ''')

    conn.commit()
    conn.close()
    print("Database tables created successfully!")

def clear_database():
    # Check if database file exists
    if os.path.exists('data.db'):
        conn = sqlite3.connect('data.db')
        cursor = conn.cursor()
        
        # Drop existing tables
        cursor.execute('DROP TABLE IF EXISTS transmissions')
        cursor.execute('DROP TABLE IF EXISTS receptions')
        
        conn.commit()
        conn.close()
        print("Existing tables dropped.")
    else:
        print("Database file does not exist. Creating new database.")
    
    # Create tables with the latest schema
    create_tables()

def export_to_csv(filename):
    if not os.path.exists('data.db'):
        print("Database file does not exist. Nothing to export.")
        return
    
    conn = sqlite3.connect('data.db')
    cursor = conn.cursor()
    
    # Get all data from transmissions table
    cursor.execute('SELECT * FROM transmissions')
    transmissions = cursor.fetchall()
    
    # Get column names
    cursor.execute('PRAGMA table_info(transmissions)')
    columns = [col[1] for col in cursor.fetchall()]
    
    # Use pandas to properly handle CSV export with potential commas in data
    try:
        import pandas as pd
        # Convert to DataFrame
        df = pd.DataFrame(transmissions, columns=columns)
        # Export to CSV with proper escaping
        df.to_csv(filename, index=False, quoting=1)  # QUOTE_ALL mode
        print(f"Data exported to {filename} successfully!")
        return columns, df
    except ImportError:
        # Fallback if pandas is not available
        import csv
        with open(filename, 'w', newline='') as f:
            writer = csv.writer(f, quoting=csv.QUOTE_ALL)
            # Write header
            writer.writerow(columns)
            # Write data rows
            for row in transmissions:
                writer.writerow([str(item) for item in row])
        print(f"Data exported to {filename} successfully!")
        return columns, transmissions
    
    finally:
        conn.close()

def analyse_data(output_file=None):
    if not os.path.exists('data.db'):
        print("Database file does not exist. Nothing to analyze.")
        return
    
    # Export data to a DataFrame directly
    columns, df = export_to_csv('temp_analysis.csv')
    
    # If we got a DataFrame directly, use it; otherwise read from the CSV
    if not isinstance(df, pd.DataFrame):
        # Read the CSV file with pandas
        df = pd.read_csv('temp_analysis.csv', parse_dates=["timestamp"])
    else:
        # Ensure timestamp is parsed as datetime
        df['timestamp'] = pd.to_datetime(df['timestamp'])
    
    # Clean up temporary file
    if os.path.exists('temp_analysis.csv'):
        os.remove('temp_analysis.csv')
    
    # Check if we have data from both sources
    if 'source' not in df.columns or len(df['source'].unique()) < 2:
        print("Warning: Need data from both standard and enhanced sources for comparison.")
        if 'source' not in df.columns:
            print("'source' column not found in data.")
            return
        elif len(df['source'].unique()) < 2:
            print(f"Only found data from {df['source'].unique()[0]} source.")
            return
    
    # Separate standard and enhanced
    df['source'] = df['source'].astype(str)
    df_standard = df[df['source'] == 'standard']
    df_enhanced = df[df['source'] == 'enhanced']
    
    # Print summary statistics
    print("\n===== ANALYSIS SUMMARY =====\n")
    print(f"Total transmissions: {len(df)}")
    print(f"Standard transmissions: {len(df_standard)}")
    print(f"Enhanced transmissions: {len(df_enhanced)}\n")
    
    # Calculate performance metrics
    if not df_standard.empty and not df_enhanced.empty:
        avg_standard_datarate = df_standard['datarate'].mean()
        avg_enhanced_datarate = df_enhanced['datarate'].mean()
        datarate_improvement = avg_enhanced_datarate / avg_standard_datarate if avg_standard_datarate > 0 else 0
        
        avg_standard_latency = df_standard['latency'].mean()
        avg_enhanced_latency = df_enhanced['latency'].mean()
        latency_improvement = avg_standard_latency / avg_enhanced_latency if avg_enhanced_latency > 0 else 0
        
        avg_compression = df_enhanced['compression_ratio'].mean()
        
        print("Performance Comparison:")
        print(f"Data Rate: Standard = {avg_standard_datarate:.2f} bps, Enhanced = {avg_enhanced_datarate:.2f} bps")
        print(f"Data Rate Improvement: {datarate_improvement:.2f}x faster with Enhanced")
        print(f"Latency: Standard = {avg_standard_latency:.2f} ms, Enhanced = {avg_enhanced_latency:.2f} ms")
        print(f"Latency Improvement: {latency_improvement:.2f}x faster with Enhanced")
        print(f"Average Compression Ratio: {avg_compression:.2f}x\n")
    
    # Set up the plots
    fig, axs = plt.subplots(3, 1, figsize=(12, 12), sharex=True)
    
    # RSSI
    if not df_standard.empty:
        axs[0].plot(df_standard["timestamp"], df_standard["rssi"], 'ro-', label="Standard")
    if not df_enhanced.empty:
        axs[0].plot(df_enhanced["timestamp"], df_enhanced["rssi"], 'go-', label="Enhanced")
    axs[0].set_ylabel("RSSI (dBm)")
    axs[0].set_title("RSSI over Time")
    axs[0].legend()
    axs[0].grid(True)
    
    # SNR
    if not df_standard.empty:
        axs[1].plot(df_standard["timestamp"], df_standard["snr"], 'ro-', label="Standard")
    if not df_enhanced.empty:
        axs[1].plot(df_enhanced["timestamp"], df_enhanced["snr"], 'go-', label="Enhanced")
    axs[1].set_ylabel("SNR (dB)")
    axs[1].set_title("SNR over Time")
    axs[1].legend()
    axs[1].grid(True)
    
    # Latency
    if not df_standard.empty:
        axs[2].plot(df_standard["timestamp"], df_standard["latency"], 'ro-', label="Standard")
    if not df_enhanced.empty:
        axs[2].plot(df_enhanced["timestamp"], df_enhanced["latency"], 'go-', label="Enhanced")
    axs[2].set_ylabel("Latency (ms)")
    axs[2].set_title("Latency over Time")
    axs[2].legend()
    axs[2].grid(True)
    
    plt.xlabel("Timestamp")
    plt.tight_layout()
    plt.xticks(rotation=45)
    
    # Save or show the plot
    if output_file:
        plt.savefig(output_file)
        print(f"Analysis plot saved to {output_file}")
    else:
        plt.show()
    
    # Additional analysis - create a second figure for data rate and compression
    fig2, axs2 = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    
    # Data Rate
    if not df_standard.empty:
        axs2[0].plot(df_standard["timestamp"], df_standard["datarate"], 'ro-', label="Standard")
    if not df_enhanced.empty:
        axs2[0].plot(df_enhanced["timestamp"], df_enhanced["datarate"], 'go-', label="Enhanced")
    axs2[0].set_ylabel("Data Rate (bps)")
    axs2[0].set_title("Data Rate over Time")
    axs2[0].legend()
    axs2[0].grid(True)
    
    # Compression Ratio (only for enhanced)
    if not df_enhanced.empty:
        axs2[1].plot(df_enhanced["timestamp"], df_enhanced["compression_ratio"], 'bo-', label="Compression Ratio")
        axs2[1].set_ylabel("Compression Ratio")
        axs[1].set_title("Compression Ratio over Time (Enhanced Only)")
        axs2[1].legend()
        axs2[1].grid(True)
    
    plt.xlabel("Timestamp")
    plt.tight_layout()
    plt.xticks(rotation=45)
    
    # Save or show the second plot
    if output_file:
        output_name, output_ext = os.path.splitext(output_file)
        second_output = f"{output_name}_datarate{output_ext}"
        plt.savefig(second_output)
        print(f"Data rate analysis plot saved to {second_output}")
    else:
        plt.show()

def import_from_csv(filename):
    if not os.path.exists(filename):
        print(f"CSV file '{filename}' does not exist.")
        return
    
    # Ask user for action
    action = ''
    while action.lower() not in ['o', 'a']:
        action = input("Do you want to (O)verwrite or (A)ppend to the transmissions table? [O/A]: ").strip().lower()
    
    conn = sqlite3.connect('data.db')
    cursor = conn.cursor()
    
    if action == 'o':
        # Drop and recreate the transmissions table only
        cursor.execute('DROP TABLE IF EXISTS transmissions')
        conn.commit()
        # Recreate only the transmissions table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS transmissions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                type TEXT NOT NULL,
                data TEXT NOT NULL,
                sf INTEGER NOT NULL,
                bw INTEGER NOT NULL,
                cr INTEGER NOT NULL,
                rssi INTEGER,
                snr REAL,
                delay INTEGER,
                datarate REAL,
                latency INTEGER,
                source TEXT NOT NULL,
                compression_ratio REAL
            )
        ''')
        conn.commit()
        print("Transmissions table overwritten.")
    else:
        print("Appending to existing transmissions table.")
    
    # Read CSV
    df = pd.read_csv(filename)
    
    # Remove id column if present (let SQLite autoincrement)
    if 'id' in df.columns:
        df = df.drop(columns=['id'])
    
    # Define required columns and their default values
    required_columns = {
        'type': 'unknown',
        'data': '',
        'sf': 7,
        'bw': 125,
        'cr': 4,
        'source': 'standard'
    }
    
    # Fill missing required columns with default values
    for col, default_value in required_columns.items():
        if col not in df.columns:
            df[col] = default_value
        else:
            # Fill NULL values with default values
            df[col] = df[col].fillna(default_value)
    
    # Convert timestamp to string if needed
    if 'timestamp' in df.columns:
        df['timestamp'] = df['timestamp'].astype(str)
    
    # Fill missing optional columns with None
    optional_columns = ['rssi', 'snr', 'delay', 'datarate', 'latency', 'compression_ratio']
    for col in optional_columns:
        if col not in df.columns:
            df[col] = None
    
    # Reorder columns to match database schema
    expected_cols = ['timestamp', 'type', 'data', 'sf', 'bw', 'cr', 'rssi', 'snr', 
                    'delay', 'datarate', 'latency', 'source', 'compression_ratio']
    df = df[expected_cols]
    
    # Insert into DB
    try:
        df.to_sql('transmissions', conn, if_exists='append', index=False)
        print(f"Imported {len(df)} rows from {filename} into transmissions table.")
    except sqlite3.IntegrityError as e:
        print(f"Error importing data: {str(e)}")
        print("Please check that all required columns have valid values.")
    finally:
        conn.close()

if __name__ == '__main__':
    if len(sys.argv) > 1:
        if sys.argv[1] == '--clear':
            clear_database()
        elif sys.argv[1] == '--extract' and len(sys.argv) > 2:
            export_to_csv(sys.argv[2])
        elif sys.argv[1] == '--analyse' or sys.argv[1] == '--analyze':
            # Check if output file is provided
            output_file = None
            if len(sys.argv) > 2:
                output_file = sys.argv[2]
            analyse_data(output_file)
        elif sys.argv[1] == '--import' and len(sys.argv) > 2:
            import_from_csv(sys.argv[2])
        else:
            print("Usage: python setup_db.py [--clear | --extract filename.csv | --analyse [output_file.png] | --import transmissions.csv]")
    else:
        create_tables()