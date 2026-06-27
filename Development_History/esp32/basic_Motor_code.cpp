#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h> // Include the ESP32 Servo Library

WebServer server(80);

// Left motor pins
const int ENA = 25;
const int IN1 = 26;
const int IN2 = 27;

// Right motor pins
const int ENB = 13;
const int IN3 = 14;
const int IN4 = 12;

// PWM settings
const int pwmChannelA = 0;
const int pwmChannelB = 1;
const int pwmFreq = 500;
const int pwmResolution = 8; // 0-255

// --- SERVO CONFIGURATION ---
#define ARM_SERVO_PIN      32  
#define GRIPPER_SERVO_PIN  33  

Servo armServo;
Servo gripperServo;

// Adjust these angles if your mechanical alignment needs fine-tuning
const int ARM_UP_ANGLE         = 180;   // Starting position (Fully CCW)
const int ARM_DOWN_ANGLE       = 90;    // Dropped down position (90 deg CW)
const int GRIPPER_CLOSED_ANGLE = 70;    // Starting position (Closed, Fully CW)
const int GRIPPER_OPEN_ANGLE   = 0;     // Opened position (70 deg CCW from closed)

// --- LIVE TELEMETRY & CONTROL VARIABLES ---
int current_color_id = 3; 
int live_area = 0;
int live_tx = 80;
int live_ty = 60;

int grab_distance_cm = 30; // Controlled by the website slider
bool is_grabbing = false;  // Prevents spamming the servo sequence while moving

// ==========================================
// SERVO MOVEMENT FUNCTIONS
// ==========================================
void Arm_Down() {
  armServo.write(ARM_DOWN_ANGLE);
  Serial.println("Servo Action: Arm DOWN (90° CW)");
}

void Arm_Up() {
  armServo.write(ARM_UP_ANGLE);
  Serial.println("Servo Action: Arm UP (Starting Position)");
}

void Gripper_Open() {
  gripperServo.write(GRIPPER_OPEN_ANGLE);
  Serial.println("Servo Action: Gripper OPEN (70° CCW)");
}

void Gripper_Closed() {
  gripperServo.write(GRIPPER_CLOSED_ANGLE);
  Serial.println("Servo Action: Gripper CLOSED (Starting Position)");
}

// Automatic sequential grab routine
void executeGrabSequence() {
  is_grabbing = true;
  Serial.println("--- TRIGGERED AUTOMATIC GRAB ---");
  
  Gripper_Open();
  delay(600); // Give servo time to physically open
  
  Arm_Down();
  delay(800); // Give arm time to drop down over the ball
  
  Gripper_Closed();
  delay(600); // Clamp down on the ball
  
  Arm_Up();
  delay(800); // Lift the ball back up safely
  
  Serial.println("--- GRAB SEQUENCE COMPLETE ---");
  is_grabbing = false;
}

// ==========================================
// CLEAN, HIGH-SPEED HTML DASHBOARD (RADAR REMOVED)
// ==========================================
const char* html_page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background: #121212; color: white; text-align: center; padding-top: 40px; margin: 0;}
    
    /* Slider Styling */
    .slider-box { background: #1e1e1e; padding: 15px; margin: 30px auto; max-width: 320px; border-radius: 10px; border: 1px solid #333;}
    .slider { width: 100%; margin-top: 10px; cursor: pointer; }
    
    .btn-grid { display: flex; flex-wrap: wrap; justify-content: center; gap: 10px; max-width: 350px; margin: auto;}
    .btn { 
      border: none; padding: 15px; font-size: 16px; font-weight: bold; border-radius: 8px; 
      cursor: pointer; width: 45%; box-shadow: 0 4px 10px rgba(0,0,0,0.5);
    }
    .btn:active { transform: scale(0.95); }
    .btn-red { background: #e74c3c; color: white; }
    .btn-yel { background: #f1c40f; color: black; }
    .btn-cya { background: #00bcd4; color: black; }
    .btn-pur { background: #9b59b6; color: white; }
    .btn-pin { background: #e84393; color: white; width: 93%;}
  </style>
</head>
<body>
  <h2>Robot Vision & Arm Control</h2>

  <div class="slider-box">
    <div>Grab Trigger Distance: <strong id="slider-label">30</strong> cm</div>
    <input type="range" min="15" max="60" value="30" class="slider" id="distSlider" oninput="changeDistance(this.value)">
  </div>

  <div class="btn-grid">
    <button class="btn btn-red" onclick="setTarget(3)">RED</button>
    <button class="btn btn-yel" onclick="setTarget(5)">YELLOW</button>
    <button class="btn btn-cya" onclick="setTarget(2)">CYAN</button>
    <button class="btn btn-pur" onclick="setTarget(1)">PURPLE</button>
    <button class="btn btn-pin" onclick="setTarget(4)">PINK</button>
  </div>

  <script>
    function setTarget(id) { fetch('/setColor?id=' + id); }
    
    function changeDistance(val) {
      document.getElementById('slider-label').innerText = val;
      fetch('/setGrabDist?val=' + val);
    }
  </script>
</body>
</html>
)rawhtml";

// ==========================================
// WEB SERVER ROUTING LOGIC
// ==========================================
void handleRoot() {
  server.send(200, "text/html", html_page);
}

void handleSetColor() {
  if (server.hasArg("id")) {
    current_color_id = server.arg("id").toInt();
    String command = "1," + String(current_color_id);
    Serial2.println(command); 
  }
  server.send(200, "text/plain", "OK");
}

void handleSetGrabDist() {
  if (server.hasArg("val")) {
    grab_distance_cm = server.arg("val").toInt();
    Serial.print("Target Grab Distance updated to: ");
    Serial.print(grab_distance_cm);
    Serial.println(" cm");
  }
  server.send(200, "text/plain", "OK");
}

// ==========================================
// MOTOR CONTROL LOGIC
// ==========================================
void moveRobot(char cmd) {
    switch (cmd) {
        case 'F':   // Forward
            digitalWrite(IN1, HIGH);
            digitalWrite(IN2, LOW);
            digitalWrite(IN3, HIGH);
            digitalWrite(IN4, LOW);
            ledcWrite(pwmChannelA, 120);
            ledcWrite(pwmChannelB, 120);
            break;

        case 'B':   // Backward
            digitalWrite(IN1, LOW);
            digitalWrite(IN2, HIGH);
            digitalWrite(IN3, LOW);
            digitalWrite(IN4, HIGH);
            ledcWrite(pwmChannelA, 120);
            ledcWrite(pwmChannelB, 120);
            break;

        case 'L':   // Turn Left
            digitalWrite(IN1, LOW);
            digitalWrite(IN2, HIGH);
            digitalWrite(IN3, HIGH);
            digitalWrite(IN4, LOW);
            ledcWrite(pwmChannelA, 120);
            ledcWrite(pwmChannelB, 120);
            break;

        case 'R':   // Turn Right
            digitalWrite(IN1, HIGH);
            digitalWrite(IN2, LOW);
            digitalWrite(IN3, LOW);
            digitalWrite(IN4, HIGH);
            ledcWrite(pwmChannelA, 120);
            ledcWrite(pwmChannelB, 120);
            break;

        case 'S':
        default:    // Stop
            ledcWrite(pwmChannelA, 0);
            ledcWrite(pwmChannelB, 0);
            break;
    }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // Correct ESP32 Hardware PWM Initialization
  ledcSetup(pwmChannelA, pwmFreq, pwmResolution);
  ledcAttachPin(ENA, pwmChannelA);

  ledcSetup(pwmChannelB, pwmFreq, pwmResolution);
  ledcAttachPin(ENB, pwmChannelB);

  // Attach Servos and force them to default home states
  armServo.attach(ARM_SERVO_PIN);
  gripperServo.attach(GRIPPER_SERVO_PIN);
  Arm_Up();
  Gripper_Closed();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Robot_Control", "12345678");
  
  server.on("/", handleRoot);
  server.on("/setColor", handleSetColor);
  server.on("/setGrabDist", handleSetGrabDist); 
  server.begin();
  
  Serial.println("\n--- BOOT COMPLETE ---");
  Serial.println("Dashboard: http://192.168.4.1");
}

void loop() {
  server.handleClient();

  // Read incoming Serial data from Camera
  if (Serial2.available() && !is_grabbing) { 
    String incomingData = Serial2.readStringUntil('\n');
    incomingData.trim(); 
    
    // Parse commas carefully to get action command, area, and distance
    int c1 = incomingData.indexOf(',');
    int c2 = incomingData.indexOf(',', c1 + 1);
    int c3 = incomingData.indexOf(',', c2 + 1);
    int c4 = incomingData.indexOf(',', c3 + 1);

    if (c1 > 0) {
      String cmd = incomingData.substring(0, c1);
      
      // Parse area
      if (c2 > c1) {
        live_area = incomingData.substring(c1 + 1, c2).toInt();
      } else {
        live_area = incomingData.substring(c1 + 1).toInt();
      }

      // Parse distance for the automatic grab trigger
      int dist_cm = 0;
      if (c2 > c1 && c3 > c2) {
        dist_cm = incomingData.substring(c2 + 1, c3).toInt();
      } else if (c2 > c1) {
        dist_cm = incomingData.substring(c2 + 1).toInt();
      }

      // Parse optional coordinates for the radar view mapping
      if (c3 > c2 && c4 > c3) {
        live_tx = incomingData.substring(c3 + 1, c4).toInt();
        live_ty = incomingData.substring(c4 + 1).toInt();
      }

      // AUTOMATIC TRIGGER EVALUATION
      if (dist_cm > 0 && dist_cm <= grab_distance_cm) {
          moveRobot('S'); // Stop wheels instantly
          executeGrabSequence();
      } else {
          moveRobot(cmd.charAt(0)); // Run standard driving wheels
      }
    }
  }
}