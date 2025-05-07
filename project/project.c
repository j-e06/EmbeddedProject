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
#include "motor.h"

i2c_inst_t  *eeprom_i2c = i2c0;

// Globals
int steps_per_rotation = 0;
int steps_per_compartment = 0;
int current_step = 0;
int dispensing_in_progress = 0;
bool calibrated = false;
bool dispense_pill_flag = false;
bool led_blink_flag = false;
bool lorawan_connected = false;
bool eeprom_initialized = false;
int pills_dispensed = 0;
system_state_t state = S_WAIT_CAL;

uint32_t last_led_toggle = 0;
uint32_t last_piezo_time = 0;
uint32_t first_delay_start = 0;

queue_t events;
struct repeating_timer timer;


// Prototypes
void init_all();
void init_button(uint gpio_pin);
void init_motor_pin(uint gpio_pin);
static void gpio_handler(uint gpio, uint32_t event_mask);
bool pill_timer_callback(struct repeating_timer *t);
void pill_dispenser();

void error_blink(uint led_pin);
bool check_button_press(uint pin);
bool check_long_press(uint pin, uint duration);


// Modify your main function to add EEPROM support
int main() {
    stdio_init_all();
    init_all();
    printf(INSTRUCTIONS);

    load_eeprom_state(eeprom_i2c, pill_timer_callback);

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // check for button presses regardless of state

        bool center_pressed = check_button_press(CENTER_BUTTON);
        bool left_pressed = check_button_press(LEFT_BUTTON);

        switch (state) {
            case S_WAIT_CAL:
                // blink LED
                if (now - last_led_toggle >= 200) {
                    gpio_put(CENTER_LED, !gpio_get(CENTER_LED));
                    last_led_toggle = now;
                }
                if (center_pressed) {
                    printf("Starting calibration...\n");
                    lorawan_send_text(lorawan_connected, "Starting calibration...");

                    calibrate();

                    if (calibrated) {
                        state = S_IDLE;
                        gpio_put(CENTER_LED, 1);
                        printf("Calibration done: %d steps/rev, %d steps/compartment\n",
                               steps_per_rotation, steps_per_compartment);
                        printf("IDLE: Press LEFT button to dispense.\n");
                        lorawan_send_text(lorawan_connected, "Calibration done!");

                        // Save the state after successful calibration
                        if (eeprom_initialized) {
                            save_state_to_eeprom(eeprom_i2c);
                        }
                    } else {
                        state = S_ERROR;
                        led_blink_flag = true;
                        printf("Calibration failed!\n");
                        lorawan_send_text(lorawan_connected, "Calibration failed!");

                    }
                }
                break;

            case S_IDLE:
                if (left_pressed) {
                    printf("Dispense sequence started.\n");
                    lorawan_send_text(lorawan_connected, "Starting pill dispenser");

                    pills_dispensed = 0;
                    first_delay_start = now;
                    state = S_FIRST_DELAY;
                    gpio_put(CENTER_LED, 1);  // led to show we're in delay mode

                    // save initial state when starting dispensing
                    if (eeprom_initialized) {
                        save_state_to_eeprom(eeprom_i2c);
                    }
                }
                break;

            case S_FIRST_DELAY:
                // wait for the first pill delay
                if (now - first_delay_start >= FIRST_PILL_DELAY) {
                    gpio_put(CENTER_LED, 0);
                    dispense_pill_flag = true;
                    state = S_DISPENSE;
                    // start repeating times
                    add_repeating_timer_ms(TIME_BETWEEN_PILLS, pill_timer_callback, NULL, &timer);
                }
                break;

            case S_DISPENSE:
                if (dispense_pill_flag) {

                    if (pills_dispensed >= MAX_PILLS) {
                        printf("All pills dispensed.\n");
                        lorawan_send_text(lorawan_connected, "All pills dispensed.");
                        cancel_repeating_timer(&timer);

                        state = S_WAIT_CAL;

                        calibrated = false;

                        if (eeprom_initialized) {
                            save_state_to_eeprom(eeprom_i2c);
                        }
                    }
                    else {
                        dispense_pill_flag = false;
                        pill_dispenser();

                        // save state after dispensing
                        if (eeprom_initialized) {
                            save_state_to_eeprom(eeprom_i2c);
                        }
                    }
                }
                sleep_ms(10);
                break;

            case S_ERROR:
                if (led_blink_flag) {
                    error_blink(CENTER_LED);
                    led_blink_flag = false;
                }

                if (check_long_press(CENTER_BUTTON, LONG_PRESS_DURATION)) {
                    printf("Resetting to calibration.\n");
                    lorawan_send_text(lorawan_connected, "Resetting to calibration.");

                    state = S_WAIT_CAL;
                    gpio_put(CENTER_LED, 0);

                    // Reset saved state when resetting calibration
                    if (eeprom_initialized) {
                        calibrated = false;
                        save_state_to_eeprom(eeprom_i2c);
                    }
                }
                sleep_ms(100);
                break;
        }
    }
}

// Timer callback
bool pill_timer_callback(struct repeating_timer *t) {
    if (pills_dispensed >= MAX_PILLS) {
        cancel_repeating_timer(&timer);
        state = S_WAIT_CAL;
        calibrated = false;

        // Save final state after finishing all pills
        if (eeprom_initialized) {
            save_state_to_eeprom(eeprom_i2c);
        }

        return false;
    }
    dispense_pill_flag = true;
    return true;
}

// Dispense one pill
void pill_dispenser() {
    printf("Dispensing pill %d...\n", pills_dispensed+1);

    lorawan_send_text(lorawan_connected, "Dispensing pill...\n");

    flush_events();
    last_piezo_time = 0; // reset piezo debounce timer

    // Move one compartment
    if (steps_per_compartment > 0) {
        move_stepper(steps_per_compartment);
    } else {
        printf("Error: Invalid compartment step count.\n");
        error_blink(CENTER_LED);
        state = S_ERROR;
        return;
    }

    // increment the pill counter
    pills_dispensed++;

    // save state to EEPROM after incrementing counter
    if (eeprom_initialized) {
        save_state_to_eeprom(eeprom_i2c);
    }



    //sleep_ms(500); // wait for pill to drop

    bool pill_detected = false;
    uint32_t detection_start = to_ms_since_boot(get_absolute_time());
    uint32_t detection_timeout = 1000; // 1 second wait for pill detection

    // check for pill detection
    event_t ev;
    while (to_ms_since_boot(get_absolute_time()) - detection_start < detection_timeout) {
        if (queue_try_remove(&events, &ev)) {
            if (ev.type == EV_PIEZO) {
                printf("Pill detected\n");
                lorawan_send_text(lorawan_connected, "Pill detected");
                pill_detected = true;
                break;
            }
        }
        sleep_ms(10);
    }

    if (!pill_detected) {
        printf("Pill NOT detected!\n");
        lorawan_send_text(lorawan_connected, "Pill NOT detected!");
        error_blink(CENTER_LED);
    }

    if (pills_dispensed >= MAX_PILLS) {
        printf("All pills dispensed\n");
        lorawan_send_text(lorawan_connected, "All pills dispensed.");
        cancel_repeating_timer(&timer);
        state = S_WAIT_CAL;
        calibrated = false;
    }
}


// Error blink
void error_blink(uint led_pin) {
    for (int i = 0; i < ERROR_BLINK_COUNT; i++) {
        gpio_put(led_pin, 1);
        sleep_ms(100);
        gpio_put(led_pin, 0);
        sleep_ms(100);
    }
}

// Button check
bool check_button_press(uint pin) {
    if (!gpio_get(pin)) {
        sleep_ms(20);
        if (!gpio_get(pin)) {
            while (!gpio_get(pin)) sleep_ms(10);
            return true;
        }
    }
    return false;
}
bool check_long_press(uint pin, uint duration) {
    if (!gpio_get(pin)) {
        uint32_t start = to_ms_since_boot(get_absolute_time());
        while (!gpio_get(pin)) {
            if (to_ms_since_boot(get_absolute_time()) - start >= duration) {
                while (!gpio_get(pin)) sleep_ms(10);
                return true;
            }
            sleep_ms(10);
        }
    }
    return false;
}



void init_button(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}
void init_motor_pin(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
}

// IRQ handler with debounce for piezo sensor
static void gpio_handler(uint gpio, uint32_t mask) {
    if (mask & GPIO_IRQ_EDGE_FALL) {
        uint32_t current_time = to_ms_since_boot(get_absolute_time());

        if (gpio == OPTO_FORK) {
            event_t ev = {EV_OPTO, current_time};
            queue_try_add(&events, &ev);
            // printf("Opto edge\n");
        }
        else if (gpio == PIEZO_GPIO) {
            // Apply debounce logic for piezo sensor
            if (last_piezo_time == 0 || current_time - last_piezo_time >= PIEZO_DEBOUNCE_MS) {
                event_t ev = {EV_PIEZO, current_time};
                queue_try_add(&events, &ev);
                last_piezo_time = current_time;
                // printf("Piezo hit\n");
            }
        }
    }
}

// GPIO init and IRQ setup
void init_all() {
    queue_init(&events, sizeof(event_t), 16);

    // Buttons
    init_button(LEFT_BUTTON);
    init_button(CENTER_BUTTON);
    init_button(RIGHT_BUTTON);

    // LEDs
    gpio_init(LEFT_LED);   gpio_set_dir(LEFT_LED, GPIO_OUT);
    gpio_init(CENTER_LED); gpio_set_dir(CENTER_LED, GPIO_OUT);
    gpio_init(RIGHT_LED);  gpio_set_dir(RIGHT_LED, GPIO_OUT);

    // Motor
    init_motor_pin(IN1);
    init_motor_pin(IN2);
    init_motor_pin(IN3);
    init_motor_pin(IN4);

    // Sensors
    gpio_set_irq_callback(gpio_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);
    init_button(OPTO_FORK);
    gpio_set_irq_enabled(OPTO_FORK, GPIO_IRQ_EDGE_FALL, true);
    init_button(PIEZO_GPIO);
    gpio_set_irq_enabled(PIEZO_GPIO, GPIO_IRQ_EDGE_FALL, true);

    // Initialize EEPROM via I2C
    eeprom_initialized = init_eeprom(eeprom_i2c);

    // Init LoraWan
    init_lorawan();

    lorawan_connected = lorawan_try_connect();
    lorawan_send_text(lorawan_connected, "Device has booted.");

    // Initialize last_piezo_time
    last_piezo_time = 0;
}