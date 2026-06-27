#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <cmath>

using namespace cv;
using namespace std;
using namespace std::chrono;

// --- GLOBAL SLIDER VARIABLES ---
int min_h = 350; // 0 to 360
int max_h = 2;   // 0 to 360
int min_s = 167; // 0 to 255
int min_v = 35;  // 0 to 255
int exposure_val = 500; 
int min_blob_area = 15; // NEW: Minimum pixels required to track

enum TrackingState { FULL_SCAN, BORDER_WATCH, LOCKED, LOST_BUT_RETAINING };

int main() {
    VideoCapture cap(0, CAP_V4L2);
    if (!cap.isOpened()) return -1;
    
    cap.set(CAP_PROP_AUTO_EXPOSURE, 1);
    int current_applied_exposure = -1;

    namedWindow("Controls", WINDOW_AUTOSIZE);
    createTrackbar("Min Hue", "Controls", &min_h, 360);
    createTrackbar("Max Hue", "Controls", &max_h, 360);
    createTrackbar("Min Sat", "Controls", &min_s, 255); 
    createTrackbar("Min Val", "Controls", &min_v, 255);
    createTrackbar("Exposure", "Controls", &exposure_val, 500);
    createTrackbar("Min Area", "Controls", &min_blob_area, 500); // NEW SLIDER

    namedWindow("Robot Steering Controller", WINDOW_AUTOSIZE);
    namedWindow("Full HSV Mask", WINDOW_AUTOSIZE);
    namedWindow("Cleaned Tracked Object", WINDOW_AUTOSIZE);
    namedWindow("ROI Edges", WINDOW_AUTOSIZE); 

    TrackingState state = FULL_SCAN;
    int tx = 80, ty = 60, win = 50; 
    
    int lost_frame_counter = 0;
    const int max_memory_frames = 15; 

    auto last_full_scan = steady_clock::now();

    while (waitKey(1) != 'q') {
        
        if (exposure_val != current_applied_exposure) {
            cap.set(CAP_PROP_EXPOSURE, exposure_val);
            current_applied_exposure = exposure_val;
        }

        Mat frame, small, small_hsv;
        cap >> frame; if (frame.empty()) break;
        flip(frame, frame, 1);
        
        resize(frame, small, Size(160, 120));
        cvtColor(small, small_hsv, COLOR_BGR2HSV);

        Mat full_mask = Mat::zeros(120, 160, CV_8UC1);
        Mat cleaned_mask = Mat::zeros(120, 160, CV_8UC1);

        for (int y = 0; y < 120; y++) {
            for (int x = 0; x < 160; x++) {
                Vec3b hsv_pixel = small_hsv.at<Vec3b>(y, x);
                int hue = hsv_pixel[0] * 2; 
                int sat = hsv_pixel[1];
                int val = hsv_pixel[2];

                bool is_color = false;
                if (sat >= min_s && val >= min_v) {
                    if (min_h > max_h) {
                        if (hue >= min_h || hue <= max_h) is_color = true;
                    } else {
                        if (hue >= min_h && hue <= max_h) is_color = true;
                    }
                }
                if (is_color) full_mask.at<uchar>(y, x) = 255;
            }
        }

        long sum_x = 0, sum_y = 0, count = 0;
        string cmd = "Stop";
        string state_text = "";
        Scalar box_color = Scalar(0, 255, 0); 
        float distance_cm = 0.0;

        if (state == BORDER_WATCH && duration_cast<seconds>(steady_clock::now() - last_full_scan).count() > 2) {
            state = FULL_SCAN;
        }

        if (state == FULL_SCAN) {
            state_text = "FULL SCANNING";
            last_full_scan = steady_clock::now();
            for (int y = 0; y < 120; y++) {
                for (int x = 0; x < 160; x++) {
                    if (full_mask.at<uchar>(y, x) == 255) { sum_x += x; sum_y += y; count++; }
                }
            }
            if (count <= min_blob_area) { // UPDATED TO USE SLIDER
                state = BORDER_WATCH;
                // Leave cleaned_mask as zeros because it's too small
            } else {
                tx = sum_x / count; 
                ty = sum_y / count;
                cleaned_mask = full_mask.clone(); 
            }
        } 
        else if (state == BORDER_WATCH) {
            state_text = "WATCHING BORDERS";
            for (int y : {0,1,2,117,118,119}) for (int x=0; x<160; x++) if (full_mask.at<uchar>(y, x) == 255) { sum_x+=x; sum_y+=y; count++; }
            for (int x : {0,1,2,157,158,159}) for (int y=0; y<120; y++) if (full_mask.at<uchar>(y, x) == 255) { sum_x+=x; sum_y+=y; count++; }
            
            if (count > min_blob_area) { // UPDATED TO USE SLIDER
                tx = sum_x / count; 
                ty = sum_y / count;
                cleaned_mask = full_mask.clone(); 
            }
            // If count <= min_blob_area, cleaned_mask naturally stays zero
        }
        else if (state == LOCKED || state == LOST_BUT_RETAINING) {
            int x1 = max(0, tx-win/2), x2 = min(160, tx+win/2), y1 = max(0, ty-win/2), y2 = min(120, ty+win/2);
            
            for (int y=y1; y<y2; y++) {
                for (int x=x1; x<x2; x++) {
                    if (full_mask.at<uchar>(y, x) == 255) { 
                        sum_x+=x; sum_y+=y; count++; 
                        cleaned_mask.at<uchar>(y, x) = 255;
                    }
                }
            }

            if (count > min_blob_area) { // UPDATED TO USE SLIDER
                Rect roi_rect(x1, y1, x2-x1, y2-y1);
                Mat roi_gray, roi_edges;
                
                Mat masked_roi;
                small(roi_rect).copyTo(masked_roi, cleaned_mask(roi_rect));
                cvtColor(masked_roi, roi_gray, COLOR_BGR2GRAY);
                
                vector<Vec3f> circles;
                HoughCircles(roi_gray, circles, HOUGH_GRADIENT, 1, roi_gray.rows/4, 100, 20, 5, 45);

                if (!circles.empty()) {
                    int cx = cvRound(circles[0][0]) + x1;
                    int cy = cvRound(circles[0][1]) + y1;
                    int r = cvRound(circles[0][2]);

                    tx = cx; 
                    ty = cy;
                    
                    float stable_area = CV_PI * r * r;
                    distance_cm = sqrt(512000.0 / stable_area);
                    
                    circle(frame, Point(cx*4, cy*4), r*4, Scalar(255, 0, 0), 2);
                } else {
                    tx = sum_x / count; ty = sum_y / count;
                    distance_cm = sqrt(512000.0 / count);
                }

                Canny(roi_gray, roi_edges, 40, 120);
                Mat big_edges;
                resize(roi_edges, big_edges, Size(300, 300), 0, 0, INTER_NEAREST);
                imshow("ROI Edges", big_edges);
            } else {
                // If it doesn't meet the threshold, wipe this box from the cleaned tab so it disappears
                cleaned_mask(Rect(x1, y1, x2-x1, y2-y1)).setTo(0);
            }
            
            if (state == LOST_BUT_RETAINING) {
                state_text = "LOST: MEMORY (" + to_string(max_memory_frames - lost_frame_counter) + ")";
                box_color = Scalar(0, 255, 255); 
            } else {
                state_text = "LOCKED";
            }
            rectangle(frame, Point(x1*4, y1*4), Point(x2*4, y2*4), box_color, 2);
        }

        if (count > min_blob_area) { // UPDATED TO USE SLIDER
            state = LOCKED;
            lost_frame_counter = 0; 

            if (tx < 70) cmd = "L";
            else if (tx > 90) cmd = "R";
            else cmd = "Stop";

            circle(frame, Point(tx*4, ty*4), 10, Scalar(255, 0, 0), -1);
            
            string metrics = "Dist: " + to_string((int)distance_cm) + " cm";
            putText(frame, metrics, Point(20, 80), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);
        } 
        else {
            if (state == LOCKED || state == LOST_BUT_RETAINING) {
                state = LOST_BUT_RETAINING;
                lost_frame_counter++;

                if (tx < 70) cmd = "L (EST)";
                else if (tx > 90) cmd = "R (EST)";
                else cmd = "Stop (EST)";

                putText(frame, "Dist: LOST", Point(20, 80), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 255), 2);

                if (lost_frame_counter >= max_memory_frames) {
                    state = FULL_SCAN;
                    lost_frame_counter = 0;
                    cmd = "Stop";
                }
            } else {
                cmd = "Stop";
            }
        }

        putText(frame, "CMD: " + cmd, Point(20, 40), FONT_HERSHEY_SIMPLEX, 1.2, box_color, 3);
        
        Mat large_full_mask, large_cleaned_mask;
        resize(full_mask, large_full_mask, Size(640, 480), 0, 0, INTER_NEAREST);
        resize(cleaned_mask, large_cleaned_mask, Size(640, 480), 0, 0, INTER_NEAREST);

        imshow("Robot Steering Controller", frame);
        imshow("Full HSV Mask", large_full_mask);
        imshow("Cleaned Tracked Object", large_cleaned_mask);
    }
    
    cap.release();
    destroyAllWindows();
    return 0;
}