#include "camera_hal.h"

// ============================================================
// AI-THINKER ESP32-CAM PHYSICAL PIN MAPPING
// ============================================================
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

// Function to handle the raw low-level camera configuration registry
bool init_camera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    
    // Connect the internal data lines to the physical camera sensor pins
    config.pin_d0 = Y2_GPIO_NUM;   config.pin_d1 = Y3_GPIO_NUM; 
    config.pin_d2 = Y4_GPIO_NUM;   config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;   config.pin_d5 = Y7_GPIO_NUM; 
    config.pin_d6 = Y8_GPIO_NUM;   config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM; 
    config.pin_vsync = VSYNC_GPIO_NUM; 
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM; 
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; 
    config.pin_reset = RESET_GPIO_NUM;
    
    config.xclk_freq_hz = 20000000; // Run internal clock at 20MHz
    config.pixel_format = PIXFORMAT_RGB565; // Force color mode for ball mode
    config.frame_size = FRAMESIZE_QQVGA;    // Keep 160x120 resolution
    config.fb_count = 1;                    // Single frame buffer saves RAM allocation

    // Initialize the physical camera and return true if successful
    if (esp_camera_init(&config) != ESP_OK) {
        return false; 
    }
    return true;
}