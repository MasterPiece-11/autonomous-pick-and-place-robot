#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

// Wi-Fi Credentials - EDIT THESE FOR YOUR NETWORK
const char* ssid = "West_Dorms";
const char* password = "Ejust@P@ssw0rd$$";

// Hardware Pin Definitions for AI-Thinker ESP32-CAM
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

enum State { FULL, BORDER, LOCKED, LOST_RETAINING };
State currentState = FULL;
int tx = 80, ty = 60, win = 30;
unsigned long lastFullScan = 0;

// Memory parameters
int lostFramesCount = 0;
const int maxMemoryFrames = 15;

// Tuned HSV (Red)
int min_h = 340, max_h = 20, min_s = 120, min_v = 80;

void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
    float rf = r/255.0, gf = g/255.0, bf = b/255.0;
    float cmax = max({rf, gf, bf}), cmin = min({rf, gf, bf}), diff = cmax-cmin;
    v = cmax * 255;
    s = (cmax == 0) ? 0 : (diff/cmax)*255;
    if (diff == 0) h = 0;
    else if (cmax == rf) h = 60 * fmod(((gf-bf)/diff), 6);
    else if (cmax == gf) h = 60 * (((bf-rf)/diff)+2);
    else h = 60 * (((rf-gf)/diff)+4);
    if (h < 0) h += 360;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL;
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");

    while(true) {
        fb = esp_camera_fb_get();
        if(!fb) continue;

        uint16_t *buf = (uint16_t *)fb->buf;
        long sx = 0, sy = 0;
        int count = 0;

        // Reset timer check
        if (currentState == BORDER && (millis() - lastFullScan > 2000)) currentState = FULL;

        // --- MATH MODES ---
        if (currentState == FULL) {
            lastFullScan = millis();
            for (int i = 0; i < 19200; i++) {
                uint16_t p = buf[i];
                float h, s, v; rgb_to_hsv((p>>8)&0xF8, (p>>3)&0xFC, (p<<3)&0xF8, h, s, v);
                if (s >= min_s && v >= min_v && (h>=340 || h<=20)) { sx += (i%160); sy += (i/160); count++; }
            }
            if (count <= 10) currentState = BORDER;
        } 
        else if (currentState == BORDER) {
            for (int i = 0; i < 19200; i++) {
                int x = i % 160, y = i / 160;
                if (x < 3 || x > 156 || y < 3 || y > 116) {
                    uint16_t p = buf[i];
                    float h, s, v; rgb_to_hsv((p>>8)&0xF8, (p>>3)&0xFC, (p<<3)&0xF8, h, s, v);
                    if (s >= min_s && v >= min_v && (h>=340 || h<=20)) { sx += x; sy += y; count++; }
                }
            }
        } 
        else if (currentState == LOCKED || currentState == LOST_RETAINING) {
            int x1 = max(0, tx-win), x2 = min(160, tx+win), y1 = max(0, ty-win), y2 = min(120, ty+win);
            for (int y = y1; y < y2; y++) {
                for (int x = x1; x < x2; x++) {
                    uint16_t p = buf[y*160+x];
                    float h, s, v; rgb_to_hsv((p>>8)&0xF8, (p>>3)&0xFC, (p<<3)&0xF8, h, s, v);
                    if (s >= min_s && v >= min_v && (h>=340 || h<=20)) { sx += x; sy += y; count++; }
                }
            }
        }

        // --- CONTROL OUTPUT ---
        if (count > 10) {
            tx = sx/count; ty = sy/count; 
            currentState = LOCKED;
            lostFramesCount = 0; 
            
            const char* cmd = (tx < 70) ? "L" : (tx > 90) ? "R" : "Stop";
            Serial.printf("DATA:%d,%d,%d,%s\n", tx, ty, count, cmd);
        } 
        else {
            if (currentState == LOCKED || currentState == LOST_RETAINING) {
                currentState = LOST_RETAINING;
                lostFramesCount++;
                
                const char* cmd = (tx < 70) ? "L" : (tx > 90) ? "R" : "Stop";
                Serial.printf("DATA:%d,%d,0,%s (ESTIMATING)\n", tx, ty, cmd);

                if (lostFramesCount >= maxMemoryFrames) {
                    currentState = FULL;
                    lostFramesCount = 0;
                    Serial.println("DATA:0,0,0,Stop");
                }
            } else {
                Serial.println("DATA:0,0,0,Stop");
            }
        }

        // Web Streaming
        uint8_t * j_buf = NULL; size_t j_len = 0;
        fmt2jpg(fb->buf, fb->len, 160, 120, fb->format, 20, &j_buf, &j_len);
        httpd_resp_send_chunk(req, (const char *)j_buf, j_len);
        free(j_buf);
        esp_camera_fb_return(fb);
    }
    return ESP_OK;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    
    // We must use RGB565 to allow direct pixel manipulation in RAM
    config.pixel_format = PIXFORMAT_RGB565; 
    config.frame_size = FRAMESIZE_QQVGA; // 160x120
    config.jpeg_quality = 12;
    config.fb_count = 1;

    // Camera Init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    // Start Web Server
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    httpd_uri_t uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = stream_handler,
        .user_ctx = NULL
    };
    
    if (httpd_start(&server, &server_config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri);
    }
    
    Serial.print("Camera Stream Ready! Go to: http://");
    Serial.println(WiFi.localIP());
}

void loop() {
    // Web streaming handles its own loop, so we just keep the CPU alive.
    delay(1); 
}