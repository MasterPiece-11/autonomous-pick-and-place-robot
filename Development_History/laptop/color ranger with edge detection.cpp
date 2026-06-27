#include <opencv2/opencv.hpp>
#include <iostream>
#include <algorithm>

using namespace cv;
using namespace std;

int min_hue = 352; 
int max_hue = 5;   
int min_sat = 196;  
int max_sat = 255;  
int min_val = 88;   // Keep this up! Setting it to 0 allows pure black noise.
int max_val = 255;  

// --- NEW: GLOBAL EXPOSURE VARIABLE ---
int exposure_val = 1000; // Initial default exposure value (Tune between 0 and 500)

bool init_camera(VideoCapture& cap, int camera_index = 0) {
    cap.open(camera_index, CAP_V4L2);
    if (!cap.isOpened()) return false;

    // Force manual exposure mode at startup so it listens to our slider
    cap.set(CAP_PROP_AUTO_EXPOSURE, 1); 
    return true;
}

bool extract_frame(VideoCapture& cap, Mat& frame) {
    cap >> frame; 
    if (frame.empty()) return false;
    flip(frame, frame, 1); 
    resize(frame, frame, Size(160, 120)); 
    return true;
}

void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
    float r_f = r / 255.0f; float g_f = g / 255.0f; float b_f = b / 255.0f;
    float cmax = std::max({r_f, g_f, b_f});
    float cmin = std::min({r_f, g_f, b_f});
    float diff = cmax - cmin;
    v = cmax * 255.0f;
    if (cmax == 0) s = 0;
    else s = (diff / cmax) * 255.0f;

    if (diff == 0) h = 0;
    else if (cmax == r_f) h = 60.0f * fmod(((g_f - b_f) / diff), 6.0f);
    else if (cmax == g_f) h = 60.0f * (((b_f - r_f) / diff) + 2.0f);
    else h = 60.0f * (((r_f - g_f) / diff) + 4.0f);
    if (h < 0) h += 360.0f; 
}

Rect isolate_color_hsv(uint8_t* pixel_array, int width, int height, Mat& output_mask) {
    output_mask = Mat::zeros(height, width, CV_8UC1);
    
    int min_x = width, max_x = 0;
    int min_y = height, max_y = 0;
    int count = 0;

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

                if (is_red) {
                    output_mask.at<uint8_t>(y, x) = 255;
                    if (x < min_x) min_x = x;
                    if (x > max_x) max_x = x;
                    if (y < min_y) min_y = y;
                    if (y > max_y) max_y = y;
                    count++;
                }
            }
        }
    }
    
    if (count > 20) {
        Rect proposed_box(min_x, min_y, (max_x - min_x) + 1, (max_y - min_y) + 1);
        float fill_ratio = (float)count / (float)proposed_box.area();
        if (fill_ratio > 0.20) { 
            return proposed_box;
        }
    }
    return Rect(0,0,0,0);
}

int main() {
    VideoCapture cap;
    Mat current_frame, color_mask;

    if (!init_camera(cap)) return -1;

    // --- CREATE THE MENU SLIDERS ---
    namedWindow("Controls", WINDOW_AUTOSIZE);
    createTrackbar("Min Hue", "Controls", &min_hue, 360);
    createTrackbar("Max Hue", "Controls", &max_hue, 360);
    createTrackbar("Min Sat", "Controls", &min_sat, 255);
    createTrackbar("Max Sat", "Controls", &max_sat, 255);
    createTrackbar("Min Val", "Controls", &min_val, 255);
    createTrackbar("Max Val", "Controls", &max_val, 255);
    
    // NEW EXPOSURE SLIDER (Range 0 to 1000)
    createTrackbar("Exposure", "Controls", &exposure_val, 1000);

    Rect last_known_roi(0, 0, 0, 0);
    int lost_frames = 0;
    const int MAX_MEMORY = 20; 
    
    // Keep track of what exposure value the camera is currently set to
    int current_applied_exposure = -1;

    while (true) {
        // --- NEW: DYNAMIC EXPOSURE UPDATER ---
        // Only updates the camera hardware if you actually slide the trackbar!
        if (exposure_val != current_applied_exposure) {
            cap.set(CAP_PROP_EXPOSURE, exposure_val);
            current_applied_exposure = exposure_val;
        }

        if (!extract_frame(cap, current_frame)) break;

        Rect bounding_box = isolate_color_hsv(current_frame.data, 160, 120, color_mask);

        if (bounding_box.area() > 0) {
            last_known_roi = bounding_box;
            lost_frames = 0; 
        } else if (lost_frames < MAX_MEMORY && last_known_roi.area() > 0) {
            bounding_box = last_known_roi;
            lost_frames++;
        } else {
            last_known_roi = Rect(0,0,0,0);
        }

        if (bounding_box.area() > 0) {
            int padding = 12; 
            bounding_box.x = max(0, bounding_box.x - padding);
            bounding_box.y = max(0, bounding_box.y - padding);
            bounding_box.width = min(160 - bounding_box.x, bounding_box.width + (padding * 2));
            bounding_box.height = min(120 - bounding_box.y, bounding_box.height + (padding * 2));

            Mat roi_color = current_frame(bounding_box);
            Mat roi_gray, roi_edges;
            
            cvtColor(roi_color, roi_gray, COLOR_BGR2GRAY);
            Canny(roi_gray, roi_edges, 40, 120); 

            vector<Vec3f> circles;
            HoughCircles(roi_gray, circles, HOUGH_GRADIENT, 1, 
                         roi_gray.rows/4, 100, 35, 5, 45);

            if (!circles.empty()) {
                Point center(cvRound(circles[0][0]) + bounding_box.x, cvRound(circles[0][1]) + bounding_box.y);
                int radius = cvRound(circles[0][2]);
                
                circle(current_frame, center, 3, Scalar(0,255,0), -1, 8, 0);
                circle(current_frame, center, radius, Scalar(255,0,0), 2, 8, 0);

                last_known_roi = Rect(max(0, center.x - radius), max(0, center.y - radius), radius * 2, radius * 2);
                lost_frames = 0; 
            }

            Mat big_edges;
            resize(roi_edges, big_edges, Size(200, 200), 0, 0, INTER_NEAREST);
            imshow("ROI Edge Detection", big_edges);
            
            Scalar box_color = (lost_frames == 0) ? Scalar(0, 255, 255) : Scalar(0, 0, 255);
            rectangle(current_frame, bounding_box, box_color, 1);
            
        } else {
            if(cv::getWindowProperty("ROI Edge Detection", cv::WND_PROP_VISIBLE) >= 1) {
                destroyWindow("ROI Edge Detection");
            }
        }

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