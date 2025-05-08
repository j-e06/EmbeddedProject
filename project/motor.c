#include "motor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pico/util/queue.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/time.h"
#include "lorawan.h"
#include "eeprom.h"
#include "config.h"


extern i2c_inst_t *eeprom_i2c;

// half‐step sequence for stepper
const uint8_t half_step[8][4] = {
    {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,1,1,0},
    {0,0,1,0}, {0,0,1,1}, {0,0,0,1}, {1,0,0,1}
};

// clear queue
void flush_events() {
    event_t junk;
    while (queue_try_remove(&events, &junk)) {}
}

// a really bad way of recalibrating the motor in the middle of a turn, but oh well
void recalibrate_motor() {
    event_t ev;

    flush_events(); // clear events in que

    printf("Returning to opto detect...\n");


    bool first_edge = false;

    while (!first_edge) {
        current_step = (current_step - 1);

        if (current_step < 0) current_step = COMPARTMENTS - 1;  // wrap around

        run_motor(current_step);

        sleep_ms(1);

        if (queue_try_remove(&events, &ev)) {
            if (ev.type == EV_OPTO) {
                // opto fork detected an edge
                first_edge = true;
                printf("Found opto edge...\n");
            }
        }
    }

    for (int i = 0; i < COMPARTMENT_OFFSET; i++) {
        current_step = (current_step - 1);

        if (current_step < 0) current_step = COMPARTMENTS - 1;

        run_motor(current_step);

        sleep_ms(1);
    }

    // go until we reached the previous pill dispensed

    move_stepper(pills_dispensed * steps_per_compartment);


}

void calibrate() {
    event_t ev;
    flush_events(); // clear que

    printf("Looking for first edge...\n");

    // look for first opto detect
    bool first_edge = false;
    while (!first_edge) {
        move_stepper(1);
        if (queue_try_remove(&events, &ev)) {
            if (ev.type == EV_OPTO) {
                // opto fork detected an edge
                first_edge = true;
                printf("Found first edge. Starting measurement...\n");
            }
        }
    }

    // now count steps for one full revolution
    int steps_count = 0;
    bool edge_detected = false;

    // move stepper until we hit opto detect again
    while (!edge_detected) {
        move_stepper(1);
        steps_count++;

        if (queue_try_remove(&events, &ev)) {
            if (ev.type == EV_OPTO) {
                edge_detected = true;
            }
        }

        // avoid infinite loop
        if (steps_count > 10000) {
            printf("Calibration failed: too many steps without detecting edge.\n");
            calibrated = false;
            return;
        }
    }

    // debug statement
    // printf("Run 1: %d steps.\n", steps_count);

    // Store the step count and calculate steps per compartment
    steps_per_rotation = steps_count;
    steps_per_compartment = (steps_per_rotation / COMPARTMENTS);
    // fully align the compartment
    move_stepper(COMPARTMENT_OFFSET);

    // this *shouldn't* be 0 in any situation but idk
    if (steps_per_compartment <= 0) {
        printf("Calibration failed: invalid compartment calculation.\n");
        calibrated = false;
        return;
    }

    calibrated = true;
    printf("Calibrated.\n");
}


void move_stepper(int steps) {
    if (steps == 0) {
        // no point going further than this if steps are 0
        return;
    }

    if (calibrated) {
        dispensing_in_progress = 1;
        if (eeprom_initialized) {
            save_state_to_eeprom(eeprom_i2c);
        }
    }

    for (int i = 0; i < steps; i++) {
        current_step = (current_step + 1) % COMPARTMENTS;
        run_motor(current_step);
        sleep_ms(1);
    }

    if (calibrated) {
        dispensing_in_progress = 0;
        if (eeprom_initialized) {
            save_state_to_eeprom(eeprom_i2c);
        }
    }
}

void run_motor(int step) {
    gpio_put(IN1, half_step[step][0]);
    gpio_put(IN2, half_step[step][1]);
    gpio_put(IN3, half_step[step][2]);
    gpio_put(IN4, half_step[step][3]);
}