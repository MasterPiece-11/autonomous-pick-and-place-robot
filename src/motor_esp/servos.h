#ifndef SERVOS_H
#define SERVOS_H

#include <Arduino.h>

void servos_init();
void arm_up();
void arm_down();
void grip();
void ungrip();
void execute_grab_sequence();

#endif