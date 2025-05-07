//eeprom.c

#include <stdio.h>
#include <string.h>
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "config.h"
#include "eeprom.h"

#include "lorawan.h"
#include "motor.h"
void reset_calibration_values(i2c_inst_t *i2c) {
    calibrated = false;
    steps_per_rotation = 0;
    steps_per_compartment = 0;
    pills_dispensed = 0;
    dispensing_in_progress = 0;

    if (eeprom_initialized) {
        save_state_to_eeprom(i2c);
    }
}

// reset pill count but keep calibration
void reset_pill_count(i2c_inst_t *i2c) {
    pills_dispensed = 0;

    if (eeprom_initialized) {
        save_state_to_eeprom(i2c);
    }
}

void load_eeprom_state(i2c_inst_t *eeprom_i2c, repeating_timer_callback_t pill_timer_callback) {
    // load state from EEPROM if available
    if (eeprom_initialized && load_state_from_eeprom(eeprom_i2c)) {
        if (calibrated && steps_per_rotation > 0 && steps_per_compartment > 0) {
            printf("Restored calibration from EEPROM\n");
            lorawan_send_text(lorawan_connected, "Restored calibration from EEPROM");

            if (pills_dispensed > 0 && pills_dispensed < MAX_PILLS || dispensing_in_progress == 1) {
                // recover from interrupted dispensing cycle
                printf("Program interrupted, recovering...\n");
                lorawan_send_text(lorawan_connected, "Recovering from interruption");
                // printf("Resuming from pill %d of %d\n", pills_dispensed + 1, MAX_PILLS);


                // Set up timer to continue dispensing

                // update state to start dispensing next pill
                state = S_DISPENSE;
                dispense_pill_flag = true; // immediately start dispensing next pill

                gpio_put(CENTER_LED, 1);
                dispensing_in_progress = 0;
                add_repeating_timer_ms(TIME_BETWEEN_PILLS, pill_timer_callback, NULL, &timer);

                recalibrate_motor();


                // if (eeprom_initialized) {
                //     save_state_to_eeprom(eeprom_i2c);
                // }

            } else {
                // EEPROM had calibration, but no interrupted dispense
                state = S_IDLE;
                gpio_put(CENTER_LED, 1);  // show calibrated status
                printf("Ready to dispense. Press LEFT button.\n");
            }
        } else {
            // invalid calibration data in EEPROM
            printf("No valid calibration in EEPROM\n");
        }
    }
}

bool init_eeprom(i2c_inst_t *i2c) {
    printf("Initializing EEPROM...\n");

    // Set up the I2C pins
    gpio_set_function(EEPROM_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(EEPROM_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(EEPROM_SDA_PIN);
    gpio_pull_up(EEPROM_SCL_PIN);

    i2c_init(i2c, 100000);  // 100 kHz, lower for reliability

    printf("I2C initialized\n");

    // check if EEPROM exists
    uint8_t addr_buf[1] = {0};  // start with address 0
    // bool eeprom_present = false;


    printf("Testing EEPROM at address 0x%02X...\n", EEPROM_ADDR);

    // write address 0
    int result = i2c_write_timeout_us(i2c, EEPROM_ADDR, addr_buf, 1, false, 100000);

    if (result == PICO_ERROR_GENERIC || result == PICO_ERROR_TIMEOUT) {
        printf("EEPROM not detected\n");
        return false;
    }

    // try to read one byte with timeout
    uint8_t test_byte = 0;
    result = i2c_read_timeout_us(i2c, EEPROM_ADDR, &test_byte, 1, false, 100000);

    if (result == 1) {
        printf("EEPROM detected successfully\n");
    } else {
        printf("EEPROM read failed with result: %d\n", result);
        return false;
    }

    // check for magic number to see if EEPROM has been initialized
    uint32_t magic = 0;
    bool read_result = eeprom_read_bytes(i2c, ADDR_MAGIC, (uint8_t*)&magic, sizeof(magic));

    if (!read_result) {
        printf("Failed to read magic number from EEPROM\n");
        return false;
    }

    printf("Read magic number: 0x%08X, Expected: 0x%08X\n", magic, EEPROM_MAGIC_NUMBER);

    if (magic != EEPROM_MAGIC_NUMBER) {
        printf("EEPROM not initialized. Setting up...\n");
        // write magic number to initialize
        magic = EEPROM_MAGIC_NUMBER;
        bool write_result = eeprom_write_bytes(i2c, ADDR_MAGIC, (uint8_t*)&magic, sizeof(magic));

        if (!write_result) {
            printf("Failed to write magic number to EEPROM\n");
            return false;
        }

        printf("Magic number written successfully\n");
    }

    printf("EEPROM initialized\n");
    return true;
}

bool save_state_to_eeprom(i2c_inst_t *i2c) {
    // globals

    bool success = true;
    uint8_t cal_flag = calibrated ? 1 : 0;

    // save calibration flag
    success &= eeprom_write_bytes(i2c, ADDR_CALIBRATED, &cal_flag, sizeof(cal_flag));

    // save numerical values
    // printf("Steps per rotation %d\nSteps per compartment: %d\n", steps_per_rotation, steps_per_compartment);
    success &= eeprom_write_bytes(i2c, ADDR_CURRENT_STEP, (uint8_t*)&current_step, sizeof(current_step));
    success &= eeprom_write_bytes(i2c, ADDR_PILLS_DISPENSED, (uint8_t*)&pills_dispensed, sizeof(pills_dispensed));
    success &= eeprom_write_bytes(i2c, ADDR_STEPS_ROTATION, (uint8_t*)&steps_per_rotation, sizeof(steps_per_rotation));
    success &= eeprom_write_bytes(i2c, ADDR_STEPS_COMPARTMENT, (uint8_t*)&steps_per_compartment, sizeof(steps_per_compartment));
    success &= eeprom_write_bytes(i2c, ADDR_DISPENSING_IN_PROGRESS, (uint8_t*)&dispensing_in_progress, sizeof(dispensing_in_progress));

    if (success) {
        printf("State saved to EEPROM\n");
    } else {
        printf("Failed to save EEPROM state\n");
    }

    return success;
}

bool load_state_from_eeprom(i2c_inst_t *i2c) {
    // define globals
    uint32_t magic;
    bool read_magic = eeprom_read_bytes(i2c, ADDR_MAGIC, (uint8_t*)&magic, sizeof(magic));

    if (!read_magic) {
        printf("Failed to read magic number\n");
        return false;
    }

    if (magic != EEPROM_MAGIC_NUMBER) {
        printf("EEPROM magic number doesn't match\n");
        return false;
    }

    uint8_t cal_flag;
    bool read_success = true;

    read_success &= eeprom_read_bytes(i2c, ADDR_CALIBRATED, &cal_flag, sizeof(cal_flag));
    read_success &= eeprom_read_bytes(i2c, ADDR_CURRENT_STEP, (uint8_t*)&current_step, sizeof(current_step));
    read_success &= eeprom_read_bytes(i2c, ADDR_PILLS_DISPENSED, (uint8_t*)&pills_dispensed, sizeof(pills_dispensed));
    read_success &= eeprom_read_bytes(i2c, ADDR_STEPS_ROTATION, (uint8_t*)&steps_per_rotation, sizeof(steps_per_rotation));
    read_success &= eeprom_read_bytes(i2c, ADDR_STEPS_COMPARTMENT, (uint8_t*)&steps_per_compartment, sizeof(steps_per_compartment));
    read_success &= eeprom_read_bytes(i2c, ADDR_DISPENSING_IN_PROGRESS, (uint8_t*)&dispensing_in_progress, sizeof(dispensing_in_progress));

    if (!read_success) {
        printf("Failed to read state values from EEPROM\n");
        return false;
    }

    calibrated = (cal_flag != 0);

    // prevent impossible step values
    if (steps_per_rotation > 10000 || steps_per_rotation < 0 ||
        steps_per_compartment > 2000 || steps_per_compartment < 0) {
        printf("Invalid step values in EEPROM, resetting to defaults\n");
        reset_calibration_values(i2c);
        return false;
    }

    // if not calibrated, ensure step values are reset to zero
    if (!calibrated) {
        steps_per_rotation = 0;
        steps_per_compartment = 0;
        pills_dispensed = 0;
        dispensing_in_progress = 0;
    }

    printf("State loaded from EEPROM\n");
    printf("  Calibrated: %s\n", calibrated ? "Yes" : "No");
    printf("  Current step: %d\n", current_step);
    printf("  Pills dispensed: %d\n", pills_dispensed);
    printf("  Steps per rotation: %d\n", steps_per_rotation);
    printf("  Steps per compartment: %d\n", steps_per_compartment);
    printf("  Dispensing in progress: %d\n", dispensing_in_progress);
    return true;
}

bool eeprom_write_bytes(i2c_inst_t *i2c, uint16_t addr, const uint8_t *data, size_t len) {
    // EEPROM writes page boundaries
    size_t bytes_written = 0;

    while (bytes_written < len) {
        // calculate current page and remaining bytes in this page
        uint16_t current_addr = addr + bytes_written;
        uint16_t page_start = (current_addr / EEPROM_PAGE_SIZE) * EEPROM_PAGE_SIZE;
        uint16_t page_end = page_start + EEPROM_PAGE_SIZE - 1;
        size_t bytes_remaining_in_page = page_end - current_addr + 1;
        size_t bytes_to_write = (len - bytes_written < bytes_remaining_in_page) ?
                                 len - bytes_written : bytes_remaining_in_page;

        // prepare EEPROM address (2 bytes) + data
        uint8_t buffer[2 + EEPROM_PAGE_SIZE]; // address + max page size
        buffer[0] = (current_addr >> 8) & 0xFF;  // high byte address
        buffer[1] = current_addr & 0xFF;         // low byte address
        memcpy(buffer + 2, data + bytes_written, bytes_to_write);

        // write to EEPROM with timeout
        int result = i2c_write_timeout_us(i2c, EEPROM_ADDR, buffer, bytes_to_write + 2, false, 100000);

        if (result != bytes_to_write + 2) {
            printf("EEPROM write failed at address 0x%04X: %d\n", current_addr, result);
            return false;
        }

        // wait for write cycle to complete
        sleep_ms(EEPROM_WRITE_TIMEOUT);
        bytes_written += bytes_to_write;
    }

    return true;
}


bool eeprom_read_bytes(i2c_inst_t *i2c, uint16_t addr, uint8_t *data, size_t len) {

    // read address
    uint8_t addr_buf[2];
    addr_buf[0] = (addr >> 8) & 0xFF;  // high byte address
    addr_buf[1] = addr & 0xFF;         // low byte address

    // write address with timeout, then read data
    int write_result = i2c_write_timeout_us(i2c, EEPROM_ADDR, addr_buf, 2, true, 100000);

    if (write_result != 2) {
        printf("EEPROM address write failed: %d\n", write_result);
        return false;
    }

    // read data with timeout
    int read_result = i2c_read_timeout_us(i2c, EEPROM_ADDR, data, len, false, 100000);

    if (read_result != len) {
        printf("EEPROM read failed: %d\n", read_result);
        return false;
    }

    return true;
}