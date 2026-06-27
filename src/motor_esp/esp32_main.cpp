#include <Arduino.h>
#include "motors.h"
#include "servos.h"
#include "dashboard.h"

// ─── Shared state (read by dashboard.cpp via extern) ──────────
int  current_color_id   = 3;
int  live_area          = 0;
int  live_tx            = 80;
int  live_ty            = 60;
int  grab_distance_cm   = 10;
int  robot_mode         = 0;   // 0=Ball, 1=AprilTag, 2=Manual, 3=OTA
bool is_grabbing        = false;
bool has_target         = false;
bool system_armed       = false;

char last_logged_direction = ' ';

// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial2.begin(115200);
    motors_init();
    servos_init();
    dashboard_init();
}

void loop() {
    dashboard_handle(); // runs web server + manual watchdog

    // ── Safety lock ───────────────────────────────────────────
    if (!system_armed) {
        motor_stop();
        while (Serial2.available()) Serial2.read();
        return;
    }

    // ── Manual / OTA mode: motors handled by dashboard, flush serial ──
    if (robot_mode == 2 || robot_mode == 3) {
        motor_stop();
        while (Serial2.available()) Serial2.read();
        return;
    }

    // ── Bypass valve: robot is carrying a ball ─────────────────
    if (has_target) {
        motor_stop();
        while (Serial2.available()) Serial2.read();
        return;
    }

    // ── Serial buffer ─────────────────────────────────────────
    static String  buf           = "";
    static bool    mem_secured   = false;
    static int     last_mode     = -1;

    if (!mem_secured) { buf.reserve(128); mem_secured = true; }

    // Flush stale data on mode change
    if (robot_mode != last_mode) {
        last_mode = robot_mode;
        while (Serial2.available()) Serial2.read();
        buf = "";
    }

    // ── Read camera serial ────────────────────────────────────
    while (Serial2.available() && !is_grabbing) {
        char c = (char)Serial2.read();
        if (c == '\r') continue;
        if (c != '\n') { buf += c; continue; }

        buf.trim();

        // Camera IP report
        if (buf.startsWith("IP:")) {
            add_log("📡 CAM IP: " + buf.substring(3));
            buf = ""; continue;
        }

        // Data shield: reject bootloader noise
        if (buf.length() >= 5) {
            char fc = buf.charAt(0);

            // ── MODE 0: Ball Hunt ─────────────────────────────
            if (robot_mode == 0 &&
                (fc=='F'||fc=='B'||fc=='L'||fc=='R'||fc=='S')) {

                int c1 = buf.indexOf(',');
                int c2 = buf.indexOf(',', c1+1);
                int c3 = buf.indexOf(',', c2+1);
                int c4 = buf.indexOf(',', c3+1);

                if (c1 > 0) {
                    char dir = buf.charAt(0);

                    live_area = (c2>c1) ? buf.substring(c1+1, c2).toInt()
                                        : buf.substring(c1+1).toInt();

                    int dist_cm = 0;
                    if (c2>c1 && c3>c2) dist_cm = buf.substring(c2+1, c3).toInt();
                    else if (c2>c1)      dist_cm = buf.substring(c2+1).toInt();

                    if (c3>c2 && c4>c3) {
                        live_tx = buf.substring(c3+1, c4).toInt();
                        live_ty = buf.substring(c4+1).toInt();
                    }

                    // Grab trigger: centered + close
                    if (dist_cm > 0 && dist_cm <= grab_distance_cm && dir == 'F') {
                        motor_stop();
                        add_log("Target Lock: " + String(dist_cm) + " cm. Grabbing.");
                        execute_grab_sequence();
                        has_target = true;
                        while (Serial2.available()) Serial2.read();
                    } else {
                        // Steer
                        if      (dir == 'F') front();
                        else if (dir == 'B') back();
                        else if (dir == 'L') left();
                        else if (dir == 'R') right();
                        else                 motor_stop();

                        if (dir != last_logged_direction) {
                            if      (dir == 'F') add_log("Tracking forward.");
                            else if (dir == 'B') add_log("Reversing.");
                            else if (dir == 'L') add_log("Steering left.");
                            else if (dir == 'R') add_log("Steering right.");
                            else if (dir == 'S') add_log("Target lost. Searching.");
                            last_logged_direction = dir;
                        }
                    }
                }
            }

            // ── MODE 1: AprilTag ──────────────────────────────
            else if (robot_mode == 1 && buf.startsWith("TAG")) {
                add_log("AprilTag: " + buf);
                motor_stop();
            }
        }

        buf = "";
    }
}