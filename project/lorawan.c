//lorawan.c

#include "lorawan.h"
#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>

#include "pico/stdlib.h"

static char response_buffer[STRLEN] = {0};

bool try_join() {
    static char failed_join[] = "+JOIN: Join failed";
    static char join_ongoing[] = "+JOIN: LoRaWAN modem is busy";
    static char join_done[] = "+JOIN: Done";

    // printf("Command: %s\n", LORAWAN_MODE);
    if (!lorawan_send_command(LORAWAN_MODE, response_buffer, LORAWAN_MODE_OUTCOME)) {
        printf("LoRa Mode command did not work...\n");
        return false;
    }
    if (!lorawan_send_command(LORAWAN_KEY, response_buffer, LORAWAN_KEY_OUTCOME)) {
        printf("LoRa appkey command did not work...\n");
        return false;
    }
    if (!lorawan_send_command(LORAWAN_CLASS, response_buffer, LORAWAN_CLASS_OUTCOME)) {
        printf("LoRa class command did not work...\n");
        return false;
    }
    if (!lorawan_send_command(LORAWAN_PORT, response_buffer, LORAWAN_PORT_OUTCOME)) {
        printf("LoRa port command did not work...\n");
        return false;
    }

    printf("Sending JOIN command...\n");
    uart_write_blocking(uart1, (const uint8_t*)LORAWAN_JOIN, strlen(LORAWAN_JOIN));
    uart_write_blocking(uart1, (const uint8_t*)"\r\n", 2);

    int pos = 0;
    uint64_t start_time = time_us_64();
    uint64_t timeout = 20 * 1000000; // 20 seconds

    while (time_us_64() - start_time < timeout) {
        while (uart_is_readable_within_us(uart1, 1000000)) { // Check within 1 sec for chars
            char c = uart_getc(uart1);
            if (pos < STRLEN - 1) {
                response_buffer[pos++] = c;
                response_buffer[pos] = '\0';

                if (c == '\n') {
                    printf("JOIN response: %s", response_buffer);

                    if (strstr(response_buffer, failed_join)) {
                        printf("Join failed!\n");
                        pos = 0;
                        memset(response_buffer, 0, STRLEN);
                        return false;
                    } else if (strstr(response_buffer, join_ongoing)) {
                        printf("Join ongoing...\n");
                    } else if (strstr(response_buffer, join_done)) {
                        printf("Join successful!\n");
                       // join_success = true;
                        pos = 0;
                        memset(response_buffer, 0, STRLEN);
                        return true; // Exit upon receiving Done
                    }
                    // Clear buffer after processing each line
                    pos = 0;
                    memset(response_buffer, 0, STRLEN);
                }
            } else {
                // Handle buffer overflow
                pos = 0;
                memset(response_buffer, 0, STRLEN);
            }
        }
        sleep_ms(10); // Shorter delay to check more frequently
    }

    printf("Join timed out.\n");
    return false;
}

void lorawan_send_text(bool connected, const char* text) {
    // Return early if not connected
    if (!connected) {
        printf("Device is not connected. Skipping message send.\n");
        return;
    }

    char respo[STRLEN];
    char command[strlen(text) + 12];

    // Format the command for sending text
    snprintf(command, sizeof(command), "AT+MSG=\"%s!\"", text);

    // Attempt to send the command and handle different outcomes
    if (lorawan_send_command(command, respo, "+MSG:")) {
        // Wait for a response sequence, check for the final success message
        if (strstr(respo, "Start") != NULL) {
            printf("Success: Message Sent\n");
        } else {
            printf("Failure: Incomplete Response. Expected '+MSG: Done'.\n");
        }
    } else {
        printf("Failure: Message Sending Failed\n");
    }
}

bool lorawan_send_command(const char *command, char *where_to_store_response, const char * expected_outcome) {
    int pos = 0;
    // write commands and end of line

    if (where_to_store_response) {
        memset(where_to_store_response, 0, STRLEN);
    }

    uart_write_blocking(uart1, (const uint8_t*)command, strlen(command));
    uart_write_blocking(uart1, (const uint8_t*)"\r\n", 2);

    // wait for response
    absolute_time_t timeout_time = make_timeout_time_ms(LORAWAN_TIMEOUT_MS);
    while (!time_reached(timeout_time)) {
        if (uart_is_readable(uart1)) {
            char c = uart_getc(uart1);
            if (pos < STRLEN -1 && where_to_store_response) {
                where_to_store_response[pos++] = c;
                where_to_store_response[pos] = '\0';

                if (c == '\n' && pos > 1) {
                    printf("Command: %s\nResponse: %s\n",command, where_to_store_response);
                    if (strstr(where_to_store_response, expected_outcome) != NULL) {
                        return true;
                    }
                    else {
                        return false;
                    }
                }
            } else {
                sleep_ms(1);
            }
        }
    }
    printf("Command timeout: %s\n", command);
    return false;
}

void init_lorawan() {
    uart_init(uart1, LORAWAN_BAUD_RATE);

    gpio_set_function(UART_TX, GPIO_FUNC_UART);
    gpio_set_function(UART_RX, GPIO_FUNC_UART);
}
bool lorawan_try_connect() {
    printf("Trying to connect to LoraWan module...\n");

    for (int i = 0; i < LORAWAN_MAX_TRIES; i++) {
        printf("Connection attempt %d of %d...\n", i + 1, LORAWAN_MAX_TRIES);

        if (lorawan_send_command(LORA_TEST, response_buffer, LORA_TEST_OUTCOME)) {
            printf("Succesfully connected to LoraWAN module, trying to join network...\n");
            if (try_join()) {
                printf("Connected to network!\n");
                return true;
            }
            else {
                printf("Failed to join, will try again...\n");
                sleep_ms(1000); // slight delay
            }
        }
        else {
            printf("No response from LoraWAN module on attempt %d.\n", i + 1);
        }
    }

    printf("Failed to establish connection after %d attempts\n", LORAWAN_MAX_TRIES);
    return false;
}