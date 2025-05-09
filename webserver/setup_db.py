import sqlite3
import os
import sys
from datetime import datetime

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
    
    # Write to CSV file
    with open(filename, 'w') as f:
        # Write header
        f.write(','.join(columns) + '\n')
        
        # Write data rows
        for row in transmissions:
            f.write(','.join([str(item) for item in row]) + '\n')
    
    print(f"Data exported to {filename} successfully!")
    conn.close()

if __name__ == '__main__':
    if len(sys.argv) > 1:
        if sys.argv[1] == '--clear':
            clear_database()
        elif sys.argv[1] == '--extract' and len(sys.argv) > 2:
            export_to_csv(sys.argv[2])
        else:
            print("Usage: python setup_db.py [--clear | --extract filename.csv]")
    else:
        create_tables()