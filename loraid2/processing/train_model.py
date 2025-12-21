#!/usr/bin/env python3
"""
Train and convert ML model for LoRa parameter optimization
"""

import numpy as np
import tensorflow as tf
from tensorflow import keras
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.metrics import confusion_matrix, r2_score
import pandas as pd
import os

def plot_training_history(history):
    """Plot training metrics history"""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 5))
    
    # Loss plot
    ax1.plot(history.history['loss'], label='Training Loss')
    ax1.plot(history.history['val_loss'], label='Validation Loss')
    ax1.set_title('Model Loss')
    ax1.set_xlabel('Epoch')
    ax1.set_ylabel('Loss')
    ax1.legend()
    ax1.grid(True)
    
    # MAE plot
    ax2.plot(history.history['mae'], label='Training MAE')
    ax2.plot(history.history['val_mae'], label='Validation MAE')
    ax2.set_title('Model MAE')
    ax2.set_xlabel('Epoch')
    ax2.set_ylabel('MAE')
    ax2.legend()
    ax2.grid(True)
    
    plt.tight_layout()
    plt.savefig('training_history.png')
    plt.close()

def plot_feature_importance(model, feature_names):
    """Plot feature importance using gradient-based analysis"""
    # Create sample input
    sample_input = np.random.normal(size=(1000, len(feature_names)))
    
    # Calculate gradients for each feature
    with tf.GradientTape() as tape:
        inputs = tf.convert_to_tensor(sample_input, dtype=tf.float32)
        tape.watch(inputs)
        predictions = model(inputs)
        mean_pred = tf.reduce_mean(predictions)
    
    gradients = tape.gradient(mean_pred, inputs)
    importance = np.abs(gradients.numpy()).mean(axis=0)
    
    # Plot feature importance
    plt.figure(figsize=(10, 6))
    sns.barplot(x=importance, y=feature_names)
    plt.title('Feature Importance')
    plt.xlabel('Mean Absolute Gradient')
    plt.tight_layout()
    plt.savefig('feature_importance.png')
    plt.close()

def plot_correlation_matrix(X, y, feature_names, target_names):
    """Plot correlation matrix of features and targets"""
    data = np.hstack([X, y])
    columns = feature_names + target_names
    df = pd.DataFrame(data, columns=columns)
    
    plt.figure(figsize=(12, 10))
    sns.heatmap(df.corr(), annot=True, cmap='coolwarm', center=0)
    plt.title('Feature-Target Correlation Matrix')
    plt.tight_layout()
    plt.savefig('correlation_matrix.png')
    plt.close()

def plot_prediction_scatter(y_true, y_pred, target_names):
    """Plot scatter plots of predicted vs actual values"""
    fig, axes = plt.subplots(2, 2, figsize=(15, 15))
    axes = axes.ravel()
    
    for i, (name, ax) in enumerate(zip(target_names, axes)):
        ax.scatter(y_true[:, i], y_pred[:, i], alpha=0.5)
        ax.plot([y_true[:, i].min(), y_true[:, i].max()], 
                [y_true[:, i].min(), y_true[:, i].max()], 
                'r--', lw=2)
        ax.set_title(f'{name}: R² = {r2_score(y_true[:, i], y_pred[:, i]):.3f}')
        ax.set_xlabel('True Values')
        ax.set_ylabel('Predictions')
        ax.grid(True)
    
    plt.tight_layout()
    plt.savefig('prediction_scatter.png')
    plt.close()

def plot_parameter_distributions(y_true, y_pred, target_names):
    """Plot distribution of true vs predicted parameters"""
    fig, axes = plt.subplots(2, 2, figsize=(15, 15))
    axes = axes.ravel()
    
    for i, (name, ax) in enumerate(zip(target_names, axes)):
        sns.kdeplot(data=y_true[:, i], label='True', ax=ax)
        sns.kdeplot(data=y_pred[:, i], label='Predicted', ax=ax)
        ax.set_title(f'{name} Distribution')
        ax.legend()
        ax.grid(True)
    
    plt.tight_layout()
    plt.savefig('parameter_distributions.png')
    plt.close()

def calculate_accuracy(y_true, y_pred, tolerance=0.1):
    """Calculate accuracy with tolerance for each parameter"""
    # Denormalize predictions
    sf_true = y_true[:, 0] * 5 + 7
    cr_true = y_true[:, 1] * 3 + 5
    bw_true = y_true[:, 2] * 2
    tp_true = y_true[:, 3] * 18 + 2
    
    sf_pred = y_pred[:, 0] * 5 + 7
    cr_pred = y_pred[:, 1] * 3 + 5
    bw_pred = y_pred[:, 2] * 2
    tp_pred = y_pred[:, 3] * 18 + 2
    
    # Calculate accuracy for each parameter
    sf_acc = np.mean(np.abs(sf_true - sf_pred) <= tolerance * 5) * 100
    cr_acc = np.mean(np.abs(cr_true - cr_pred) <= tolerance * 3) * 100
    bw_acc = np.mean(np.abs(bw_true - bw_pred) <= tolerance * 2) * 100
    tp_acc = np.mean(np.abs(tp_true - tp_pred) <= tolerance * 18) * 100
    
    # Overall accuracy
    overall_acc = np.mean([sf_acc, cr_acc, bw_acc, tp_acc])
    
    return {
        'SF': sf_acc,
        'CR': cr_acc,
        'BW': bw_acc,
        'TP': tp_acc,
        'Overall': overall_acc
    }

def create_model():
    """Create the neural network model"""
    model = keras.Sequential([
        keras.layers.Dense(64, activation='relu', input_shape=(4,)),
        keras.layers.BatchNormalization(),
        keras.layers.Dropout(0.2),
        keras.layers.Dense(32, activation='relu'),
        keras.layers.BatchNormalization(),
        keras.layers.Dropout(0.2),
        keras.layers.Dense(4, activation='sigmoid')  # 4 outputs: SF, CR, BW, TP
    ])
    
    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=0.001),
        loss='mse',
        metrics=['mae']
    )
    
    return model

def generate_training_data(n_samples=10000):
    """Generate synthetic training data"""
    # Input features
    rssi = np.random.normal(-90, 20, n_samples)  # RSSI values
    snr = np.random.normal(0, 10, n_samples)     # SNR values
    battery = np.random.uniform(0.2, 1.0, n_samples)  # Battery levels
    payload = np.random.randint(1, 255, n_samples)    # Payload sizes
    
    # Calculate optimal parameters based on conditions
    sf = np.clip(np.round(((-rssi - 50) / 10) + 7), 7, 12)
    cr = np.clip(np.round((1 - battery) * 3 + 5), 5, 8)
    bw = np.where(payload > 100, 2,           # 500kHz
                 np.where(payload > 50, 1, 0)) # 250kHz or 125kHz
    tp = np.clip(np.round((1 - battery) * 18 + 2), 2, 20)
    
    # Normalize outputs to 0-1 range
    sf_norm = (sf - 7) / 5
    cr_norm = (cr - 5) / 3
    bw_norm = bw / 2
    tp_norm = (tp - 2) / 18
    
    X = np.column_stack([rssi, snr, battery, payload])
    y = np.column_stack([sf_norm, cr_norm, bw_norm, tp_norm])
    
    return X, y

def convert_to_tflite(model, quantize=True):
    """Convert Keras model to TFLite format"""
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    
    if quantize:
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_types = [tf.float16]
    
    tflite_model = converter.convert()
    
    # Save model
    with open('../sender/model.h', 'w') as f:
        f.write('#ifndef MODEL_H\n')
        f.write('#define MODEL_H\n\n')
        f.write('const unsigned char g_model[] = {\n')
        
        # Write model data as C array
        for i, byte in enumerate(tflite_model):
            f.write(f'0x{byte:02x},')
            if (i + 1) % 12 == 0:
                f.write('\n')
        
        f.write('\n};\n\n')
        f.write('#endif // MODEL_H\n')
    
    print(f"Model size: {len(tflite_model) / 1024:.2f} KB")

def main():
    # Set style for plots
    plt.style.use('seaborn-v0_8')  # Updated style name
    sns.set_palette("husl")
    
    # Feature and target names
    feature_names = ['RSSI', 'SNR', 'Battery', 'Payload']
    target_names = ['SF', 'CR', 'BW', 'TP']
    
    # Generate training data
    print("Generating training data...")
    X, y = generate_training_data()
    
    # Plot correlation matrix
    print("Plotting correlation matrix...")
    plot_correlation_matrix(X, y, feature_names, target_names)
    
    # Split data
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2)
    
    # Scale features
    scaler = StandardScaler()
    X_train_scaled = scaler.fit_transform(X_train)
    X_test_scaled = scaler.transform(X_test)
    
    # Create and train model
    print("Training model...")
    model = create_model()
    history = model.fit(
        X_train_scaled, y_train,
        validation_split=0.2,
        epochs=100,
        batch_size=32,
        callbacks=[
            keras.callbacks.EarlyStopping(patience=10, restore_best_weights=True)
        ]
    )
    
    # Plot training history
    print("Plotting training history...")
    plot_training_history(history)
    
    # Plot feature importance
    print("Analyzing feature importance...")
    plot_feature_importance(model, feature_names)
    
    # Generate predictions
    y_pred = model.predict(X_test_scaled)
    
    # Plot prediction scatter plots
    print("Plotting prediction analysis...")
    plot_prediction_scatter(y_test, y_pred, target_names)
    
    # Plot parameter distributions
    print("Plotting parameter distributions...")
    plot_parameter_distributions(y_test, y_pred, target_names)
    
    # Calculate and print accuracies
    accuracies = calculate_accuracy(y_test, y_pred)
    print("\nModel Accuracy (with 10% tolerance):")
    print(f"Spreading Factor: {accuracies['SF']:.1f}%")
    print(f"Coding Rate: {accuracies['CR']:.1f}%")
    print(f"Bandwidth: {accuracies['BW']:.1f}%")
    print(f"Transmission Power: {accuracies['TP']:.1f}%")
    print(f"Overall Accuracy: {accuracies['Overall']:.1f}%")
    
    # Convert and save model
    print("\nConverting model to TFLite...")
    convert_to_tflite(model)
    print("Model converted and saved to sender/model.h")
    
    print("\nVisualization files saved:")
    print("- correlation_matrix.png")
    print("- training_history.png")
    print("- feature_importance.png")
    print("- prediction_scatter.png")
    print("- parameter_distributions.png")

if __name__ == "__main__":
    main() 