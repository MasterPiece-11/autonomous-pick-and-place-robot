#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <cmath>

// --- OFFICIAL APRILTAG HEADERS ---
extern "C" {
#include <apriltag.h>
#include <tagStandard52h13.h> // The specific library for your professor's tag!
}

using namespace cv;
using namespace std;

int main() {
    VideoCapture cap(0, CAP_V4L2);
    if (!cap.isOpened()) {
        cerr << "Error: Cannot open laptop webcam!" << endl;
        return -1;
    }

    // 1. Initialize the Official AprilTag Detector Engine
    apriltag_family_t *tf = tagStandard52h13_create();
    apriltag_detector_t *td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);

    cout << "Official AprilTag Robotics Engine Running!" << endl;
    cout << "-> Scanning specifically for tagStandard52h13..." << endl;
    cout << "Press 'q' to quit." << endl;

    Mat frame, gray;
    while (true) {
        cap >> frame;
        if (frame.empty()) break;

        // Remember to flip because webcams act like mirrors!
        flip(frame, frame, 1);
        resize(frame, frame, Size(160, 120));

        // AprilTag strictly requires Grayscale matrices
        cvtColor(frame, gray, COLOR_BGR2GRAY);

        // 2. Wrap the OpenCV matrix in the official AprilTag memory struct
        image_u8_t img_header = {
            .width = gray.cols,
            .height = gray.rows,
            .stride = gray.cols,
            .buf = gray.data
        };

        // 3. Run the advanced detection pipeline
        zarray_t *detections = apriltag_detector_detect(td, &img_header);

        bool tag_found = false;

        // 4. Loop through any detected 52h13 tags
        for (int i = 0; i < zarray_size(detections); i++) {
            apriltag_detection_t *det;
            zarray_get(detections, i, &det);
            
            tag_found = true;

            // Grab the exactly calculated corners
            Point2f p0(det->p[0][0], det->p[0][1]);
            Point2f p1(det->p[1][0], det->p[1][1]);
            Point2f p2(det->p[2][0], det->p[2][1]);
            Point2f p3(det->p[3][0], det->p[3][1]);

            // --- NAVIGATION SQUISH MATH ---
            float left_edge_height = abs(p0.y - p3.y);
            float right_edge_height = abs(p1.y - p2.y);
            
            // det->c[0] is the exact sub-pixel X center calculated by the engine
            float center_offset = det->c[0] - (160.0f / 2.0f); 

            // Visual Rendering
            line(frame, p0, p1, Scalar(0, 255, 0), 2);
            line(frame, p1, p2, Scalar(0, 255, 0), 2);
            line(frame, p2, p3, Scalar(0, 255, 0), 2);
            line(frame, p3, p0, Scalar(0, 255, 0), 2);
            
            string id_text = "ID: " + to_string(det->id);
            putText(frame, id_text, p0, FONT_HERSHEY_SIMPLEX, 0.4, Scalar(0, 0, 255), 1);

            cout << "\r[DETECTED] ID: " << det->id << " | ";
            if (abs(left_edge_height - right_edge_height) < 1.5f) {
                cout << "Status: SQUARE | ";
            } else if (left_edge_height < right_edge_height) {
                cout << "Status: SQUISHED LEFT | ";
            } else {
                cout << "Status: SQUISHED RIGHT | ";
            }

            if (abs(center_offset) < 10.0f) {
                cout << "Position: CENTERED   " << flush;
            } else if (center_offset > 0) {
                cout << "Position: MOVE RIGHT  " << flush;
            } else {
                cout << "Position: MOVE LEFT   " << flush;
            }
        }

        // Clean up the detection memory array to prevent memory leaks
        apriltag_detections_destroy(detections);

        if (!tag_found) {
            cout << "\r[SEARCHING] Waiting for 52h13 tag...                     " << flush;
        }

        Mat big_frame;
        resize(frame, big_frame, Size(640, 480), 0, 0, INTER_NEAREST);
        imshow("Robot Phase 2 Simulation", big_frame);

        if (waitKey(30) == 'q') break;
    }

    // Safely shutdown the detectors when we quit
    apriltag_detector_destroy(td);
    tagStandard52h13_destroy(tf);
    
    cap.release();
    destroyAllWindows();
    return 0;
}