#include "vision_color.h"
#include "esp_camera.h"

// --- LOCAL TRACKING TUNERS (YOUR EXACT ORIGINAL CONFIG) ---
static int min_h = 352, max_h = 2, min_s = 131, min_v = 35; // Red default
static int min_blob_area = 15; 
static int min_target_area = 500;  
static int max_target_area = 30000; 

// --- SEARCH ALGORITHM STATE MACHINE ---
enum TrackingState { FULL_SCAN, BORDER_WATCH, LOCKED, LOST_BUT_RETAINING };
static TrackingState state = FULL_SCAN;

static int tx = 80, ty = 60, win = 80; 
static int lost_frame_counter = 0;
const int max_memory_frames = 15;
static unsigned long last_full_scan = 0;
static int smoothed_area = 0;

// --- COLOR SELECTION PRESETS ---
void applyColorPreset(int color_id) {
    switch (color_id) {
        case 1: min_h=244; max_h=284; min_s=84; min_v=44; break;   // Purple
        case 2: min_h=145; max_h=204; min_s=45; min_v=138; break;  // Cyan
        case 3: min_h=352; max_h=2;   min_s=131; min_v=35; break;  // Red
        case 4: min_h=328; max_h=352; min_s=117; min_v=117; break; // Pink
        case 5: min_h=40;  max_h=60;  min_s=117; min_v=117; break; // Yellow
        default: return; 
    }
}

// --- CONVERT RGB565 BYTES TO INTERNAL INT ---
static uint16_t get_pixel(camera_fb_t *fb, int x, int y) {
    int index = (y * 160 + x) * 2;
    return (fb->buf[index] << 8) | fb->buf[index + 1];
}

// --- HSV THRESHOLD MATHEMATICAL FILTER ---
static bool check_color(uint16_t pixel) {
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

// ============================================================
// CORE PROCESSING LOOP (EXHAUSTIVE BALANCING FRAME SCAN)
// ============================================================
void vision_color_loop() {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    long sum_x = 0, sum_y = 0, count = 0;
    int min_x = 160, max_x = 0;
    int min_y = 120, max_y = 0;
    String cmd = "S"; 

    if (state == BORDER_WATCH && (millis() - last_full_scan) > 2000) {
        state = FULL_SCAN;
    }

    // Inline pixel accumulation macro to track bounding target dimensions
    #define PROCESS_PIXEL(px, py) do { \
        sum_x += (px); sum_y += (py); count++; \
        if ((px) < min_x) min_x = (px); \
        if ((px) > max_x) max_x = (px); \
        if ((py) < min_y) min_y = (py); \
        if ((py) > max_y) max_y = (py); \
    } while(0)

    // Run structural region searches depending on machine tracking context
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

    long send_area = 0;
    int distance_cm = 0; 

    // Target locked verification branch
    if (count > min_blob_area) {
        tx = sum_x / count; 
        ty = sum_y / count;
        tx = 159 - tx; // Invert camera image symmetry for orientation alignment

        state = LOCKED;
        lost_frame_counter = 0;

        int box_width = max_x - min_x;
        int box_height = max_y - min_y;
        long bounding_area = box_width * box_height;

        if (smoothed_area == 0) smoothed_area = bounding_area;
        else smoothed_area = (smoothed_area * 3 + bounding_area) / 4;
        
        send_area = smoothed_area;

        // ============================================================
        // THE PIXEL DENSITY SHIELD: Kills far-away phantom grabs
        // ============================================================
        if (count < 350) { 
            // If we have fewer than 350 actual pixels, the ball is physically far away.
            // Override the stretched bounding box and force a safe distance!
            distance_cm = 999; 
        }

        else if (send_area > 0) {
            distance_cm = (int)sqrt(512000.0 / send_area); // Applied focal constant inversion
        }

        // Steer navigation evaluation assignments
        if (tx < 70)       cmd = "L";
        else if (tx > 90)  cmd = "R";
        else {
            // Only allow forward/backward driving if the ball is confirmed real
            if (distance_cm == 999)               cmd = "F"; 
            else if (send_area < min_target_area)      cmd = "F"; 
            else if (send_area > max_target_area) cmd = "B"; 
            else                                  cmd = "S"; 
        }
        
    } else {
        // Handle tracking loss gracefully via structural retention buffers
        if (state == LOCKED || state == LOST_BUT_RETAINING) {
            state = LOST_BUT_RETAINING;
            lost_frame_counter++;
            send_area = smoothed_area; 
            
            if (send_area > 0) {
                distance_cm = (int)sqrt(512000.0 / send_area);
            }

            if (tx < 70)      cmd = "L";
            else if (tx > 90) cmd = "R";
            else {
                if (send_area < min_target_area)      cmd = "F";
                else if (send_area > max_target_area) cmd = "B";
                else                                  cmd = "S";
            }

            if (lost_frame_counter >= max_memory_frames) {
                state = FULL_SCAN;
                lost_frame_counter = 0;
                smoothed_area = 0; send_area = 0; distance_cm = 0;
                cmd = "S";
            }
        } else {
            cmd = "S"; send_area = 0; distance_cm = 0;
        }
    }

    // Ship results directly to the Main ESP32 using Serial execution pipes
    String payload = cmd + "," + String(send_area) + "," + String(distance_cm) + "," + String(tx) + "," + String(ty);
    Serial.println(payload);

    esp_camera_fb_return(fb); // Release frame buffer registry to prevent memory allocation freezes
}