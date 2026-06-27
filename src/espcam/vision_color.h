#ifndef VISION_COLOR_H
#define VISION_COLOR_H

#include <Arduino.h>

// Expose these color functions to main.cpp
void vision_color_loop();
void applyColorPreset(int color_id);

#endif