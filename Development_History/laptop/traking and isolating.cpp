#include <opencv2/opencv.hpp>
#include <iostream>
#include <algorithm>

using namespace cv;
using namespace std;

// --- GLOBAL VARIABLES FOR HSV SLIDERS ---
int min_hue = 340;  
int max_hue = 20;   
int min_sat = 120;  
int max_sat = 255;  
int min_val = 80;   
int max_val = 255;  

// --- NEW: TARGET DATA STRUCTURE ---
struct Target {
    bool found;
    int x;       // Center X
    int y;       // Center Y
    int min_x;   // Bounding box left
    int min_y;   // Bounding box top
    int max_x;   // Bounding box right
    int max_y;   // Bounding box bottom
    int area;    // Total white pixels (helps filter out tiny noise specs!)
};

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

// --- 2. RGB TO HSV ---
void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
    float r_f = r / 255.0f;
    float g_f = g / 255.0f;
    float b_f = b / 255.0f;

    float cmax = std::max({r_f, g_f, b_f});
    float cmin = std::min({r_f, g_f, b_f});
    float diff = cmax - cmin;

    v = cmax * 255.0f;
    s = (cmax == 0) ? 0 : (diff / cmax) * 255.0f;

    if (diff == 0) h = 0;
    else if (cmax == r_f) h = 60.0f * fmod(((g_f - b_f) / diff), 6.0f);
    else if (cmax == g_f) h = 60.0f * (((b_f - r_f) / diff) + 2.0f);
    else h = 60.0f * (((r_f - g_f) / diff) + 4.0f);

    if (h < 0) h += 360.0f; 
}

// --- 3. COLOR ISOLATION ---
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

            if (s >= min_sat && s <= max_sat && v >= min_val && v <= max_val) {
                bool is_red = false;
                if (min_hue > max_hue) {
                    if (h >= min_hue || h <= max_hue) is_red = true;
                } else {
                    if (h >= min_hue && h <= max_hue) is_red = true;
                }

                if (is_red) output_mask.at<uint8_t>(y, x) = 255; 
            }
        }
    }
}

// --- 4. NEW: FIND THE BALL CENTER (ESP32 COMPATIBLE) ---
Target find_blob_stats(Mat& mask) {
    Target t;
    t.found = false;
    t.area = 0;
    t.min_x = mask.cols; t.min_y = mask.rows;
    t.max_x = 0; t.max_y = 0;
    
    long sum_x = 0;
    long sum_y = 0;

    for (int y = 0; y < mask.rows; y++) {
        for (int x = 0; x < mask.cols; x++) {
            if (mask.at<uint8_t>(y, x) == 255) {
                sum_x += x;
                sum_y += y;
                t.area++;

                // Expand bounding box
                if (x < t.min_x) t.min_x = x;
                if (x > t.max_x) t.max_x = x;
                if (y < t.min_y) t.min_y = y;
                if (y > t.max_y) t.max_y = y;
            }
        }
    }

    // If we found enough pixels to safely say it's the ball (e.g., more than 10 pixels to ignore noise)
    if (t.area > 10) {
        t.found = true;
        t.x = sum_x / t.area;
        t.y = sum_y / t.area;
    }

    return t;
}

// --- MAIN LOOP ---
int main() {
    VideoCapture cap;
    Mat current_frame, color_mask;

    if (!init_camera(cap)) return -1;

    namedWindow("Controls", WINDOW_AUTOSIZE);
    createTrackbar("Min Hue", "Controls", &min_hue, 360);
    createTrackbar("Max Hue", "Controls", &max_hue, 360);
    createTrackbar("Min Sat", "Controls", &min_sat, 255);
    createTrackbar("Max Sat", "Controls", &max_sat, 255);
    createTrackbar("Min Val", "Controls", &min_val, 255);
    createTrackbar("Max Val", "Controls", &max_val, 255);

    while (true) {
        if (!extract_frame(cap, current_frame)) break;

        // 1. Create the mask
        isolate_color_hsv(current_frame.data, 160, 120, color_mask);

        // 2. Scan the mask to find the ball
        Target ball = find_blob_stats(color_mask);

        // Zoom in for the laptop display (Multiply scale by 4 since 160x4 = 640)
        Mat big_frame, big_mask;
        resize(current_frame, big_frame, Size(640, 480), 0, 0, INTER_NEAREST);
        resize(color_mask, big_mask, Size(640, 480), 0, 0, INTER_NEAREST);

        // 3. Draw on the screen if found
        if (ball.found) {
            int scale = 4; // Because we resized from 160x120 to 640x480
            
            // Draw Bounding Box (Green)
            rectangle(big_frame, 
                      Point(ball.min_x * scale, ball.min_y * scale), 
                      Point(ball.max_x * scale, ball.max_y * scale), 
                      Scalar(0, 255, 0), 2);
            
            // Draw Center Crosshairs (Blue)
            circle(big_frame, Point(ball.x * scale, ball.y * scale), 5, Scalar(255, 0, 0), -1);
            
            // Print the coordinates to the terminal so you can see the raw data!
            cout << "Ball Target: X=" << ball.x << ", Y=" << ball.y << ", Area=" << ball.area << endl;
        }

        imshow("Robot Vision", big_frame);
        imshow("HSV Mask", big_mask);

        if (waitKey(30) == 'q') break; 
    }

    cap.release();
    destroyAllWindows();
    return 0;
}