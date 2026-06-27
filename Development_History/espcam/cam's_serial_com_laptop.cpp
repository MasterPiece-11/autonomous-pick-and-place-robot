#include <Arduino.h>
#include "esp_camera.h"

// --- CAMERA PIN DEFINITIONS (AI-Thinker) ---
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

// Onboard Red LED pin for visual diagnostics
#define LED_PIN           33 

void setup() {
  Serial.begin(115200); 
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Turn off LED initially (Active LOW)
  
  // Blink once to show code has successfully started executing setup()
  digitalWrite(LED_PIN, LOW); delay(200); digitalWrite(LED_PIN, HIGH);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;   config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;   config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;   config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;   config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM; config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 
  config.frame_size = FRAMESIZE_QQVGA; 
  config.jpeg_quality = 12;            
  config.fb_count = 1;

  // Initialize the camera lens
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    // CAMERA INITIALIZATION FAILED: Blink the onboard LED rapidly forever
    while(1) {
      digitalWrite(LED_PIN, LOW);  delay(100);
      digitalWrite(LED_PIN, HIGH); delay(100);
    }
  }

  // Camera initialized successfully! Turn LED solid ON to verify
  digitalWrite(LED_PIN, LOW); 
}

void loop() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    // If we fail to acquire a frame, blink once sharply
    digitalWrite(LED_PIN, HIGH); delay(50); digitalWrite(LED_PIN, LOW);
    return;
  }

  // Stream out the markers and payload
  Serial.print("###START###");
  Serial.write(fb->buf, fb->len);
  Serial.print("###END###");

  esp_camera_fb_return(fb);
  delay(30); 
}