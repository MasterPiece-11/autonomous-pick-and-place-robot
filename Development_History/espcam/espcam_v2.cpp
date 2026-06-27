#include "esp_camera.h"
#include <Arduino.h>

// ==========================================
// AI-THINKER ESP32-CAM PINOUT
// ==========================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// --- GLOBAL TRACKING VARIABLES ---
int min_h = 352, max_h = 2, min_s = 131, min_v = 35; // Default to Red
int min_blob_area = 15; // Minimum colored pixels to qualify as a "find"

// --- DISTANCE TUNING VARIABLES ---
int min_target_area = 500;  // Under this bounding-box area = Move Forward
int max_target_area = 1500; // Over this bounding-box area = Move Backward

// --- STATE MACHINE VARIABLES ---
int current_phase = 1; // 1 = Color, 2 = AprilTag
enum TrackingState { FULL_SCAN, BORDER_WATCH, LOCKED, LOST_BUT_RETAINING };
TrackingState state = FULL_SCAN;

int tx = 80, ty = 60, win = 80; 
int lost_frame_counter = 0;
const int max_memory_frames = 15;
unsigned long last_full_scan = 0;
int smoothed_area = 0; 
String serial_buffer = "";

// --- COLOR PRESET APPLIER ---
void applyColorPreset(int color_id) {
    switch (color_id) {
        case 1: min_h=244; max_h=284; min_s=84; min_v=44; break; // Purple
        case 2: min_h=145; max_h=204; min_s=45; min_v=138; break; // Cyan
        case 3: min_h=352; max_h=2; min_s=131; min_v=35; break; // Red
        case 4: min_h=328; max_h=352; min_s=117; min_v=117; break; // Pink
        case 5: min_h=40; max_h=60; min_s=117; min_v=117; break; // Yellow
        default: return; 
    }
}

// --- SERIAL COMMAND PARSER ---
void processSerialCommand(String cmd_str) {
    int comma_idx = cmd_str.indexOf(',');
    if (comma_idx > 0) {
        int phase = cmd_str.substring(0, comma_idx).toInt();
        int color = cmd_str.substring(comma_idx + 1).toInt();
        
        if (phase == 1) {
            current_phase = 1;
            applyColorPreset(color);
        } else if (phase == 2) {
            current_phase = 2;
        }
    }
}

// --- PIXEL EXTRACTOR ---
uint16_t get_pixel(camera_fb_t *fb, int x, int y) {
    int index = (y * 160 + x) * 2;
    return (fb->buf[index] << 8) | fb->buf[index + 1];
}

// --- DYNAMIC COLOR CHECKER ---
bool check_color(uint16_t pixel) {
    uint8_t r = ((pixel >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((pixel >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (pixel & 0x1F) * 255 / 31;

    uint8_t cmax = max(r, max(g, b));
    uint8_t cmin = min(r, min(g, b));
    uint8_t diff = cmax - cmin;

    uint8_t v = cmax;
    uint8_t s = (cmax == 0) ? 0 : (diff * 255) / cmax;
    int16_t h = 0;

    if (diff != 0) {
        if (cmax == r) {
            h = 60 * (g - b) / diff;
            if (h < 0) h += 360;
        } else if (cmax == g) {
            h = 60 * (b - r) / diff + 120;
        } else {
            h = 60 * (r - g) / diff + 240;
        }
    }
    return (s >= min_s && v >= min_v) && ((min_h > max_h) ? (h >= min_h || h <= max_h) : (h >= min_h && h <= max_h));
}

void setup() {
    Serial.begin(115200);
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QQVGA; 
    config.fb_count = 1;

    if (esp_camera_init(&config) != ESP_OK) return;
    last_full_scan = millis();
}

void loop() {
    // 1. Check Serial for incoming commands (e.g., "1,3" for Phase 1, Color Red)
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            processSerialCommand(serial_buffer);
            serial_buffer = "";
        } else if (c != '\r') {
            serial_buffer += c;
        }
    }

    // Bypass color tracking if in Phase 2
    if (current_phase == 2) {
        // ... AprilTag Logic placeholder ...
        delay(100);
        return;
    }

    // 2. Capture Frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    long sum_x = 0, sum_y = 0, count = 0;
    
    // Bounding Box Trackers
    int min_x = 160, max_x = 0;
    int min_y = 120, max_y = 0;
    
    String cmd = "S"; 

    if (state == BORDER_WATCH && (millis() - last_full_scan) > 2000) {
        state = FULL_SCAN;
    }

    // Helper macro to update all our tracking variables at once
    #define PROCESS_PIXEL(px, py) do { \
        sum_x += (px); sum_y += (py); count++; \
        if ((px) < min_x) min_x = (px); \
        if ((px) > max_x) max_x = (px); \
        if ((py) < min_y) min_y = (py); \
        if ((py) > max_y) max_y = (py); \
    } while(0)

    // 3. Scan Pixels Based on State
    if (state == FULL_SCAN) {
        last_full_scan = millis();
        for (int y = 0; y < 120; y++) {
            for (int x = 0; x < 160; x++) {
                if (check_color(get_pixel(fb, x, y))) PROCESS_PIXEL(x, y);
            }
        }
        if (count <= min_blob_area) state = BORDER_WATCH;
    } 
    else if (state == BORDER_WATCH) {
        int y_borders[] = {0, 1, 2, 117, 118, 119};
        for (int i = 0; i < 6; i++) {
            for (int x = 0; x < 160; x++) {
                if (check_color(get_pixel(fb, x, y_borders[i]))) PROCESS_PIXEL(x, y_borders[i]);
            }
        }
        int x_borders[] = {0, 1, 2, 157, 158, 159};
        for (int i = 0; i < 6; i++) {
            for (int y = 0; y < 120; y++) {
                if (check_color(get_pixel(fb, x_borders[i], y))) PROCESS_PIXEL(x_borders[i], y);
            }
        }
    }
    else if (state == LOCKED || state == LOST_BUT_RETAINING) {
        int x1 = max(0, tx - win / 2), x2 = min(160, tx + win / 2);
        int y1 = max(0, ty - win / 2), y2 = min(120, ty + win / 2);
        for (int y = y1; y < y2; y++) {
            for (int x = x1; x < x2; x++) {
                if (check_color(get_pixel(fb, x, y))) PROCESS_PIXEL(x, y);
            }
        }
    }

    // 4. Command and Target Processor
    long send_area = 0;

    if (count > min_blob_area) {
        tx = sum_x / count; 
        ty = sum_y / count;
        tx = 159 - tx; // Match layout flip logic

        state = LOCKED;
        lost_frame_counter = 0;

        // Bounding Area Calculation with Smoothing Filter
        int box_width = max_x - min_x;
        int box_height = max_y - min_y;
        long bounding_area = box_width * box_height;

        if (smoothed_area == 0) smoothed_area = bounding_area;
        else smoothed_area = (smoothed_area * 3 + bounding_area) / 4;
        
        send_area = smoothed_area;

        // Centering & Distance Logic
        if (tx < 70) {
            cmd = "L";
        } else if (tx > 90) {
            cmd = "R";
        } else {
            // Ball is perfectly centered. Now check distance!
            if (send_area < min_target_area) {
                cmd = "F"; 
            } else if (send_area > max_target_area) {
                cmd = "B"; 
            } else {
                cmd = "S"; 
            }
        }
        
    } else {
        if (state == LOCKED || state == LOST_BUT_RETAINING) {
            state = LOST_BUT_RETAINING;
            lost_frame_counter++;
            
            send_area = smoothed_area; // Keep retaining the last known distance

            // Estimate movements while in retaining memory
            if (tx < 70) cmd = "L";
            else if (tx > 90) cmd = "R";
            else {
                if (send_area < min_target_area) cmd = "F";
                else if (send_area > max_target_area) cmd = "B";
                else cmd = "S";
            }

            if (lost_frame_counter >= max_memory_frames) {
                state = FULL_SCAN;
                lost_frame_counter = 0;
                smoothed_area = 0;
                send_area = 0;
                cmd = "S";
            }
        } else {
            cmd = "S";
            send_area = 0;
        }
    }

    // 5. Send highly-compressed string over Serial to the main ESP32 Brain
    String payload = cmd + "," + String(send_area);
    Serial.println(payload);

    esp_camera_fb_return(fb); // Free memory
}