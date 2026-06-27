#include <opencv2/opencv.hpp>
#include <iostream>
#include <algorithm>

using namespace cv;
using namespace std;

// --- GLOBAL VARIABLES FOR HSV SLIDERS ---
int min_hue = 30;  
int max_hue = 20;   

// Saturation (0-255)
int min_sat = 200;  
int max_sat = 255;  // NEW: Ceiling for richness

// Value/Brightness (0-255)
int min_val = 80;   
int max_val = 255;  // NEW: Ceiling for brightness

// --- 1. HARDWARE SETUP ---
bool init_camera(VideoCapture& cap, int camera_index = 0) {
    cap.open(camera_index, CAP_V4L2);
    return cap.isOpened();
}

bool extract_frame(VideoCapture& cap, Mat& frame) {
    cap >> frame; 
    if (frame.empty()) return false;
    flip(frame, frame, 1); 
    resize(frame, frame, Size(160, 120)); 
    return true;
}

// --- 2. THE MATH: RGB TO HSV (ESP32 COMPATIBLE) ---
void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
    float r_f = r / 255.0f;
    float g_f = g / 255.0f;
    float b_f = b / 255.0f;

    float cmax = std::max({r_f, g_f, b_f});
    float cmin = std::min({r_f, g_f, b_f});
    float diff = cmax - cmin;

    v = cmax * 255.0f;

    if (cmax == 0) {
        s = 0;
    } else {
        s = (diff / cmax) * 255.0f;
    }

    if (diff == 0) {
        h = 0;
    } else if (cmax == r_f) {
        h = 60.0f * fmod(((g_f - b_f) / diff), 6.0f);
    } else if (cmax == g_f) {
        h = 60.0f * (((b_f - r_f) / diff) + 2.0f);
    } else {
        h = 60.0f * (((r_f - g_f) / diff) + 4.0f);
    }

    if (h < 0) h += 360.0f; 
}

// --- 3. COLOR ISOLATION (THE ESP32 BRAIN) ---
void isolate_color_hsv(uint8_t* pixel_array, int width, int height, Mat& output_mask) {
    output_mask = Mat::zeros(height, width, CV_8UC1);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int index = (y * width + x) * 3; 
            
            uint8_t b = pixel_array[index];
            uint8_t g = pixel_array[index + 1];
            uint8_t r = pixel_array[index + 2];

            float h, s, v;
            rgb_to_hsv(r, g, b, h, s, v);

            // UPDATED: Now checks that S and V are BETWEEN your min and max sliders!
            if (s >= min_sat && s <= max_sat && v >= min_val && v <= max_val) {
                
                bool is_red = false;
                if (min_hue > max_hue) {
                    if (h >= min_hue || h <= max_hue) is_red = true;
                } else {
                    if (h >= min_hue && h <= max_hue) is_red = true;
                }

                if (is_red) {
                    output_mask.at<uint8_t>(y, x) = 255; 
                }
            }
        }
    }
}

// --- MAIN LOOP ---
int main() {
    VideoCapture cap;
    Mat current_frame, color_mask;

    if (!init_camera(cap)) return -1;

    // --- CREATE THE HSV SLIDER MENU ---
    namedWindow("Controls", WINDOW_AUTOSIZE);
    createTrackbar("Min Hue", "Controls", &min_hue, 360);
    createTrackbar("Max Hue", "Controls", &max_hue, 360);
    
    createTrackbar("Min Sat", "Controls", &min_sat, 255);
    createTrackbar("Max Sat", "Controls", &max_sat, 255); // NEW
    
    createTrackbar("Min Val", "Controls", &min_val, 255);
    createTrackbar("Max Val", "Controls", &max_val, 255); // NEW

    cout << "HSV tracking running! All 6 parameters are now controllable." << endl;

    while (true) {
        if (!extract_frame(cap, current_frame)) break;

        isolate_color_hsv(current_frame.data, 160, 120, color_mask);

        Mat big_frame, big_mask;
        resize(current_frame, big_frame, Size(640, 480), 0, 0, INTER_NEAREST);
        resize(color_mask, big_mask, Size(640, 480), 0, 0, INTER_NEAREST);

        imshow("Robot Vision", big_frame);
        imshow("HSV Mask", big_mask);

        if (waitKey(30) == 'q') break; 
    }

    cap.release();
    destroyAllWindows();
    return 0;
}