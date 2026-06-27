#ifndef MOTORS_H
#define MOTORS_H

#include <Arduino.h>

void motors_init();
void front();
void back();
void left();
void right();
void motor_stop();

#endif