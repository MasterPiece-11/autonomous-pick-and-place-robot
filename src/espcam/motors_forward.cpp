#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════
// PIN DEFINITIONS
// ═══════════════════════════════════════════════════════════════
// Left motor
#define ENA 25
#define IN1 26
#define IN2 27

// Right motor
#define ENB 13
#define IN3 14
#define IN4 12

// PWM Configuration (PlatformIO Core 2.x compatible)
#define PWM_CH_A    4
#define PWM_CH_B    5
#define PWM_FREQ    15000
#define PWM_RES     8

// Test Speed (Change between 0 and 255)
#define TEST_SPEED  200 

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== MOTOR DIRECT HARDWARE TEST ===");

    // 1. Initialize H-Bridge Direction Pins
    pinMode(IN1, OUTPUT); 
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT); 
    pinMode(IN4, OUTPUT);

    // 2. Setup ESP32 PWM Generation
    ledcSetup(PWM_CH_A, PWM_FREQ, PWM_RES);
    ledcAttachPin(ENA, PWM_CH_A);

    ledcSetup(PWM_CH_B, PWM_FREQ, PWM_RES);
    ledcAttachPin(ENB, PWM_CH_B);

    Serial.println("[TEST] Pins configured.");
    delay(1000);

    // 3. Set Direction to Forward
    // Left Motor Forward
    digitalWrite(IN1, HIGH); 
    digitalWrite(IN2, LOW);
    
    // Right Motor Forward
    digitalWrite(IN3, HIGH); 
    digitalWrite(IN4, LOW);

    // 4. Inject PWM Power
    Serial.print("[TEST] Sending PWM Power: ");
    Serial.println(TEST_SPEED);
    
    ledcWrite(PWM_CH_A, TEST_SPEED);
    ledcWrite(PWM_CH_B, TEST_SPEED);
}

void loop() {
    // Keep running continuously. 
    // If you need to stop it, press the RESET button on the ESP32 or pull the power plug.
    delay(1000);
}