#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "camera_hal.h"   // Include our hardware config module
#include "vision_color.h" // Include our color engine module

// --- SYSTEM STATE TRACKING VARIABLES ---
int current_phase = 0; // 0 = Ball Hunt (Color), 1 = AprilTag, 3 = OTA Mode
String serial_buffer = "";
bool in_ota_mode = false;

// We will build this vision loop in our next file step
// void vision_apriltag_loop();

// ============================================================
// MODAL OPERATION 3: OVER-THE-AIR (OTA) WIRELESS RECEIVER
// ============================================================
void start_OTA_mode() {
    in_ota_mode = true;
    
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH); 
    
    WiFi.mode(WIFI_STA);
    WiFi.begin("Robot_Control", "12345678");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        digitalWrite(4, !digitalRead(4)); 
    }
    
    digitalWrite(4, HIGH);

    // ============================================================
    // NEW: Send assigned IP address back to the Main Motor Board
    // ============================================================
    Serial.println("IP:" + WiFi.localIP().toString());

    ArduinoOTA.setHostname("ESPCAM-Vision-Core");
    ArduinoOTA.begin();

    // THE SAFE TRAP LOOP
    while (true) {
        ArduinoOTA.handle();
        
        if (Serial.available()) {
            String escape_cmd = Serial.readStringUntil('\n');
            if (escape_cmd.indexOf("0,") >= 0 || escape_cmd.indexOf("1,") >= 0) {
                digitalWrite(4, LOW); 
                ESP.restart();        
            }
        }
        delay(10); 
    }
}

// ============================================================
// SUBSYSTEM INSTRUCTION PARSER
// ============================================================
void processSerialCommand(String cmd_str) {
    int comma_idx = cmd_str.indexOf(',');
    if (comma_idx > 0) {
        int received_mode = cmd_str.substring(0, comma_idx).toInt();
        int structural_target = cmd_str.substring(comma_idx + 1).toInt();
        
        if (received_mode == 3 && current_phase != 3) {
            current_phase = 3;
            start_OTA_mode(); // Instantly pivot out of normal running states
        } else {
            current_phase = received_mode;
            if (current_phase == 0) {
                applyColorPreset(structural_target); // Update color matching vectors dynamically
            }
        }
    }
}

// ============================================================
// SETUP SUBROUTINE REGISTER
// ============================================================
void setup() {
    Serial.begin(115200); // Main hardware serial interface link to tracking module
    
    // Attempt low-level hardware configuration setup
    if (!init_camera()) {
        // Critical hardware error loop: blink indicator if camera is loose/broken
        pinMode(4, OUTPUT);
        while(true) { 
            digitalWrite(4, HIGH); delay(100);
            digitalWrite(4, LOW);  delay(100); 
        } 
    }
}

// ============================================================
// CONTINUOUS SCHEDULER EXECUTION CORE
// ============================================================
void loop() {
    // 1. Evaluate any outstanding state messages sent from the motor controller board
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            processSerialCommand(serial_buffer);
            serial_buffer = "";
        } else if (c != '\r') {
            serial_buffer += c;
        }
    }

    // 2. Execution Routing Hand-offs (The Traffic Cop Engine)
    if (current_phase == 0) {
        vision_color_loop(); // Run color threshold tracking execution sweeps
    } 
    else if (current_phase == 1) {
        // vision_apriltag_loop(); // Placeholder for our specialized single-tag logic module
        delay(50); // Yield CPU performance slice cycles briefly
    }
}