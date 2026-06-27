#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <Arduino.h>

void dashboard_init();
void dashboard_handle();
void add_log(String msg);
bool is_manual_override();

#endif