//
// Created by yep on 29/04/2025.
//

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


// Half‐step sequence for stepper
const uint8_t half_step[8][4] = {
    {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,1,1,0},
    {0,0,1,0}, {0,0,1,1}, {0,0,0,1}, {1,0,0,1}
};
// Clear queue
void flush_events() {
    event_t junk;
    while (queue_try_remove(&events, &junk)) {}
}

// Calibration: measure full revolution
void calibrate() {
    event_t ev;
    flush_events(); // Clear any existing events

    printf("Looking for first edge...\n");
    //now we start looking for the first edge so we can start counting the steps
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

    // we've found the first edge, now count steps for one full revolution
    int steps_count = 0;
    bool edge_detected = false;

    // Move stepper until we hit another edge
    while (!edge_detected) {
        move_stepper(1);
        steps_count++;

        if (queue_try_remove(&events, &ev)) {
            if (ev.type == EV_OPTO) {
                edge_detected = true;
            }
        }

        // Safety check to prevent infinite loop
        if (steps_count > 10000) {
            printf("Calibration failed: too many steps without detecting edge.\n");
            calibrated = false;
            return;
        }
    }

    printf("Run 1: %d steps.\n", steps_count);

    // Store the step count and calculate steps per compartment
    steps_per_rotation = steps_count;
    steps_per_compartment = (steps_per_rotation / COMPARTMENTS);
    move_stepper(COMPARTMENT_OFFSET);

    // Make sure steps_per_compartment is not zero
    if (steps_per_compartment <= 0) {
        printf("Calibration failed: invalid compartment calculation.\n");
        calibrated = false;
        return;
    }

    calibrated = true;
    printf("Calibrated.\n");
    // lorawan_send_text(lorawan_connected, "Calibrated.");
}


// Stepper helpers
void move_stepper(int steps) {
    for (int i = 0; i < steps; i++) {
        current_step = (current_step + 1) % COMPARTMENTS;
        run_motor(current_step);
        sleep_ms(1);
    }
}
void run_motor(int step) {
    gpio_put(IN1, half_step[step][0]);
    gpio_put(IN2, half_step[step][1]);
    gpio_put(IN3, half_step[step][2]);
    gpio_put(IN4, half_step[step][3]);
}
