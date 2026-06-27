//not working
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <fcntl.h>    // File control definitions (O_RDWR)
#include <termios.h>  // POSIX terminal control definitions
#include <unistd.h>   // UNIX standard function definitions (read, close)

using namespace cv;
using namespace std;

int main() {
    // --- CONFIGURATION ---
    // Change this to your ESP32-CAM port. On Linux, it's usually /dev/ttyUSB0 or /dev/ttyACM0
    string portName = "/dev/ttyUSB0"; 
    
    // 1. Open the serial port
    int serial_port = open(portName.c_str(), O_RDWR);
    if (serial_port < 0) {
        cerr << "Error: Could not open serial port " << portName << "\n";
        cerr << "Tip: You may need to run with 'sudo' or add your user to the 'dialout' group." << endl;
        return 1;
    }

    // 2. Configure the serial port for 115200 baud, 8N1 (Raw Mode)
    struct termios tty;
    if (tcgetattr(serial_port, &tty) != 0) {
        cerr << "Error: tcgetattr failed" << endl;
        return 1;
    }
// Explicitly set input and output baud rates
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~PARENB;        // No parity bit
    tty.c_cflag &= ~CSTOPB;        // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 data bits
    tty.c_cflag &= ~CRTSCTS;       // Disable hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines

    // CRITICAL FOR BINARY DATA (JPEGs): Turn off ALL translation/processing
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    // Set non-blocking read behavior (Read whatever is available instantly)
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0; // 0 means return immediately with whatever bytes exist

    // --- CLEAR GARBAGE BUFFER BEFORE STARTING ---
    tcflush(serial_port, TCIOFLUSH);
    cout << "Buffer flushed. Listening for active stream..." << endl;

    string buffer = "";
    char read_buf[4096]; // Expanded buffer size
    
    const string START_MARKER = "###START###";
    const string END_MARKER = "###END###";

    // 3. The Stream Loop
    while (true) {
        // Read incoming bytes
        int num_bytes = read(serial_port, &read_buf, sizeof(read_buf));
        
        if (num_bytes > 0) {
            cout << "Received " << num_bytes << " bytes..." << endl;
            
            // Append new bytes to our master buffer
            buffer.append(read_buf, num_bytes);
            
            // Keep processing as long as there are full frames in the buffer
            size_t start_idx = buffer.find(START_MARKER);
            size_t end_idx = buffer.find(END_MARKER);
            
            while (start_idx != string::npos && end_idx != string::npos && end_idx > start_idx) {
                
                // Extract only the JPEG payload
                size_t jpeg_start = start_idx + START_MARKER.length();
                size_t jpeg_length = end_idx - jpeg_start;
                string jpeg_data = buffer.substr(jpeg_start, jpeg_length);
                
                // Erase the processed frame from the buffer
                buffer.erase(0, end_idx + END_MARKER.length());
                
                // Convert the raw bytes to an OpenCV Mat
                vector<uchar> img_vec(jpeg_data.begin(), jpeg_data.end());
                Mat frame = imdecode(img_vec, IMREAD_COLOR);
                
                // If the JPEG decoded successfully, show it!
                if (!frame.empty()) {
                    imshow("ESP32-CAM Wired Stream", frame);
                } else {
                    cerr << "Warning: Corrupt JPEG frame skipped." << endl;
                }

                // Check if another complete frame is already waiting in the buffer
                start_idx = buffer.find(START_MARKER);
                end_idx = buffer.find(END_MARKER);
            }

            // Safety check: Prevent memory leak if buffer grows out of control with junk
            if (buffer.length() > 50000) {
                cerr << "Buffer overflow protection triggered! Clearing buffer." << endl;
                buffer.clear();
            }
        }
        
        // Wait 1ms and check if 'q' was pressed to exit
        if (waitKey(1) == 'q') {
            break;
        }
    }

    close(serial_port);
    destroyAllWindows();
    return 0;
}