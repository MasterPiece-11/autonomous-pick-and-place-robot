#include "dashboard.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "motors.h"
#include "servos.h"

extern int current_color_id;
extern int grab_distance_cm;
extern int robot_mode;
extern int live_area;
extern bool has_target;
extern bool system_armed;
extern void motor_stop();

WebServer server(80);
String log_buffer = "";

void add_log(String msg) {
    Serial.println(msg);
    log_buffer += "[" + String(millis()/1000) + "s] " + msg + "\n";
    if (log_buffer.length() > 2000)
        log_buffer = log_buffer.substring(log_buffer.length() - 1000);
}

// ─── Manual drive state ───────────────────────────────────────
static unsigned long last_move_ping  = 0;
static bool          manual_override = false;

bool is_manual_override() { return manual_override; }

// ─── HTML ────────────────────────────────────────────────────
const char* html_page = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Robot Control</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: Arial, sans-serif; background: #121212; color: white;
           text-align: center; padding: 16px; margin: 0; }
    h2   { margin: 0 0 14px; font-size: 18px; color: #eee; }

    /* ── Cards ── */
    .card { background: #1e1e1e; border: 1px solid #333; border-radius: 10px;
            padding: 14px; margin: 12px auto; max-width: 340px; }

    /* ── Start / Stop ── */
    .row  { display: flex; gap: 10px; justify-content: center; }
    .btn-start { flex:1; background:#28a745; color:white; font-weight:bold;
                 font-size:16px; border:none; padding:14px; border-radius:6px; cursor:pointer; }
    .btn-stop  { flex:1; background:#dc3545; color:white; font-weight:bold;
                 font-size:16px; border:none; padding:14px; border-radius:6px; cursor:pointer; }

    /* ── Mode buttons ── */
    .mode-row  { display:flex; flex-wrap:wrap; gap:8px; justify-content:center; margin-top:6px; }
    .mode-btn  { flex:1; min-width:90px; border:2px solid #555; background:#2b2b2b; color:white;
                 padding:10px 8px; font-weight:bold; border-radius:6px; cursor:pointer; font-size:13px; }
    .mode-active { background:#00ff66; color:black; border-color:#00ff66; }
    .btn-ota   { flex:100%; background:#8e44ad; border-color:#9b59b6; margin-top:4px; }
    .btn-ota.mode-active { background:#f39c12; color:black; border-color:#f1c40f; }

    /* ── D-pad ── */
    .dpad { display:grid; grid-template-columns:repeat(3,70px);
            grid-template-rows:repeat(3,58px); gap:6px; justify-content:center; }
    .dp   { background:#2b2b2b; border:2px solid #555; color:white; font-size:24px;
            font-weight:bold; border-radius:8px; cursor:pointer;
            display:flex; align-items:center; justify-content:center;
            user-select:none; -webkit-user-select:none; touch-action:none; }
    .dp:active, .dp.held { background:#00ff66; border-color:#00ff66; color:black; }
    .dp-blank { visibility:hidden; }
    .dp-stop  { font-size:18px; color:#666; background:#1a1a1a; border-color:#333; }

    /* ── Gripper panel ── */
    .grip-row { display:flex; gap:10px; justify-content:center; margin-top:4px; }
    .gbtn { flex:1; padding:13px 8px; font-size:14px; font-weight:bold;
            border:none; border-radius:6px; cursor:pointer; }
    .gbtn:active { transform:scale(0.95); }
    .gbtn-arm-up   { background:#2980b9; color:white; }
    .gbtn-arm-down { background:#1a5276; color:white; }
    .gbtn-open     { background:#27ae60; color:white; }
    .gbtn-close    { background:#922b21; color:white; }
    .gbtn-seq      { background:#8e44ad; color:white; flex:100%; margin-top:8px; padding:13px; }

    /* ── Terminal ── */
    .term-wrap { background:#000; border:2px solid #333; border-radius:8px;
                 max-width:340px; margin:12px auto; }
    .term-hdr  { background:#222; color:#aaa; padding:6px 10px; font-size:12px;
                 font-weight:bold; text-align:left; border-bottom:1px solid #333; }
    .term-body { height:110px; overflow-y:auto; color:#00ff66; font-family:monospace;
                 font-size:13px; text-align:left; padding:8px; white-space:pre-wrap; }

    /* ── Stats ── */
    .stat-val { color:#00ff66; font-weight:bold; font-family:monospace; text-align:right; }
  </style>
</head>
<body>
<h2>🤖 Robot Control Panel</h2>

<!-- START / STOP -->
<div class="card">
  <div class="row">
    <button class="btn-start" onclick="fetch('/system_start')">▶ START</button>
    <button class="btn-stop"  onclick="fetch('/system_stop')">🛑 STOP</button>
  </div>
</div>

<!-- MODE -->
<div class="card">
  <div style="font-size:12px;color:#aaa;font-weight:bold;margin-bottom:8px;">OPERATION MODE</div>
  <div class="mode-row">
    <button id="mode0" class="mode-btn mode-active" onclick="switchMode(0)">BALL HUNT</button>
    <button id="mode1" class="mode-btn"             onclick="switchMode(1)">APRIL TAG</button>
    <button id="mode2" class="mode-btn"             onclick="switchMode(2)">MANUAL</button>
    <button id="mode3" class="mode-btn btn-ota"     onclick="switchMode(3)">⚡ OTA FLASH</button>
  </div>
</div>

<!-- MOVEMENT D-PAD -->
<div class="card" id="drive-panel">
  <div style="font-size:12px;color:#aaa;font-weight:bold;margin-bottom:10px;">MOVEMENT</div>
  <div class="dpad">
    <div class="dp-blank"></div>
    <div class="dp" id="dp-F"
         onpointerdown="driveStart('F',this)" onpointerup="driveStop()" onpointerleave="driveStop()">▲</div>
    <div class="dp-blank"></div>

    <div class="dp" id="dp-L"
         onpointerdown="driveStart('L',this)" onpointerup="driveStop()" onpointerleave="driveStop()">◀</div>
    <div class="dp dp-stop" id="dp-S"
         onpointerdown="driveStart('S',this)" onpointerup="driveStop()" onpointerleave="driveStop()">■</div>
    <div class="dp" id="dp-R"
         onpointerdown="driveStart('R',this)" onpointerup="driveStop()" onpointerleave="driveStop()">▶</div>

    <div class="dp-blank"></div>
    <div class="dp" id="dp-B"
         onpointerdown="driveStart('B',this)" onpointerup="driveStop()" onpointerleave="driveStop()">▼</div>
    <div class="dp-blank"></div>
  </div>
</div>

<!-- GRIPPER CONTROLS -->
<div class="card" id="grip-panel">
  <div style="font-size:12px;color:#aaa;font-weight:bold;margin-bottom:10px;">ARM & GRIPPER</div>
  <div class="grip-row">
    <button class="gbtn gbtn-arm-up"   onclick="sendCmd('/arm?pos=up')">⬆ Arm Up</button>
    <button class="gbtn gbtn-arm-down" onclick="sendCmd('/arm?pos=down')">⬇ Arm Down</button>
  </div>
  <div class="grip-row" style="margin-top:8px;">
    <button class="gbtn gbtn-open"  onclick="sendCmd('/gripper?pos=open')">✋ Open</button>
    <button class="gbtn gbtn-close" onclick="sendCmd('/gripper?pos=close')">✊ Close</button>
  </div>
  <div class="grip-row">
    <button class="gbtn gbtn-seq" onclick="sendCmd('/grab_sequence')">⚙ Run Full Grab Sequence</button>
  </div>
</div>

<!-- LIVE STATS -->
<div class="card">
  <table style="width:100%;font-size:14px;border-collapse:collapse;">
    <tr style="border-bottom:1px solid #333;">
      <th style="padding:6px;color:#aaa;text-align:left;">Metric</th>
      <th style="padding:6px;color:#aaa;text-align:right;">Value</th>
    </tr>
    <tr>
      <td style="padding:8px 6px;">Ball Area (px)</td>
      <td id="stat-area" class="stat-val" style="padding:8px 6px;">0 px</td>
    </tr>
  </table>
</div>

<!-- TERMINAL -->
<div class="term-wrap">
  <div class="term-hdr">📟 ROBOT LOG</div>
  <div id="terminal" class="term-body">Connecting...</div>
</div>

<script>
  function sendCmd(url) { fetch(url); }

  // ── Mode switching ──
  function switchMode(n) {
    fetch('/setMode?m=' + n);
    ['mode0','mode1','mode2','mode3'].forEach(id => {
      document.getElementById(id).classList.remove('mode-active');
    });
    document.getElementById('mode' + n).classList.add('mode-active');
  }

  // ── D-pad drive ──
  let driveTimer = null;
  let activeDP   = null;

  function driveStart(dir, el) {
    driveStop();
    fetch('/move?dir=' + dir);
    activeDP = el;
    el.classList.add('held');
    driveTimer = setInterval(() => fetch('/move?dir=' + dir), 150);
  }

  function driveStop() {
    clearInterval(driveTimer); driveTimer = null;
    if (activeDP) { activeDP.classList.remove('held'); activeDP = null; }
    fetch('/move?dir=S');
  }

  // ── Log polling ──
  setInterval(() => {
    fetch('/getLogs').then(r => r.text()).then(t => {
      if (!t.length) return;
      let term = document.getElementById('terminal');
      term.innerText += t;
      term.scrollTop = term.scrollHeight;
    });
  }, 1000);

  // ── Stats polling ──
  setInterval(() => {
    fetch('/getStats').then(r => r.text()).then(v => {
      document.getElementById('stat-area').innerText = v + ' px';
    });
  }, 200);
</script>
</body>
</html>
)rawhtml";

// ─── OTA page ────────────────────────────────────────────────
void handleUpdatePage() {
    String html = "<html><head><style>body{font-family:Arial;background:#121212;color:white;text-align:center;padding-top:50px;}";
    html += ".box{background:#1e1e1e;padding:30px;border-radius:10px;display:inline-block;border:1px solid #333;}";
    html += "input[type=file]{background:#2b2b2b;padding:10px;color:white;border-radius:5px;}";
    html += "input[type=submit]{background:#8e44ad;color:white;border:none;padding:10px 20px;font-weight:bold;border-radius:5px;cursor:pointer;margin-top:20px;}</style></head>";
    html += "<body><div class='box'><h2>Firmware OTA Upload</h2>";
    html += "<form method='POST' action='/doUpdate' enctype='multipart/form-data'>";
    html += "<input type='file' name='update'><br><input type='submit' value='Flash Robot'>";
    html += "</form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleDoUpdate() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "SUCCESS: Rebooting...");
    delay(1000);
    ESP.restart();
}

void handleUploadChunk() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        add_log("OTA: Writing firmware...");
        Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) add_log("OTA: Success!");
        else Update.printError(Serial);
    }
}

// ─── Route handlers ──────────────────────────────────────────
void handleRoot()    { server.send(200, "text/html", html_page); }
void handleGetLogs() { server.send(200, "text/plain", log_buffer); log_buffer = ""; }

void handleSetMode() {
    if (server.hasArg("m")) {
        robot_mode = server.arg("m").toInt();
        // Reset manual override when leaving manual mode
        if (robot_mode != 2) { manual_override = false; motor_stop(); }
        String names[] = {"Ball Hunt","AprilTag","Manual","OTA"};
        add_log("Mode: " + names[robot_mode]);
        Serial2.println(String(robot_mode) + ",0");
    }
    server.send(200, "text/plain", "OK");
}

// ── Manual move (D-pad) ──
void handleMove() {
    if (!system_armed) { server.send(200, "text/plain", "DISARMED"); return; }
    if (server.hasArg("dir")) {
        char d = server.arg("dir").charAt(0);
        last_move_ping  = millis();
        manual_override = (d != 'S');
        switch (d) {
            case 'F': front();      break;
            case 'B': back();       break;
            case 'L': left();       break;
            case 'R': right();      break;
            default:  motor_stop(); manual_override = false; break;
        }
    }
    server.send(200, "text/plain", "OK");
}

// ── Arm ──
void handleArm() {
    if (server.hasArg("pos")) {
        String pos = server.arg("pos");
        if (pos == "up")   arm_up();
        else               arm_down();
    }
    server.send(200, "text/plain", "OK");
}

// ── Gripper ──
void handleGripper() {
    if (server.hasArg("pos")) {
        String pos = server.arg("pos");
        if (pos == "open") grip();
        else               ungrip();
    }
    server.send(200, "text/plain", "OK");
}

// ── Full grab sequence ──
void handleGrabSequence() {
    execute_grab_sequence();
    server.send(200, "text/plain", "OK");
}

// ── Watchdog: cuts motors if D-pad hold loses connection ──
void manual_drive_watchdog() {
    if (manual_override && (millis() - last_move_ping > 500)) {
        motor_stop();
        manual_override = false;
    }
}

// ─── Init ────────────────────────────────────────────────────
void dashboard_init() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Robot_Control", "12345678");

    server.on("/",              handleRoot);
    server.on("/getLogs",       handleGetLogs);
    server.on("/setMode",       handleSetMode);
    server.on("/move",          HTTP_GET, handleMove);
    server.on("/arm",           HTTP_GET, handleArm);
    server.on("/gripper",       HTTP_GET, handleGripper);
    server.on("/grab_sequence", HTTP_GET, handleGrabSequence);
    server.on("/getStats", []() {
        server.send(200, "text/plain", String(live_area));
    });
    server.on("/system_start", HTTP_GET, []() {
        system_armed    = true;
        manual_override = false;
        add_log("SYSTEM ARMED.");
        server.send(200, "text/plain", "ARMED");
    });
    server.on("/system_stop", HTTP_GET, []() {
        system_armed    = false;
        manual_override = false;
        motor_stop();
        while (Serial2.available()) Serial2.read();
        add_log("EMERGENCY HALT.");
        server.send(200, "text/plain", "HALTED");
    });
    server.on("/update",   HTTP_GET,  handleUpdatePage);
    server.on("/doUpdate", HTTP_POST, handleDoUpdate, handleUploadChunk);

    server.begin();
    add_log("Dashboard ready at 192.168.4.1");
}

void dashboard_handle() {
    server.handleClient();
    manual_drive_watchdog();
}