#include "motors.h"

// Left motor pins
const int ENA = 25;
const int IN1 = 26;
const int IN2 = 27;

// Right motor pins
const int ENB = 13;
const int IN3 = 14;
const int IN4 = 12;

const int pwmChannelA  = 0;
const int pwmChannelB  = 1;
const int pwmFreq      = 15000;
const int pwmResolution = 8; // 0-255

void motors_init() {
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);

    ledcSetup(pwmChannelA, pwmFreq, pwmResolution);
    ledcAttachPin(ENA, pwmChannelA);

    ledcSetup(pwmChannelB, pwmFreq, pwmResolution);
    ledcAttachPin(ENB, pwmChannelB);

    motor_stop();
}

void motor_stop() {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);
    ledcWrite(pwmChannelA, 0);
    ledcWrite(pwmChannelB, 0);
}

// Left  motor:  IN1=LOW,  IN2=HIGH  → forward
// Right motor:  IN3=LOW,  IN4=HIGH  → forward
void front() {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
    digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
    ledcWrite(pwmChannelA, 255);
    ledcWrite(pwmChannelB, 255);
}

// Left  motor:  IN1=HIGH, IN2=LOW   → reverse
// Right motor:  IN3=HIGH, IN4=LOW   → reverse
void back() {
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
    digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
    ledcWrite(pwmChannelA, 255);
    ledcWrite(pwmChannelB, 255);
}

// Spin left:  left motor reverse, right motor forward
void left() {
    digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
    ledcWrite(pwmChannelA, 255);
    ledcWrite(pwmChannelB, 255);
}

// Spin right: left motor forward, right motor reverse
void right() {
    digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
    digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
    ledcWrite(pwmChannelA, 255);
    ledcWrite(pwmChannelB, 255);
}