#ifndef CAMERA_HAL_H
#define CAMERA_HAL_H

#include "esp_camera.h"

// Expose the camera initialization function to main.cpp
bool init_camera();

#endif