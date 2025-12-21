#!/usr/bin/env python3
"""
LoRa Optimization using Edge Machine Learning
A comprehensive approach to dynamic parameter selection
"""

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import mean_squared_error, mean_absolute_error
import tensorflow as tf
from tensorflow.keras.models import Sequential, Model
from tensorflow.keras.layers import Dense, Input, Dropout, BatchNormalization
from tensorflow.keras.optimizers import Adam
from tensorflow.keras.callbacks import EarlyStopping, ModelCheckpoint
import tensorflow.keras.backend as K
import random
import math
import os
import json
from datetime import datetime

# Configure GPU
gpus = tf.config.list_physical_devices('GPU')
if gpus:
    try:
        # Currently, memory growth needs to be the same across GPUs
        for gpu in gpus:
            tf.config.experimental.set_memory_growth(gpu, True)
        logical_gpus = tf.config.list_logical_devices('GPU')
        print(f"{len(gpus)} Physical GPUs, {len(logical_gpus)} Logical GPUs")
    except RuntimeError as e:
        # Memory growth must be set before GPUs have been initialized
        print(e)

# Set random seeds for reproducibility
np.random.seed(42)
tf.random.set_seed(42)
random.seed(42)

# Configure visualization settings
plt.style.use('seaborn-v0_8-whitegrid')
sns.set(font_scale=1.2)
plt.rcParams['figure.figsize'] = (14, 8)
plt.rcParams['axes.labelsize'] = 14
plt.rcParams['axes.titlesize'] = 16
plt.rcParams['xtick.labelsize'] = 12
plt.rcParams['ytick.labelsize'] = 12

def calculate_toa(payload_size, sf, bw=125, cr=5, explicit_header=True, crc_on=True):
    """Calculate Time on Air for LoRa transmission"""
    # Calculate number of symbols in the preamble
    n_preamble = 8
    
    # Calculate number of symbols
    # Payload size + header (if explicit) + CRC (if enabled)
    header_size = 0 if not explicit_header else 20  # bits
    crc_size = 0 if not crc_on else 16  # bits
    
    # Convert to bytes (rounded up)
    header_bytes = (header_size + 7) // 8
    crc_bytes = (crc_size + 7) // 8
    
    # Total size in bytes
    total_size = payload_size + header_bytes + crc_bytes
    
    # Number of useful bits per symbol
    # SF - (if SF is greater than 10, we lose 1 bit per symbol due to LNA optimization)
    bits_per_symbol = sf - (2 if sf > 10 else 0)
    
    # Number of bits that fit into a complete set of SF symbols
    # Each set takes 2^SF chips
    bits_per_codeword = 4 * bits_per_symbol
    
    # Coding overhead
    coding_rate = cr / (cr + 4)
    
    # Number of symbols for payload
    # Ceil[(8 * total_size) / (coding_rate * bits_per_codeword)] * SF
    n_symbols_payload = math.ceil(8 * total_size / (coding_rate * bits_per_codeword)) * sf
    
    # Total number of symbols
    n_symbols = n_preamble + 4.25 + n_symbols_payload
    
    # Time per symbol
    t_symbol = (2**sf) / bw  # seconds
    
    # Total time on air
    toa = n_symbols * t_symbol
    
    return toa * 1000  # convert to milliseconds

def calculate_data_rate(sf, bw, cr=5):
    """Calculate data rate for LoRa transmission"""
    # Number of useful bits per symbol
    bits_per_symbol = sf - (2 if sf > 10 else 0)
    
    # Coding rate
    coding_rate = cr / (cr + 4)
    
    # Data rate in bits per second
    data_rate = (bits_per_symbol * bw * coding_rate) / (2**sf)
    
    return data_rate

def generate_synthetic_data(n_samples=1000):
    """Generate synthetic dataset for training"""
    # Environmental conditions
    snr = np.random.normal(0, 10, n_samples)  # dB
    rssi = np.random.normal(-90, 20, n_samples)  # dBm
    interference = np.random.uniform(0, 1, n_samples)
    
    # Payload characteristics
    payload_size = np.random.randint(1, 250, n_samples)  # bytes
    criticality = np.random.uniform(0, 1, n_samples)
    
    # Network conditions
    network_load = np.random.uniform(0, 1, n_samples)
    retransmission_count = np.random.poisson(0.5, n_samples)
    
    # Device state
    battery_level = np.random.uniform(0.2, 1, n_samples)
    temperature = np.random.normal(25, 5, n_samples)  # Celsius
    
    # Calculate optimal parameters based on conditions
    optimal_sf = np.clip(np.round(7 + (snr + 20) / 5), 7, 12)
    optimal_cr = np.clip(np.round(5 + criticality * 3), 5, 8)
    optimal_bw = np.where(payload_size > 100, 500, np.where(payload_size > 50, 250, 125))
    optimal_tp = np.clip(np.round(2 + (1 - battery_level) * 18), 2, 20)
    
    # Create feature matrix
    X = np.column_stack([
        snr, rssi, interference,
        payload_size, criticality,
        network_load, retransmission_count,
        battery_level, temperature
    ])
    
    # Create target matrix
    y = np.column_stack([optimal_sf, optimal_cr, optimal_bw, optimal_tp])
    
    return X, y

def create_model(input_dim):
    """Create neural network model for parameter prediction"""
    # Input layer
    inputs = Input(shape=(input_dim,))
    
    # Shared layers
    x = Dense(64, activation='relu')(inputs)
    x = BatchNormalization()(x)
    x = Dropout(0.2)(x)
    
    x = Dense(32, activation='relu')(x)
    x = BatchNormalization()(x)
    x = Dropout(0.2)(x)
    
    # Output branches
    sf_output = Dense(1, activation='linear', name='sf')(x)
    cr_output = Dense(1, activation='linear', name='cr')(x)
    bw_output = Dense(1, activation='linear', name='bw')(x)
    tp_output = Dense(1, activation='linear', name='tp')(x)
    
    # Create model
    model = Model(inputs=inputs, outputs=[sf_output, cr_output, bw_output, tp_output])
    
    # Custom loss function to handle different scales
    def custom_loss(y_true, y_pred):
        return K.mean(K.square(y_true - y_pred))
    
    # Compile model with metrics for each output
    model.compile(
        optimizer=Adam(learning_rate=0.001),
        loss={'sf': custom_loss, 'cr': custom_loss, 'bw': custom_loss, 'tp': custom_loss},
        metrics={
            'sf': ['mae'],
            'cr': ['mae'],
            'bw': ['mae'],
            'tp': ['mae']
        }
    )
    
    return model

def train_model(X, y, epochs=100, batch_size=32):
    """Train the model"""
    # Split data
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)
    
    # Scale features
    scaler = StandardScaler()
    X_train_scaled = scaler.fit_transform(X_train)
    X_test_scaled = scaler.transform(X_test)
    
    # Create model
    model = create_model(X.shape[1])
    
    # Callbacks
    callbacks = [
        EarlyStopping(monitor='val_loss', patience=10, restore_best_weights=True),
        ModelCheckpoint('best_model.h5', monitor='val_loss', save_best_only=True)
    ]
    
    # Train model
    history = model.fit(
        X_train_scaled,
        {'sf': y_train[:, 0], 'cr': y_train[:, 1], 'bw': y_train[:, 2], 'tp': y_train[:, 3]},
        validation_split=0.2,
        epochs=epochs,
        batch_size=batch_size,
        callbacks=callbacks,
        verbose=1
    )
    
    return model, history, scaler, X_test_scaled, y_test

def evaluate_model(model, X_test, y_test):
    """Evaluate model performance"""
    # Make predictions
    y_pred = model.predict(X_test)
    
    # Calculate metrics
    metrics = {}
    for i, param in enumerate(['sf', 'cr', 'bw', 'tp']):
        mse = mean_squared_error(y_test[:, i], y_pred[i])
        mae = mean_absolute_error(y_test[:, i], y_pred[i])
        metrics[param] = {'mse': mse, 'mae': mae}
    
    return metrics

def plot_training_history(history):
    """Plot training history"""
    plt.figure(figsize=(12, 6))
    plt.plot(history.history['loss'], label='Training Loss')
    plt.plot(history.history['val_loss'], label='Validation Loss')
    plt.title('Model Training History')
    plt.xlabel('Epoch')
    plt.ylabel('Loss')
    plt.legend()
    plt.grid(True)
    plt.show()

def save_synthetic_data_to_csv(X, y, filename='lora_synthetic_data.csv'):
    """Save synthetic data to CSV file with descriptive column names"""
    # Create column names
    feature_columns = [
        'snr_dB', 'rssi_dBm', 'interference',
        'payload_size_bytes', 'criticality',
        'network_load', 'retransmission_count',
        'battery_level', 'temperature_C'
    ]
    
    target_columns = ['optimal_sf', 'optimal_cr', 'optimal_bw', 'optimal_tp']
    
    # Create DataFrame
    df = pd.DataFrame(
        np.column_stack([X, y]),
        columns=feature_columns + target_columns
    )
    
    # Save to CSV
    df.to_csv(filename, index=False)
    print(f"\nSynthetic data saved to {filename}")
    
    # Print basic statistics
    print("\nData Statistics:")
    print(df.describe().round(2))
    
    return df

def main():
    """Main function"""
    print("Generating synthetic data...")
    X, y = generate_synthetic_data()
    
    # Save synthetic data to CSV
    df = save_synthetic_data_to_csv(X, y)
    
    print("\nTraining model...")
    model, history, scaler, X_test, y_test = train_model(X, y)
    
    print("\nEvaluating model...")
    metrics = evaluate_model(model, X_test, y_test)
    
    print("\nModel Performance Metrics:")
    for param, values in metrics.items():
        print(f"{param.upper()}:")
        print(f"  MSE: {values['mse']:.4f}")
        print(f"  MAE: {values['mae']:.4f}")
    
    print("\nPlotting training history...")
    plot_training_history(history)
    
    # Save model and scaler
    model.save('lora_optimization_model.h5')
    with open('scaler.pkl', 'wb') as f:
        import pickle
        pickle.dump(scaler, f)
    
    print("\nModel and scaler saved successfully!")

if __name__ == "__main__":
    main() 