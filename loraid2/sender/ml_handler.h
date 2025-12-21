#ifndef ML_HANDLER_H
#define ML_HANDLER_H

#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model.h" // This will be our converted TFLite model

class MLHandler {
private:
    // TFLite globals
    tflite::ErrorReporter* error_reporter;
    const tflite::Model* model;
    tflite::MicroInterpreter* interpreter;
    TfLiteTensor* input;
    TfLiteTensor* output;
    
    // Create an area of memory to use for input, output, and intermediate arrays
    static const int kTensorArenaSize = 50 * 1024;
    uint8_t tensor_arena[kTensorArenaSize];
    
    // Input feature normalization parameters
    const float rssi_mean = -90.0f;
    const float rssi_std = 20.0f;
    const float snr_mean = 0.0f;
    const float snr_std = 10.0f;
    
public:
    MLHandler() {
        static tflite::MicroErrorReporter micro_error_reporter;
        error_reporter = &micro_error_reporter;
        
        // Map the model into a usable data structure
        model = tflite::GetModel(g_model);
        if (model->version() != TFLITE_SCHEMA_VERSION) {
            error_reporter->Report("Model version mismatch!");
            return;
        }
        
        // This pulls in all the operation implementations we need
        static tflite::AllOpsResolver resolver;
        
        // Build an interpreter to run the model
        static tflite::MicroInterpreter static_interpreter(
            model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
        interpreter = &static_interpreter;
        
        // Allocate memory from the tensor_arena for the model's tensors
        interpreter->AllocateTensors();
        
        // Obtain pointers to the model's input and output tensors
        input = interpreter->input(0);
        output = interpreter->output(0);
    }
    
    // Normalize input features
    void normalizeFeatures(float& rssi, float& snr) {
        rssi = (rssi - rssi_mean) / rssi_std;
        snr = (snr - snr_mean) / snr_std;
    }
    
    // Get optimal parameters based on current conditions
    bool getOptimalParameters(float rssi, float snr, float battery_level, 
                            uint8_t payload_size, uint8_t& sf, uint8_t& cr, 
                            long& bw, int8_t& tp) {
        // Normalize input features
        normalizeFeatures(rssi, snr);
        
        // Set input tensor values
        input->data.f[0] = rssi;
        input->data.f[1] = snr;
        input->data.f[2] = battery_level;
        input->data.f[3] = payload_size / 255.0f; // Normalize to 0-1
        
        // Run inference
        TfLiteStatus invoke_status = interpreter->Invoke();
        if (invoke_status != kTfLiteOk) {
            return false;
        }
        
        // Read output values and denormalize
        sf = static_cast<uint8_t>(output->data.f[0] * 5 + 7); // SF 7-12
        cr = static_cast<uint8_t>(output->data.f[1] * 3 + 5); // CR 5-8
        bw = static_cast<long>((output->data.f[2] > 0.66f) ? 500E3 : 
                              (output->data.f[2] > 0.33f) ? 250E3 : 125E3);
        tp = static_cast<int8_t>(output->data.f[3] * 18 + 2); // TP 2-20
        
        return true;
    }
};

#endif // ML_HANDLER_H 