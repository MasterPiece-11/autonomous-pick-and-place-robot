#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>

using namespace cv;
using namespace std;
using namespace std::chrono;

// HSV tuning
int min_h = 062, max_h = 005, min_s = 118, min_v = 68;

enum TrackingState { FULL_SCAN, BORDER_WATCH, LOCKED, LOST_BUT_RETAINING };

bool is_red(Vec3b p) {
    Mat bgr(1, 1, CV_8UC3, p);
    Mat hsv;
    cvtColor(bgr, hsv, COLOR_BGR2HSV);
    Vec3b h = hsv.at<Vec3b>(0, 0);
    int hue = h[0] * 2;
    return (h[1] >= min_s && h[2] >= min_v) && ((min_h > max_h) ? (hue >= min_h || hue <= max_h) : (hue >= min_h && hue <= max_h));
}

int main() {
    VideoCapture cap(0);
    if (!cap.isOpened()) return -1;

    TrackingState state = FULL_SCAN;
    int tx = 80, ty = 60, win = 40;
    
    // Memory Settings
    int lost_frame_counter = 0;
    const int max_memory_frames = 15; // 15 frames of leeway

    auto last_full_scan = steady_clock::now();

    while (waitKey(1) != 'q') {
        Mat frame, small;
        cap >> frame; if (frame.empty()) break;
        flip(frame, frame, 1);
        resize(frame, small, Size(160, 120));

        long sum_x = 0, sum_y = 0, count = 0;
        string cmd = "Stop";
        string state_text = "";
        Scalar box_color = Scalar(0, 255, 0); // Green

        // Heartbeat timer check
        if (state == BORDER_WATCH && duration_cast<seconds>(steady_clock::now() - last_full_scan).count() > 2) {
            state = FULL_SCAN;
        }

        // --- MATH MODES ---
        if (state == FULL_SCAN) {
            state_text = "FULL SCANNING";
            last_full_scan = steady_clock::now();
            for (int y = 0; y < 120; y++) {
                for (int x = 0; x < 160; x++) {
                    if (is_red(small.at<Vec3b>(y, x))) { sum_x += x; sum_y += y; count++; }
                }
            }
            if (count <= 10) state = BORDER_WATCH;
        } 
        else if (state == BORDER_WATCH) {
            state_text = "WATCHING BORDERS";
            for (int y : {0,1,2,117,118,119}) for (int x=0; x<160; x++) if (is_red(small.at<Vec3b>(y,x))) { sum_x+=x; sum_y+=y; count++; }
            for (int x : {0,1,2,157,158,159}) for (int y=0; y<120; y++) if (is_red(small.at<Vec3b>(y,x))) { sum_x+=x; sum_y+=y; count++; }
        }
        else if (state == LOCKED || state == LOST_BUT_RETAINING) {
            int x1 = max(0, tx-win/2), x2 = min(160, tx+win/2), y1 = max(0, ty-win/2), y2 = min(120, ty+win/2);
            for (int y=y1; y<y2; y++) {
                for (int x=x1; x<x2; x++) {
                    if (is_red(small.at<Vec3b>(y,x))) { sum_x+=x; sum_y+=y; count++; }
                }
            }
            
            if (state == LOST_BUT_RETAINING) {
                state_text = "LOST: MEMORY (" + to_string(max_memory_frames - lost_frame_counter) + ")";
                box_color = Scalar(0, 255, 255); // Yellow
            } else {
                state_text = "LOCKED";
            }
            rectangle(frame, Point(x1*4, y1*4), Point(x2*4, y2*4), box_color, 2);
        }

        // --- COMMAND AND TARGET PROCESSOR ---
        if (count > 10) {
            tx = sum_x / count; ty = sum_y / count;
            state = LOCKED;
            lost_frame_counter = 0; // Reset timer

            // Determine Command
            if (tx < 70) cmd = "L";
            else if (tx > 90) cmd = "R";
            else cmd = "Stop";

            // Draw center dot
            circle(frame, Point(tx*4, ty*4), 10, Scalar(255, 0, 0), -1);
            
            // Display metrics: X, Y, and Area (count)
            string metrics = "X: " + to_string(tx) + "  Y: " + to_string(ty) + "  Area: " + to_string(count);
            putText(frame, metrics, Point(20, 80), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);
        } 
        else {
            if (state == LOCKED || state == LOST_BUT_RETAINING) {
                state = LOST_BUT_RETAINING;
                lost_frame_counter++;

                // Output estimating parameters based on last known state
                if (tx < 70) cmd = "L (ESTIMATING)";
                else if (tx > 90) cmd = "R (ESTIMATING)";
                else cmd = "Stop (ESTIMATING)";

                string est_metrics = "LAST X: " + to_string(tx) + "  Y: " + to_string(ty) + "  Area: 0";
                putText(frame, est_metrics, Point(20, 80), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 255), 2);

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
        imshow("Robot Steering Controller", frame);
    }
    return 0;
}