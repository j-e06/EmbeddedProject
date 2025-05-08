// config.h

#ifndef CONFIG_H
#define CONFIG_H


#include <pico/util/queue.h>

#include "pico/time.h"
#include "config.h"

#define LEFT_BUTTON         9
#define CENTER_BUTTON       8  // calibration button
#define RIGHT_BUTTON        7  // pill dispenser button

#define EEPROM_SDA_PIN 16
#define EEPROM_SCL_PIN 17

#define LEFT_LED            20
#define CENTER_LED          21
#define RIGHT_LED           22

#define IN1                 2
#define IN2                 3
#define IN3                 6
#define IN4                 13
#define OPTO_FORK           28
#define PIEZO_GPIO          27

#define TIME_BETWEEN_PILLS  5000 // for testing purposes the pills are dispensed now every Xs, to change it to 30s
#define FIRST_PILL_DELAY    5000 // make them both 30000

#define MAX_PILLS           7
#define COMPARTMENTS        8
#define COMPARTMENT_OFFSET 150
#define ERROR_BLINK_COUNT   5
#define LONG_PRESS_DURATION 2000   // 2 seconds
#define PIEZO_DEBOUNCE_MS   1000   // 1 second debounce for piezo sensor


// eeprom config
#define EEPROM_ADDR           0x50    // I2C address
#define EEPROM_SIZE           32768    // size in bytes?
#define EEPROM_PAGE_SIZE      64      // how many pages
#define EEPROM_WRITE_TIMEOUT  5       // wait for write cycle

// state storage addresses
#define ADDR_MAGIC            0       // magic number to check if EEPROM is initialized (4 bytes)
#define ADDR_CALIBRATED       4       // calibration flag (1 byte)
#define ADDR_CURRENT_STEP     5       // current step position (4 bytes)
#define ADDR_PILLS_DISPENSED  9       // pills dispensed (4 bytes)
#define ADDR_STEPS_ROTATION   13      // steps per rotation (4 bytes)
#define ADDR_STEPS_COMPARTMENT 17     // steps per compartment (4 bytes)
#define ADDR_DISPENSING_IN_PROGRESS 25


// magic number to validate EEPROM content
#define EEPROM_MAGIC_NUMBER   0xABC123  // no difference

#define LORA_TEST "AT"

#define LORA_TEST_OUTCOME "+AT: OK"

#define UART_TX 4
#define UART_RX 5

#define LORAWAN_BAUD_RATE 9600
#define LORAWAN_TIMEOUT_MS 500
#define LORAWAN_MAX_TRIES 1
#define STRLEN 1024

#define LORAWAN_MODE "AT+MODE=LWOTAA"
#define LORAWAN_KEY "AT+KEY=APPKEY,\"44F649EDCE50703B29776CE6CFFB46F4\""
#define LORAWAN_CLASS "AT+CLASS=A"
#define LORAWAN_PORT "AT+PORT=8"
#define LORAWAN_JOIN "AT+JOIN"


#define LORAWAN_MODE_OUTCOME "+MODE: LWOTAA"
#define LORAWAN_KEY_OUTCOME "+KEY: APPKEY 44F649EDCE50703B29776CE6CFFB46F4"
#define LORAWAN_CLASS_OUTCOME "+CLASS: A"
#define LORAWAN_PORT_OUTCOME "+PORT: 8"

extern int steps_per_rotation;
extern int steps_per_compartment;
extern int current_step;
extern bool calibrated;
extern bool dispense_pill_flag;
extern bool led_blink_flag;
extern bool lorawan_connected;
extern bool eeprom_initialized;
extern int pills_dispensed;
extern int dispensing_in_progress;


// timestamping
extern uint32_t last_led_toggle;
extern uint32_t last_piezo_time;
extern uint32_t first_delay_start;

// queue & timer
extern queue_t events;
extern struct repeating_timer timer;

// System states
typedef enum {
    S_WAIT_CAL,
    S_IDLE,
    S_FIRST_DELAY,
    S_DISPENSE,
    S_ERROR
} system_state_t;

// event types
typedef enum {
    EV_OPTO,
    EV_PIEZO
} event_type_t;

// event structure
typedef struct {
    event_type_t type;
    uint32_t timestamp;
} event_t;


extern system_state_t state;


#endif //CONFIG_H