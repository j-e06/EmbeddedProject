#ifndef MOTOR_H
#define MOTOR_H
//temp
void calibrate();
void move_stepper(int steps);
void run_motor(int step);
void flush_events();
void recalibrate_motor();
#endif //MOTOR_H