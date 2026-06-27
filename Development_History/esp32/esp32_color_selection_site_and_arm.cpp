#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h> // Include the ESP32 Servo Library

WebServer server(80);

// --- SERVO CONFIGURATION ---
#define ARM_SERVO_PIN      32  // Change to your actual physical GPIO pin
#define GRIPPER_SERVO_PIN  33  // Change to your actual physical GPIO pin

Servo armServo;
Servo gripperServo;

// Adjust these angles if your mechanical alignment needs fine-tuning
const int ARM_UP_ANGLE         = 180;   // Starting position (Fully CCW)
const int ARM_DOWN_ANGLE       = 70;  // Dropped down position (90 deg CW)
const int GRIPPER_CLOSED_ANGLE = 70;  // Starting position (Closed, Fully CW)
const int GRIPPER_OPEN_ANGLE   = 30;   // Opened position (70 deg CCW from closed)

// --- LIVE TELEMETRY & CONTROL VARIABLES ---
int current_color_id = 3; 
int grab_distance_cm = 30; 
bool is_grabbing = false;

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
  
  // 1. Immediately tell the wheels to stop so we don't run over the ball
  // moveRobot('S'); 
  Serial.println("--- TRIGGERED AUTOMATIC GRAB ---");
  
  Gripper_Closed();
  delay(600); // Give servo time to physically open
  
  Arm_Down();
  delay(800); // Give arm time to drop down over the ball
  
  // Gripper_Open();
  // delay(600); // Clamp down on the ball
  
  // Arm_Up();
  // delay(800); // Lift the ball back up safely
  
  Serial.println("--- GRAB SEQUENCE COMPLETE ---");
  is_grabbing = false;
}

// ==========================================
// CLEANED, LIGHTWEIGHT HTML DASHBOARD
// ==========================================
const char* html_page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background: #121212; color: white; text-align: center; padding-top: 20px; margin: 0;}
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
  <h2>Robot Arm Control Panel</h2>
  
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
// MAIN SETUP & LOOP
// ==========================================
void moveRobot(char cmd) {
    // Motor logic goes here
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);
  
  // Attach Servos to new valid output pins
  armServo.attach(ARM_SERVO_PIN);
  gripperServo.attach(GRIPPER_SERVO_PIN);
  
  // Send them home immediately
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
    
    if (incomingData.length() > 0) {
      int c1 = incomingData.indexOf(',');
      int c2 = incomingData.indexOf(',', c1 + 1);
      int c3 = incomingData.indexOf(',', c2 + 1);
      int c4 = incomingData.indexOf(',', c3 + 1);
      
      if (c1 > 0 && c2 > c1 && c3 > c2 && c4 > c3) {
        String cmd  = incomingData.substring(0, c1);
        int area    = incomingData.substring(c1 + 1, c2).toInt();
        int dist_cm = incomingData.substring(c2 + 1, c3).toInt();
        int tx      = incomingData.substring(c3 + 1, c4).toInt();
        int ty      = incomingData.substring(c4 + 1).toInt();
        
        // Print clean logs to USB console
        Serial.print("Action: ");      Serial.print(cmd);
        Serial.print("\t | Dist: ");    Serial.print(dist_cm);
        Serial.println(" cm");

        if (dist_cm > 0 && dist_cm <= grab_distance_cm) {
            moveRobot('S'); // Stop wheels instantly
            executeGrabSequence();
        } else {
            moveRobot(cmd.charAt(0)); 
        }
      }
    }
  }
}