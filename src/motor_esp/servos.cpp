#include "servos.h"
#include "dashboard.h"
#include <ESP32Servo.h>

extern bool is_grabbing;

#define ARM_SERVO_PIN      32
#define GRIPPER_SERVO_PIN  33

Servo armServo;
Servo gripperServo;

const int ARM_UP_ANGLE         = 180;
const int ARM_DOWN_ANGLE       = 70;
const int GRIPPER_CLOSED_ANGLE = 70;
const int GRIPPER_OPEN_ANGLE   = 25;

void servos_init() {
    armServo.attach(ARM_SERVO_PIN);
    gripperServo.attach(GRIPPER_SERVO_PIN);
    armServo.write(ARM_UP_ANGLE);
    gripperServo.write(GRIPPER_CLOSED_ANGLE);
}

void arm_up() {
    armServo.write(ARM_UP_ANGLE);
    add_log("Servo: Arm raised.");
}

void arm_down() {
    armServo.write(ARM_DOWN_ANGLE);
    add_log("Servo: Arm lowered.");
}

void grip() {
    gripperServo.write(GRIPPER_OPEN_ANGLE);
    add_log("Servo: Gripper opened.");
}

void ungrip() {
    gripperServo.write(GRIPPER_CLOSED_ANGLE);
    add_log("Servo: Gripper closed.");
}

void execute_grab_sequence() {
    is_grabbing = true;
    add_log("WARNING: Target in extraction zone. Commencing recovery!");

    ungrip();       delay(600);
    arm_down();     delay(800);
    grip();         delay(600);
    arm_up();       delay(800);

    add_log("SUCCESS: Recovery complete.");
    is_grabbing = false;
}