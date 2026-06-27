#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

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
// Servos
#define ARM_PIN     32
#define GRIPPER_PIN 33

// PWM Configuration (Core 2.x compatible)
#define PWM_CH_A    4
#define PWM_CH_B    5
#define PWM_FREQ    15000
#define PWM_RES     8
#define DRIVE_SPEED 220 

// Servo angles
#define ARM_UP          180
#define ARM_DOWN        70
#define GRIPPER_CLOSED  70
#define GRIPPER_OPEN    30

WebServer server(80);

Servo armServo;
Servo gripperServo;
bool is_grabbing = false;

// ═══════════════════════════════════════════════════════════════
// MOTOR LAYER (Flipped Configurations)
// ═══════════════════════════════════════════════════════════════
void motor_stop() {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);
    ledcWrite(PWM_CH_A, 0);
    ledcWrite(PWM_CH_B, 0);
}

// ═══════════════════════════════════════════════════════════════
// SERVO ACTIONS
// ═══════════════════════════════════════════════════════════════
void arm_up()        { armServo.write(ARM_UP);          Serial.println("[SERVO] Arm UP"); }
void arm_down()      { armServo.write(ARM_DOWN);        Serial.println("[SERVO] Arm DOWN"); }
void gripper_open()  { gripperServo.write(GRIPPER_OPEN);    Serial.println("[SERVO] Gripper OPEN"); }
void gripper_close() { gripperServo.write(GRIPPER_CLOSED);  Serial.println("[SERVO] Gripper CLOSED"); }

void execute_grab_sequence() {
    is_grabbing = true;
    Serial.println("[SEQUENCE] Starting automated grab...");
    gripper_close(); delay(600);
    arm_down();      delay(800);
    gripper_open();  delay(600);
    arm_up();        delay(800);
    Serial.println("[SEQUENCE] Grab sequence complete.");
    is_grabbing = false;
}

// ═══════════════════════════════════════════════════════════════
// CLEAN MANUAL REMOTE DASHBOARD UI
// ═══════════════════════════════════════════════════════════════
const char* html_page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Manual Robot Control Rig</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family:Arial,sans-serif; background:#121212; color:white; text-align:center; padding:15px; margin:0; }
    h2 { color:#00ff66; margin-bottom: 5px; }
    p { color: #888; font-size: 13px; margin: 0 0 20px 0; }
    
    .card { background:#1e1e1e; border:1px solid #333; border-radius:12px; padding:15px; margin:12px auto; max-width:340px; }
    .card-title { font-size:11px; color:#888; font-weight:bold; letter-spacing:1px; margin-bottom:12px; }

    /* Movement Grid Layout */
    .dpad { display:grid; grid-template-columns:repeat(3,75px); grid-template-rows:repeat(3,65px); gap:8px; justify-content:center; }
    .dp { background:#2b2b2b; border:2px solid #555; color:white; font-size:26px; font-weight:bold; border-radius:10px; cursor:pointer;
          display:flex; align-items:center; justify-content:center; user-select:none; -webkit-user-select:none; touch-action:none; }
    .dp:active { background:#00ff66; border-color:#00ff66; color:black; }
    .dp-blank { visibility:hidden; }
    .dp-stop { font-size:16px; color:#aaa; background:#dc3545; border-color:#dc3545; }
    .dp-stop:active { background:#ff0000; color:white; }

    /* Servo Rows Layout */
    .g2 { display:flex; gap:10px; margin-bottom:10px; }
    .gbtn { flex:1; padding:14px 5px; font-size:14px; font-weight:bold; border:none; border-radius:8px; cursor:pointer; color:white; }
    .gbtn:active { transform:scale(0.96); }
    .g-au { background:#2980b9; }
    .g-ad { background:#1a5276; }
    .g-op { background:#27ae60; }
    .g-cl { background:#922b21; }
    .g-seq { background:#8e44ad; width:100%; padding:14px; font-size:14px; font-weight:bold; border:none; border-radius:8px; cursor:pointer; color:white; }
    .g-seq:active { transform:scale(0.96); }
  </style>
</head>
<body>

  <h2>🤖 Manual Control Rig</h2>
  <p>Flipped Motors + Servo Integration Active</p>
  
  <div class="card">
    <div class="card-title">CHASSIS DRIVE</div>
    <div class="dpad">
      <div class="dp-blank"></div>
      <button class="dp" onpointerdown="sendMove('F')" onpointerup="sendMove('S')" onpointerleave="sendMove('S')">▲</button>
      <div class="dp-blank"></div>
      <button class="dp" onpointerdown="sendMove('L')" onpointerup="sendMove('S')" onpointerleave="sendMove('S')">◀</button>
      <button class="dp dp-stop" onpointerdown="sendMove('S')">■</button>
      <button class="dp" onpointerdown="sendMove('R')" onpointerup="sendMove('S')" onpointerleave="sendMove('S')">▶</button>
      <div class="dp-blank"></div>
      <button class="dp" onpointerdown="sendMove('B')" onpointerup="sendMove('S')" onpointerleave="sendMove('S')">▼</button>
      <div class="dp-blank"></div>
    </div>
  </div>

  <div class="card">
    <div class="card-title">ARM & GRIPPER MANIPULATORS</div>
    <div class="g2">
      <button class="gbtn g-au" onclick="sendServo('/arm?pos=up')">⬆ Arm Up</button>
      <button class="gbtn g-ad" onclick="sendServo('/arm?pos=down')">⬇ Arm Down</button>
    </div>
    <div class="g2">
      <button class="gbtn g-op" onclick="sendServo('/gripper?pos=open')">✋ Open Gripper</button>
      <button class="gbtn g-cl" onclick="sendServo('/gripper?pos=close')">✊ Close Gripper</button>
    </div>
    <button class="g-seq" onclick="sendServo('/grab_sequence')">⚙ Run Automatic Grab Sequence</button>
  </div>

<script>
  function sendMove(dir) {
    fetch('/test_move?dir=' + dir).catch(e => console.error(e));
  }
  function sendServo(endpoint) {
    fetch(endpoint).catch(e => console.error(e));
  }
</script>
</body>
</html>
)rawhtml";

// ═══════════════════════════════════════════════════════════════
// WEB ROUTE CONTROLLERS
// ═══════════════════════════════════════════════════════════════
void handleRoot() { server.send(200, "text/html", html_page); }

void handleTestMove() {
    if (server.hasArg("dir")) {
        char d = server.arg("dir").charAt(0);
        switch (d) {
            case 'F': // Physical Backward Execution
                digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
                digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
                ledcWrite(PWM_CH_A, DRIVE_SPEED); ledcWrite(PWM_CH_B, DRIVE_SPEED);
                break;
            case 'B': // Physical Forward Execution
                digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
                digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
                ledcWrite(PWM_CH_A, DRIVE_SPEED); ledcWrite(PWM_CH_B, DRIVE_SPEED);
                break;
            case 'L': // Physical Right Turn Spin Execution
                digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
                digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
                ledcWrite(PWM_CH_A, DRIVE_SPEED); ledcWrite(PWM_CH_B, DRIVE_SPEED);
                break;
            case 'R': // Physical Left Turn Spin Execution
                digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
                digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
                ledcWrite(PWM_CH_A, DRIVE_SPEED); ledcWrite(PWM_CH_B, DRIVE_SPEED);
                break;
            default:
                motor_stop();
                break;
        }
        server.send(200, "text/plain", "ACK");
    } else {
        server.send(400, "text/plain", "Bad Request");
    }
}

void handleArm() {
    if (server.hasArg("pos")) {
        String pos = server.arg("pos");
        if (pos == "up") arm_up(); else arm_down();
    }
    server.send(200, "text/plain", "ACK");
}

void handleGripper() {
    if (server.hasArg("pos")) {
        String pos = server.arg("pos");
        if (pos == "open") gripper_open(); else gripper_close();
    }
    server.send(200, "text/plain", "ACK");
}

void handleGrabSequence() {
    execute_grab_sequence();
    server.send(200, "text/plain", "ACK");
}

// ═══════════════════════════════════════════════════════════════
// HARDWARE SETUPS
// ═══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== MANUAL CONTROL SKETCH BOOTING ===");

    // Motor Pins Setup
    pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
    ledcSetup(PWM_CH_A, PWM_FREQ, PWM_RES);
    ledcAttachPin(ENA, PWM_CH_A);
    ledcSetup(PWM_CH_B, PWM_FREQ, PWM_RES);
    ledcAttachPin(ENB, PWM_CH_B);
    motor_stop();

    // Servo Configuration Layer
    armServo.attach(ARM_PIN);
    gripperServo.attach(GRIPPER_PIN);
    arm_up();
    gripper_close();

    // Soft AP Initialization
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Motor_Test_Net", "12345678");
    Serial.println("[Wi-Fi] Network 'Motor_Test_Net' Broad-casted.");

    // Web Server Paths Routing
    server.on("/",              HTTP_GET, handleRoot);
    server.on("/test_move",     HTTP_GET, handleTestMove);
    server.on("/arm",           HTTP_GET, handleArm);
    server.on("/gripper",       HTTP_GET, handleGripper);
    server.on("/grab_sequence", HTTP_GET, handleGrabSequence);

    server.begin();
    Serial.println("[SERVER] Manual Hub initialized and listening.");
}

void loop() {
    server.handleClient();
}