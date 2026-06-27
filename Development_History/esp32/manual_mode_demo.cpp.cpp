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

// PWM Configuration (Using channels 4 & 5 to stay clear of servos)
#define PWM_CH_A    4
#define PWM_CH_B    5
#define PWM_FREQ    15000
#define PWM_RES     8

// Servo angles
#define ARM_UP          180
#define ARM_DOWN        70
#define GRIPPER_CLOSED  70
#define GRIPPER_OPEN    25

// ═══════════════════════════════════════════════════════════════
// GLOBALS
// ═══════════════════════════════════════════════════════════════
WebServer server(80);

Servo armServo;
Servo gripperServo;

bool system_armed   = false;
bool has_target     = false;
bool is_grabbing    = false;
int  robot_mode     = 0;   // 0=Ball, 1=AprilTag, 2=Manual, 3=OTA
int  live_area      = 0;
int  current_color_id   = 3;
int  grab_distance_cm   = 10;
int  live_tx = 80, live_ty = 60;

// Manual drive watchdog
static unsigned long last_move_ping  = 0;
static bool          manual_override = false;

// Log buffer for terminal
String log_buffer = "";

// ═══════════════════════════════════════════════════════════════
// LOGGING
// ═══════════════════════════════════════════════════════════════
void add_log(String msg) {
    Serial.println(msg);
    log_buffer += "[" + String(millis()/1000) + "s] " + msg + "\n";
    if (log_buffer.length() > 3000)
        log_buffer = log_buffer.substring(log_buffer.length() - 1500);
}

// ═══════════════════════════════════════════════════════════════
// MOTOR FUNCTIONS
// ═══════════════════════════════════════════════════════════════
void motor_stop() {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);
    ledcWrite(PWM_CH_A, 0);
    ledcWrite(PWM_CH_B, 0);
    Serial.println("[MOTOR] STOP");
}

void motor_front() {
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
    digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
    ledcWrite(PWM_CH_A, 200);
    ledcWrite(PWM_CH_B, 200);
    Serial.println("[MOTOR] FORWARD");
}

void motor_back() {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
    digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
    ledcWrite(PWM_CH_A, 200);
    ledcWrite(PWM_CH_B, 200);
    Serial.println("[MOTOR] BACK");
}

void motor_left() {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
    digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
    ledcWrite(PWM_CH_A, 200);
    ledcWrite(PWM_CH_B, 200);
    Serial.println("[MOTOR] LEFT");
}

void motor_right() {
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
    ledcWrite(PWM_CH_A, 200);
    ledcWrite(PWM_CH_B, 200);
    Serial.println("[MOTOR] RIGHT");
}

void motors_init() {
    pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
    
    ledcSetup(PWM_CH_A, PWM_FREQ, PWM_RES);
    ledcAttachPin(ENA, PWM_CH_A);
    
    ledcSetup(PWM_CH_B, PWM_FREQ, PWM_RES);
    ledcAttachPin(ENB, PWM_CH_B);
    
    motor_stop();
    Serial.println("[MOTOR] Init done");
}

// ═══════════════════════════════════════════════════════════════
// SERVO FUNCTIONS
// ═══════════════════════════════════════════════════════════════
void arm_up() {
    armServo.write(ARM_UP);
    add_log("Servo: Arm UP");
}

void arm_down() {
    armServo.write(ARM_DOWN);
    add_log("Servo: Arm DOWN");
}

void gripper_open() {
    gripperServo.write(GRIPPER_OPEN);
    add_log("Servo: Gripper OPEN");
}

void gripper_close() {
    gripperServo.write(GRIPPER_CLOSED);
    add_log("Servo: Gripper CLOSED");
}

void execute_grab_sequence() {
    is_grabbing = true;
    add_log("GRAB: Starting sequence");
    gripper_close(); delay(600);
    arm_down();      delay(800);
    gripper_open();  delay(600);
    arm_up();        delay(800);
    add_log("GRAB: Complete");
    is_grabbing = false;
}

void servos_init() {
    armServo.attach(ARM_PIN);
    gripperServo.attach(GRIPPER_PIN);
    arm_up();
    gripper_close();
    Serial.println("[SERVO] Init done");
}

// ═══════════════════════════════════════════════════════════════
// HTML DASHBOARD (With streamlined, race-free network scripts)
// ═══════════════════════════════════════════════════════════════
const char* html_page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Robot Control</title>
  <style>
    * { box-sizing:border-box; }
    body { font-family:Arial,sans-serif; background:#121212; color:white;
           text-align:center; padding:12px; margin:0; }
    h2   { margin:0 0 12px; font-size:18px; color:#eee; }

    .card { background:#1e1e1e; border:1px solid #333; border-radius:10px;
            padding:14px; margin:10px auto; max-width:340px; }
    .card-title { font-size:11px; color:#888; font-weight:bold;
                  letter-spacing:1px; margin-bottom:10px; }

    .row2 { display:flex; gap:10px; }
    .btn-start { flex:1; background:#28a745; color:white; font-weight:bold;
                 font-size:16px; border:none; padding:14px; border-radius:6px; cursor:pointer; }
    .btn-stop  { flex:1; background:#dc3545; color:white; font-weight:bold;
                 font-size:16px; border:none; padding:14px; border-radius:6px; cursor:pointer; }

    .mode-row { display:flex; flex-wrap:wrap; gap:8px; justify-content:center; }
    .mbtn { flex:1; min-width:80px; border:2px solid #555; background:#2b2b2b;
            color:white; padding:10px 6px; font-weight:bold; border-radius:6px;
            cursor:pointer; font-size:13px; }
    .mbtn.on { background:#00ff66; color:black; border-color:#00ff66; }
    .mbtn-ota { flex:100%; background:#8e44ad; border-color:#9b59b6; margin-top:4px; }
    .mbtn-ota.on { background:#f39c12; color:black; border-color:#f1c40f; }

    .dpad { display:grid; grid-template-columns:repeat(3,72px);
            grid-template-rows:repeat(3,60px); gap:6px; justify-content:center; }
    .dp { background:#2b2b2b; border:2px solid #555; color:white; font-size:26px;
          font-weight:bold; border-radius:8px; cursor:pointer;
          display:flex; align-items:center; justify-content:center;
          user-select:none; -webkit-user-select:none; touch-action:none; }
    .dp:active,.dp.held { background:#00ff66; border-color:#00ff66; color:black; }
    .dp-blank { visibility:hidden; }
    .dp-stop  { font-size:16px; color:#666; background:#1a1a1a; border-color:#333; }

    .g2 { display:flex; gap:8px; margin-bottom:8px; }
    .gbtn { flex:1; padding:13px 6px; font-size:14px; font-weight:bold;
            border:none; border-radius:6px; cursor:pointer; }
    .gbtn:active { transform:scale(0.95); }
    .g-au  { background:#2980b9; color:white; }
    .g-ad  { background:#1a5276; color:white; }
    .g-op  { background:#27ae60; color:white; }
    .g-cl  { background:#922b21; color:white; }
    .g-seq { background:#8e44ad; color:white; width:100%; padding:13px;
             font-size:14px; font-weight:bold; border:none; border-radius:6px; cursor:pointer; }
    .g-seq:active { transform:scale(0.95); }

    .term { background:#000; border:2px solid #333; border-radius:8px;
            max-width:340px; margin:10px auto; }
    .term-hdr  { background:#222; color:#aaa; padding:6px 10px; font-size:12px;
                 font-weight:bold; text-align:left; border-bottom:1px solid #333; }
    .term-body { height:120px; overflow-y:auto; color:#00ff66; font-family:monospace;
                 font-size:12px; text-align:left; padding:8px; white-space:pre-wrap; }

    .stat-val { color:#00ff66; font-weight:bold; font-family:monospace; }
  </style>
</head>
<body>
<h2>🤖 Robot Control</h2>

<div class="card">
  <div class="row2">
    <button class="btn-start" onclick="cmd('/system_start')">▶ START</button>
    <button class="btn-stop"  onclick="cmd('/system_stop')">🛑 STOP</button>
  </div>
</div>

<div class="card">
  <div class="card-title">OPERATION MODE</div>
  <div class="mode-row">
    <button id="m0" class="mbtn on"  onclick="setMode(0)">BALL HUNT</button>
    <button id="m1" class="mbtn"     onclick="setMode(1)">APRIL TAG</button>
    <button id="m2" class="mbtn"     onclick="setMode(2)">MANUAL</button>
    <button id="m3" class="mbtn mbtn-ota" onclick="setMode(3)">⚡ OTA FLASH</button>
  </div>
</div>

<div class="card">
  <div class="card-title">MOVEMENT</div>
  <div class="dpad">
    <div class="dp-blank"></div>
    <div class="dp" onpointerdown="driveStart('F',this)" onpointerup="driveStop()" onpointerleave="driveStop()">▲</div>
    <div class="dp-blank"></div>
    <div class="dp" onpointerdown="driveStart('L',this)" onpointerup="driveStop()" onpointerleave="driveStop()">◀</div>
    <div class="dp dp-stop" onpointerdown="driveStart('S',this)" onpointerup="driveStop()" onpointerleave="driveStop()">■</div>
    <div class="dp" onpointerdown="driveStart('R',this)" onpointerup="driveStop()" onpointerleave="driveStop()">▶</div>
    <div class="dp-blank"></div>
    <div class="dp" onpointerdown="driveStart('B',this)" onpointerup="driveStop()" onpointerleave="driveStop()">▼</div>
    <div class="dp-blank"></div>
  </div>
</div>

<div class="card">
  <div class="card-title">ARM & GRIPPER</div>
  <div class="g2">
    <button class="gbtn g-au" onclick="cmd('/arm?pos=up')">⬆ Arm Up</button>
    <button class="gbtn g-ad" onclick="cmd('/arm?pos=down')">⬇ Arm Down</button>
  </div>
  <div class="g2">
    <button class="gbtn g-op" onclick="cmd('/gripper?pos=open')">✋ Open</button>
    <button class="gbtn g-cl" onclick="cmd('/gripper?pos=close')">✊ Close</button>
  </div>
  <button class="g-seq" onclick="cmd('/grab_sequence')">⚙ Full Grab Sequence</button>
</div>

<div class="card">
  <table style="width:100%;font-size:13px;border-collapse:collapse;">
    <tr style="border-bottom:1px solid #333;">
      <th style="padding:5px;color:#aaa;text-align:left;">Metric</th>
      <th style="padding:5px;color:#aaa;text-align:right;">Value</th>
    </tr>
    <tr>
      <td style="padding:7px 5px;">Ball Area</td>
      <td id="stat-area" class="stat-val" style="padding:7px 5px;text-align:right;">0 px</td>
    </tr>
  </table>
</div>

<div class="term">
  <div class="term-hdr">📟 ROBOT LOG</div>
  <div id="terminal" class="term-body">Connecting to 192.168.4.1...</div>
</div>

<script>
  function cmd(url) {
    console.log('CMD:', url);
    fetch(url)
      .then(r => r.text())
      .then(t => console.log('RESP:', t))
      .catch(e => console.error('ERR:', e));
  }

  function setMode(n) {
    cmd('/setMode?m=' + n);
    [0,1,2,3].forEach(i => document.getElementById('m'+i).classList.remove('on'));
    document.getElementById('m'+n).classList.add('on');
  }

  let driveTimer = null;
  let activeDP   = null;

  // Modified to clear local memory without flooding sequential network commands
  function driveStart(dir, el) {
    if (driveTimer) { clearInterval(driveTimer); driveTimer = null; }
    if (activeDP) { activeDP.classList.remove('held'); }
    
    activeDP = el;
    el.classList.add('held');
    
    cmd('/move?dir=' + dir);
    // Ping periodically to prevent the watchdog from shutting down the channels
    driveTimer = setInterval(() => cmd('/move?dir=' + dir), 200);
  }

  function driveStop() {
    if (!driveTimer) return; 
    clearInterval(driveTimer); 
    driveTimer = null;
    if (activeDP) { activeDP.classList.remove('held'); activeDP = null; }
    cmd('/move?dir=S');
  }

  setInterval(() => {
    fetch('/getLogs')
      .then(r => r.text())
      .then(t => {
        if (!t.length) return;
        let el = document.getElementById('terminal');
        el.innerText += t;
        el.scrollTop  = el.scrollHeight;
      });
  }, 1000);

  setInterval(() => {
    fetch('/getStats')
      .then(r => r.text())
      .then(v => { document.getElementById('stat-area').innerText = v + ' px'; });
  }, 300);
</script>
</body>
</html>
)rawhtml";

// ═══════════════════════════════════════════════════════════════
// ROUTE HANDLERS
// ═══════════════════════════════════════════════════════════════
void handleRoot()    { server.send(200, "text/html", html_page); }
void handleGetLogs() { server.send(200, "text/plain", log_buffer); log_buffer = ""; }
void handleGetStats(){ server.send(200, "text/plain", String(live_area)); }

void handleSystemStart() {
    system_armed    = true;
    manual_override = false;
    add_log(">>> SYSTEM ARMED");
    server.send(200, "text/plain", "ARMED");
}

void handleSystemStop() {
    system_armed    = false;
    manual_override = false;
    motor_stop();
    while (Serial2.available()) Serial2.read();
    add_log(">>> EMERGENCY HALT");
    server.send(200, "text/plain", "HALTED");
}

void handleSetMode() {
    if (server.hasArg("m")) {
        robot_mode = server.arg("m").toInt();
        if (robot_mode != 2) { manual_override = false; motor_stop(); }
        String names[] = {"Ball Hunt","AprilTag","Manual","OTA"};
        add_log("Mode → " + names[robot_mode]);
        Serial2.println(String(robot_mode) + ",0");
    }
    server.send(200, "text/plain", "OK");
}

void handleMove() {
    Serial.println("[MOVE] hit, armed=" + String(system_armed));
    if (!system_armed) { server.send(200, "text/plain", "DISARMED"); return; }

    if (server.hasArg("dir")) {
        char d = server.arg("dir").charAt(0);
        Serial.println("[MOVE] dir=" + String(d));
        last_move_ping  = millis();
        manual_override = (d != 'S');
        switch (d) {
            case 'F': motor_front(); break;
            case 'B': motor_back();  break;
            case 'L': motor_left();  break;
            case 'R': motor_right(); break;
            default:  motor_stop();  manual_override = false; break;
        }
    }
    server.send(200, "text/plain", "OK");
}

void handleArm() {
    Serial.println("[ARM] hit");
    if (server.hasArg("pos")) {
        String pos = server.arg("pos");
        Serial.println("[ARM] pos=" + pos);
        if (pos == "up") arm_up(); else arm_down();
    }
    server.send(200, "text/plain", "OK");
}

void handleGripper() {
    Serial.println("[GRIPPER] hit");
    if (server.hasArg("pos")) {
        String pos = server.arg("pos");
        Serial.println("[GRIPPER] pos=" + pos);
        if (pos == "open") gripper_open(); else gripper_close();
    }
    server.send(200, "text/plain", "OK");
}

void handleGrabSequence() {
    Serial.println("[GRAB SEQ] hit");
    execute_grab_sequence();
    server.send(200, "text/plain", "OK");
}

void manual_watchdog() {
    // Watchdog cuts power if 1000ms passes without a web ping
    if (manual_override && (millis() - last_move_ping > 1000)) {
        motor_stop();
        manual_override = false;
        Serial.println("[WATCHDOG] motor cut");
    }
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== ROBOT BOOTING ===");

    Serial2.begin(115200);

    motors_init();
    servos_init();

    WiFi.mode(WIFI_AP);
    WiFi.softAP("Robot_Control", "12345678");
    Serial.println("[WiFi] AP started: 192.168.4.1");

    server.on("/",              HTTP_GET, handleRoot);
    server.on("/getLogs",       HTTP_GET, handleGetLogs);
    server.on("/getStats",      HTTP_GET, handleGetStats);
    server.on("/system_start",  HTTP_GET, handleSystemStart);
    server.on("/system_stop",   HTTP_GET, handleSystemStop);
    server.on("/setMode",       HTTP_GET, handleSetMode);
    server.on("/move",          HTTP_GET, handleMove);
    server.on("/arm",           HTTP_GET, handleArm);
    server.on("/gripper",       HTTP_GET, handleGripper);
    server.on("/grab_sequence", HTTP_GET, handleGrabSequence);

    server.begin();
    Serial.println("[Server] Started on port 80");
    add_log("Ready at 192.168.4.1");
}

// ═══════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
    server.handleClient();
    manual_watchdog();

    if (!system_armed) {
        motor_stop();
        while (Serial2.available()) Serial2.read();
        return;
    }

    if (robot_mode == 2 || robot_mode == 3 || manual_override) {
        while (Serial2.available()) Serial2.read();
        return;
    }

    if (has_target) {
        motor_stop();
        while (Serial2.available()) Serial2.read();
        return;
    }

    static String buf         = "";
    static bool   mem_ok      = false;
    static int    last_mode   = -1;
    static char   last_dir    = ' ';

    if (!mem_ok) { buf.reserve(128); mem_ok = true; }

    if (robot_mode != last_mode) {
        last_mode = robot_mode;
        while (Serial2.available()) Serial2.read();
        buf = "";
    }

    while (Serial2.available() && !is_grabbing) {
        char c = (char)Serial2.read();
        if (c == '\r') continue;
        if (c != '\n') { buf += c; continue; }
        buf.trim();

        if (buf.startsWith("IP:")) {
            add_log("CAM IP: " + buf.substring(3));
            buf = ""; continue;
        }

        if (buf.length() >= 5) {
            char fc = buf.charAt(0);

            if (robot_mode == 0 &&
                (fc=='F'||fc=='B'||fc=='L'||fc=='R'||fc=='S')) {

                int c1 = buf.indexOf(',');
                int c2 = buf.indexOf(',', c1+1);
                int c3 = buf.indexOf(',', c2+1);
                int c4 = buf.indexOf(',', c3+1);

                if (c1 > 0) {
                    char dir = fc;
                    live_area = (c2>c1) ? buf.substring(c1+1,c2).toInt()
                                        : buf.substring(c1+1).toInt();
                    int dist = 0;
                    if (c2>c1 && c3>c2) dist = buf.substring(c2+1,c3).toInt();
                    else if (c2>c1)      dist = buf.substring(c2+1).toInt();
                    if (c3>c2 && c4>c3) {
                        live_tx = buf.substring(c3+1,c4).toInt();
                        live_ty = buf.substring(c4+1).toInt();
                    }

                    if (dist > 0 && dist <= grab_distance_cm && dir == 'F') {
                        motor_stop();
                        add_log("Grab at " + String(dist) + "cm");
                        execute_grab_sequence();
                        has_target = true;
                        while (Serial2.available()) Serial2.read();
                    } else {
                        if      (dir=='F') motor_front();
                        else if (dir=='B') motor_back();
                        else if (dir=='L') motor_left();
                        else if (dir=='R') motor_right();
                        else               motor_stop();

                        if (dir != last_dir) {
                            if      (dir=='F') add_log("Moving forward");
                            else if (dir=='B') add_log("Reversing");
                            else if (dir=='L') add_log("Turning left");
                            else if (dir=='R') add_log("Turning right");
                            else               add_log("Searching...");
                            last_dir = dir;
                        }
                    }
                }
            }

            else if (robot_mode == 1 && buf.startsWith("TAG")) {
                add_log("AprilTag: " + buf);
                motor_stop();
            }
        }
        buf = "";
    }
}