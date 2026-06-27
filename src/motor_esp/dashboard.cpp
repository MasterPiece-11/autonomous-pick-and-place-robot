#include "dashboard.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h> // ◄ NEW: Built-in ESP32 flash memory writing library
#include "motors.h"  // ◄ Include this to gain access to motor control functions

extern int current_color_id;
extern int grab_distance_cm;
extern int robot_mode; 
extern int live_area;

extern bool has_target;

// Pulls in variables and functions that live in main.cpp or other files
extern bool system_armed;
extern void motor_stop();

WebServer server(80);
String log_buffer = ""; 

void add_log(String msg) {
  Serial.println(msg);            
  log_buffer += "[" + String(millis()/1000) + "s] " + msg + "\n"; 
  if (log_buffer.length() > 2000) {
    log_buffer = log_buffer.substring(log_buffer.length() - 1000);
  }
}

// ==========================================
// HTML DASHBOARD WITH MODE 3 OTA BUTTON
// ==========================================
const char* html_page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background: #121212; color: white; text-align: center; padding-top: 20px; margin: 0;}
    
    /* NEW: Safety Control Panel */
    .safety-box { background: #1e1e1e; padding: 15px; margin: 15px auto; max-width: 320px; border-radius: 10px; border: 1px solid #444; display: flex; justify-content: space-between;}
    .btn-start { background: #28a745; color: white; font-weight: bold; font-size: 16px; border: none; padding: 15px; border-radius: 5px; cursor: pointer; width: 48%;}
    .btn-stop { background: #dc3545; color: white; font-weight: bold; font-size: 16px; border: none; padding: 15px; border-radius: 5px; cursor: pointer; width: 48%;}
    .btn-start:active, .btn-stop:active { transform: scale(0.95); }

    .mode-container { background: #1e1e1e; padding: 15px; margin: 15px auto; max-width: 320px; border-radius: 10px; border: 1px solid #444;}
    .mode-btn { border: 2px solid #555; background: #2b2b2b; color: white; padding: 10px 15px; font-weight: bold; border-radius: 5px; cursor: pointer; width: 45%; margin: 0 4px;}
    .mode-active { background: #00ff66; color: black; border-color: #00ff66; }
    
    .btn-ota { width: 94%; margin-top: 12px; background: #8e44ad; border-color: #9b59b6; transition: 0.3s; }
    .btn-ota.mode-active { background: #f39c12; color: black; border-color: #f1c40f; }

    .terminal-box { background: #000; border: 2px solid #333; border-radius: 8px; max-width: 350px; margin: 15px auto; padding: 0;}
    .terminal-header { background: #222; color: #aaa; padding: 6px; font-size: 12px; text-align: left; font-weight: bold; border-bottom: 1px solid #333;}
    .terminal-body { height: 120px; overflow-y: auto; color: #00ff66; background: #050505; font-family: monospace; font-size: 13px; text-align: left; padding: 10px; white-space: pre-wrap;}

    .slider-box { background: #1e1e1e; padding: 15px; margin: 15px auto; max-width: 320px; border-radius: 10px; border: 1px solid #333;}
    .slider { width: 100%; margin-top: 10px; cursor: pointer; }
    
    .btn-grid { display: flex; flex-wrap: wrap; justify-content: center; gap: 10px; max-width: 350px; margin: auto;}
    .btn { border: none; padding: 15px; font-size: 16px; font-weight: bold; border-radius: 8px; cursor: pointer; width: 45%;}
    .btn:active { transform: scale(0.95); }
    .btn-red { background: #e74c3c; color: white; }
    .btn-yel { background: #f1c40f; color: black; }
    .btn-cya { background: #00bcd4; color: black; }
    .btn-pur { background: #9b59b6; color: white; }
    .btn-pin { background: #e84393; color: white; width: 93%;}
  </style>
</head>
<body>
  <h2>Robot Multi-Mode Core</h2>

  <div class="safety-box">
    <button class="btn-start" onclick="fetch('/system_start')">▶ START</button>
    <button class="btn-stop" onclick="fetch('/system_stop')">🛑 STOP</button>
  </div>

  <div class="mode-container">
    <div style="margin-bottom: 10px; font-weight: bold; font-size: 14px; color: #aaa;">SYSTEM OPERATION MODE</div>
    <button id="mode0" class="mode-btn mode-active" onclick="switchMode(0)">BALL HUNT</button>
    <button id="mode1" class="mode-btn" onclick="switchMode(1)">APRIL TAG</button>
    <button id="mode3" class="mode-btn btn-ota" onclick="switchMode(3)">⚡ ENTER OTA PROGRAMMING</button>
  </div>

  <div class="slider-box" style="margin-top: 15px; background: #1a1a1a; border: 1px solid #444;">
    <table style="width: 100%; text-align: left; border-collapse: collapse; font-size: 14px;">
      <tr style="border-bottom: 1px solid #333;">
        <th style="padding: 6px; color: #aaa;">Target Metric</th>
        <th style="padding: 6px; color: #aaa; text-align: right;">Live Value</th>
      </tr>
      <tr>
        <td style="padding: 8px 6px;">Ball Pixels (Area Size)</td>
        <td id="stat-area" style="padding: 8px 6px; text-align: right; color: #00ff66; font-weight: bold; font-family: monospace;">0 px</td>
      </tr>
    </table>
  </div>

  <div class="terminal-box">
    <div class="terminal-header">🤖 ROBOT LOG MONITOR</div>
    <div id="terminal" class="terminal-body">Connecting to core...</div>
  </div>

  <div id="ball-controls">
    <div class="slider-box">
      <div>Grab Trigger Distance: <strong id="slider-label">30</strong> cm</div>
      <input type="range" min="3" max="60" value="10" class="slider" id="distSlider" oninput="changeDistance(this.value)">
    </div>

    <div class="btn-grid">
      <button class="btn btn-red" onclick="setTarget(3)">RED</button>
      <button class="btn btn-yel" onclick="setTarget(5)">YELLOW</button>
      <button class="btn btn-cya" onclick="setTarget(2)">CYAN</button>
      <button class="btn btn-pur" onclick="setTarget(1)">PURPLE</button>
      <button class="btn btn-pin" onclick="setTarget(4)">PINK</button>
    </div>
  </div>

  <script>
    function switchMode(modeNum) {
      fetch('/setMode?m=' + modeNum);
      
      document.getElementById('mode0').classList.remove('mode-active');
      document.getElementById('mode1').classList.remove('mode-active');
      document.getElementById('mode3').classList.remove('mode-active');
      
      document.getElementById('mode' + modeNum).classList.add('mode-active');
      
      let panel = document.getElementById('ball-controls');
      if(modeNum !== 0) panel.style.display = "none";
      else panel.style.display = "block";
    }

    function setTarget(id) { fetch('/setColor?id=' + id); }
    
    function changeDistance(val) {
      document.getElementById('slider-label').innerText = val;
      fetch('/setGrabDist?val=' + val);
    }

    // 1. Fetch Terminal Logs (Every 1 second)
    setInterval(() => {
      fetch('/getLogs')
        .then(response => response.text())
        .then(text => {
          if (text.length > 0) {
            let term = document.getElementById('terminal');
            term.innerText += text;
            term.scrollTop = term.scrollHeight; // Auto-scroll to bottom
          }
        });
    }, 1000);

    // 2. Fetch Ball Pixel Size (Fast update: Every 200ms)
    setInterval(() => {
      fetch('/getStats')
        .then(response => response.text())
        .then(val => {
          document.getElementById('stat-area').innerText = val + " px";
        });
    }, 200);
  </script>
</body>
</html>
)rawhtml";

// This page serves a simple upload form when you go to 192.168.4.1/update
void handleUpdatePage() {
  String html = "<html><head><style>body{font-family:Arial;background:#121212;color:white;text-align:center;padding-top:50px;}";
  html += ".box{background:#1e1e1e;padding:30px;border-radius:10px;display:inline-block;border:1px solid #333;}";
  html += "input[type=file]{background:#2b2b2b;padding:10px;color:white;border-radius:5px;}";
  html += "input[type=submit]{background:#8e44ad;color:white;border:none;padding:10px 20px;font-weight:bold;border-radius:5px;cursor:pointer;margin-top:20px;}</style></head>";
  html += "<body><div class='box'><h2>Main Board Firmware Wireless Upload</h2>";
  html += "<form method='POST' action='/doUpdate' enctype='multipart/form-data'>";
  html += "<input type='file' name='update'><br>";
  html += "<input type='submit' value='Flash Robot Brain'>";
  html += "</form></div></body></html>";
  server.send(200, "text/html", html);
}

// This handles the binary chunk streaming directly into the flash chips
void handleDoUpdate() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", (Update.hasError()) ? "FAIL: Reload firmware and try again." : "SUCCESS: Robot rebooting...");
  delay(1000);
  ESP.restart(); // Reboot the main board automatically when done!
}

void handleUploadChunk() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    add_log("OTA System: Microcode stream opened. Writing blocks...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { // Start writing to the backup partition
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) { // True tells the chip to set this new binary as the boot target
      add_log("OTA System: Success! Microcode completely applied.");
    } else {
      Update.printError(Serial);
    }
  }
}

// ==========================================
// SERVER ROUTING & SERVICE LOGIC
// ==========================================
void handleRoot() { server.send(200, "text/html", html_page); }
void handleGetLogs() { server.send(200, "text/plain", log_buffer); log_buffer = ""; }

void handleSetMode() {
  if (server.hasArg("m")) {
    robot_mode = server.arg("m").toInt();
    
    if (robot_mode == 1) add_log("Mode Changed: AprilTag Tracking.");
    else if (robot_mode == 3) add_log("WARNING: OTA PROGRAMMING MODE ACTIVE. Camera shutdown initiated.");
    else add_log("Mode Changed: Ball Sighting Context.");
    
    // Command type "3" tells the ESP32-CAM to shut down and turn on Wi-Fi
    String command = String(robot_mode) + ",0"; 
    Serial2.println(command);
  }
  server.send(200, "text/plain", "OK");
}

// Look for this specific function inside your dashboard.cpp file and replace it:
void handleSetColor() { 
  if (server.hasArg("id")) {
    current_color_id = server.arg("id").toInt();
    
    // UNLOCK THE ROBOT! Tell it the arm is empty and it can hunt again.
    has_target = false; 
    
    add_log("Target Updated: Hunting new color ID " + String(current_color_id));
    
    // Send the new color to the camera
    String command = "0," + String(current_color_id); 
    Serial2.println(command);
  }
  server.send(200, "text/plain", "OK");
}

void handleSetGrabDist() {
  if (server.hasArg("val")) {
    grab_distance_cm = server.arg("val").toInt();
    add_log("Dashboard Request: Grab distance slider set to " + String(grab_distance_cm) + " cm");
  }
  server.send(200, "text/plain", "OK");
}

void dashboard_init() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Robot_Control", "12345678");
  
  server.on("/", handleRoot);
  server.on("/getLogs", handleGetLogs); 
  server.on("/setMode", handleSetMode); 
  server.on("/setColor", handleSetColor);
  server.on("/setGrabDist", handleSetGrabDist); 
  // NEW: Sends raw area value to the web panel
  server.on("/getStats", []() {
    server.send(200, "text/plain", String(live_area));
  });
  // ============================================================
  // SAFETY ROUTES (Software Kill Switch & Arming)
  // ============================================================
  server.on("/system_start", HTTP_GET, []() {
    system_armed = true;
    add_log("SYSTEM ARMED: Motors engaged and ready for pilot.");
    right(); // Small nudge to visually confirm responsiveness
    delay(200);
    server.send(200, "text/plain", "ARMED");
  });

  server.on("/system_stop", HTTP_GET, []() {
    system_armed = false;
    motor_stop(); // Instantly cut power to the wheels
    
    // Flush out any commands hiding in the pipeline
    while (Serial2.available() > 0) { Serial2.read(); } 
    
    add_log("EMERGENCY HALT: Motors disabled. System disarmed.");
    server.send(200, "text/plain", "HALTED");
  });
  // ============================================================

  // ◄ NEW: Register the wireless browser updating channels
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/doUpdate", HTTP_POST, handleDoUpdate, handleUploadChunk);
  
  server.begin(); // Keeps everything running smoothly
  
  add_log("Web Core Server initiated at port 80.");
}

void dashboard_handle() { server.handleClient(); }