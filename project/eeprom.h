//eeprom.h

#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"


bool init_eeprom(i2c_inst_t *i2c);

// prototypes
bool save_state_to_eeprom(i2c_inst_t *i2c);
bool load_state_from_eeprom(i2c_inst_t *i2c);
bool check_need_recovery(void);
void load_eeprom_state(i2c_inst_t *eeprom_i2c, repeating_timer_callback_t pill_timer_callback);
void reset_calibration_values(i2c_inst_t *i2c);
void reset_pill_count(i2c_inst_t *i2c);  // New function to reset only pill count
bool eeprom_write_bytes(i2c_inst_t *i2c, uint16_t addr, const uint8_t *data, size_t len);
bool eeprom_read_bytes(i2c_inst_t *i2c, uint16_t addr, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // EEPROM_MANAGER_H