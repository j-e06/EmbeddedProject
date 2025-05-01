//lorawan.c
/*
 * LoRaWAN Driver Implementation for Raspberry Pi Pico
 * Optimized for reliability and performance
 */

#include "lora_test.h"
#include "../config.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>

#include "pico/stdlib.h"

// respo buffer
static char response_buffer[STRLEN] = {0};

// command validator
typedef struct {
    const char* expected_outcome;
    bool found;
} CmdContext;
/**
callbakc function ran during the response reading to check if a given response from loraWAN matches our expected
outcome
 */
bool command_validator(const char* response, void* ctx) {
    CmdContext* cmd_ctx = (CmdContext*)ctx;

    if (strstr(response, cmd_ctx->expected_outcome) != NULL) {
        cmd_ctx->found = true;
        return true; // end processing since we got a success
    }

    return false; // continue processing
}
/**
 send command to lorawan and wait for response
 validates response using the command_validator callback function
 utilizes lorawan_read_response to handle the response checking
 */
bool lorawan_send_command(const char *command, char *where_to_store_response, const char *expected_outcome) {
    // clear resp buffer before using
    if (where_to_store_response) {
        memset(where_to_store_response, 0, STRLEN);
    }

    // EoL writing
    uart_write_blocking(uart1, (const uint8_t*)command, strlen(command));
    uart_write_blocking(uart1, (const uint8_t*)"\r\n", 2);

    printf("Command: %s\n", command);

    // creating cmd context
    CmdContext cmd_ctx = { expected_outcome, false };

    // validate
    if (!lorawan_read_response(LORAWAN_TIMEOUT_MS * 1000, command_validator, &cmd_ctx)) {
        printf("Command timed out: %s\n", command);
        return false;
    }

    // timed out or match found
    return cmd_ctx.found;
}

/**
 reads response from lorawan until timeout or until validator func says we've got a success
 handles line by line reading from uart
 */
bool lorawan_read_response(uint64_t timeout_us, bool (*validator)(const char*, void*), void* context) {
    int pos = 0;
    memset(response_buffer, 0, STRLEN); // clear resp buffer

    absolute_time_t timeout_time = make_timeout_time_us(timeout_us);

    while (!time_reached(timeout_time)) {
        if (uart_is_readable(uart1)) {
            char c = uart_getc(uart1);
            if (pos < STRLEN - 1) {
                response_buffer[pos++] = c;
                response_buffer[pos] = '\0';

                if (c == '\n') {
                    // line done
                    printf("Response: %s", response_buffer);

                    // make sure it's valid
                    if (validator && validator(response_buffer, context)) {
                        return true;
                    }

                    // clear buffer after line processing
                    pos = 0;
                    memset(response_buffer, 0, STRLEN);
                }
            } else {
                // buffer overflow bs
                pos = 0;
                memset(response_buffer, 0, STRLEN);
            }
        } else {
            // stop cpu from being a lil *****
            sleep_ms(5);
        }
    }

    printf("Read timed out.\n");
    return false;
}

// join resp validator
typedef struct {
    bool join_success;
    bool join_failed;
} JoinContext;
/**
 a validator specifically for join responses, idk why they decided it has so many ***** responses
 checks  for response and updates the join context accordingly
 */
bool join_validator(const char* response, void* ctx) {
    JoinContext* join_ctx = ctx;

    if (strstr(response, "+JOIN: Join failed")) {
        printf("Join failed!\n");
        join_ctx->join_failed = true;
        return true; // end  with failure
    } else if (strstr(response, "+JOIN: LoRaWAN modem is busy")) {
        printf("Join ongoing...\n");
    } else if (strstr(response, "+JOIN: Done")) {
        printf("Join successful!\n");
        join_ctx->join_success = true;
        return true; // end with success
    }

    return false; // continue
}

/**
 tries to join the school lorawan network
 first sends all the necessary commands, then the +join command that has a stupid amount of responses
 so we have to make these funcs to handle it properly

 */
bool try_join() {
    // initial network commands
    if (!lorawan_send_command(LORAWAN_MODE, response_buffer, LORAWAN_MODE_OUTCOME)) {
        printf("LoRa Mode command failed\n");
        return false;
    }
    if (!lorawan_send_command(LORAWAN_KEY, response_buffer, LORAWAN_KEY_OUTCOME)) {
        printf("LoRa appkey command failed\n");
        return false;
    }
    if (!lorawan_send_command(LORAWAN_CLASS, response_buffer, LORAWAN_CLASS_OUTCOME)) {
        printf("LoRa class command failed\n");
        return false;
    }
    if (!lorawan_send_command(LORAWAN_PORT, response_buffer, LORAWAN_PORT_OUTCOME)) {
        printf("LoRa port command failed\n");
        return false;
    }

    printf("Sending JOIN command...\n");
    uart_write_blocking(uart1, (const uint8_t*)LORAWAN_JOIN, strlen(LORAWAN_JOIN));
    uart_write_blocking(uart1, (const uint8_t*)"\r\n", 2);

    // join context initialization, hacky way of making sure responses are valid but meh
    JoinContext join_ctx = { false, false };

    // read join responses and validate them with a 20 sec timeout
    lorawan_read_response(20 * 1000000, join_validator, &join_ctx);

    return join_ctx.join_success && !join_ctx.join_failed;
}

// message response validator
typedef struct {
    bool msg_done;
} MsgContext;

/**
 validate message responses from lorawan
 mainly for the send_text function, the end result of it is +MSG: Done so we're only care about that
 */
bool msg_validator(const char* response, void* ctx) {
    MsgContext* msg_ctx = (MsgContext*)ctx;

    if (strstr(response, "+MSG: Done") != NULL) {
        printf("Success: Message Sent\n");
        msg_ctx->msg_done = true;
        return true; // End processing with success
    }

    return false; // Continue processing
}
/***
 send text to lorawan network
 makes sure that the given text is not null, and modifies it to be in the proper format
 waits for response and validates it using msg_validator function

 */
void lorawan_send_text(bool connected, const char* text) {
    // make sure lorawan is connected
    if (!connected) {
        printf("LoraWAN is not connected. Skipping message send.\n");
        return;
    }

    // make sure given text isn't null or empty
    if (!text || text[0] == '\0') {
        printf("Error: Cannot send empty message\n");
        return;
    }

    // bufrfer size calculation
    size_t text_len = strlen(text);
    size_t cmd_size = text_len + 12; // "AT+MSG=\"\"!" + null terminator

    char command[cmd_size];
    snprintf(command, cmd_size, "AT+MSG=\"%s!\"", text); // combine given text into the right format

    // send command and handle the results
    if (lorawan_send_command(command, response_buffer, "+MSG:")) {
        // Create message context and initialize it
        MsgContext msg_ctx = { false };

        // Read and process MSG responses with a 10-second timeout
        lorawan_read_response(10 * 1000000, msg_validator, &msg_ctx);

        if (!msg_ctx.msg_done) {
            printf("Warning: Message send did not complete\n");
        }
    } else {
        // we didn't get a msg + done so we failed for reasons xyz
        printf("Failure: Message Sending Failed\n");
    }
}

/**
 initialize lorawan module
 */
void init_lorawan() {
    uart_init(uart1, LORAWAN_BAUD_RATE);

    gpio_set_function(UART_TX, GPIO_FUNC_UART);
    gpio_set_function(UART_RX, GPIO_FUNC_UART);
    printf("LoraWAN initialized...\n");
}

/**
 tries to join lorawan network LORAWAN_MAX_TRIES amount of times
 sends a test message before attempting join
 if successfull, proceeds to try_join()
 why we've split these into 2 functions? good question, i forgot why i did that last week
 but we're sticking with it anyway
 */
bool lorawan_try_connect() {
    printf("Trying to connect to LoRaWAN module...\n");

    for (int i = 0; i < LORAWAN_MAX_TRIES; i++) {
        printf("Connection attempt %d of %d...\n", i + 1, LORAWAN_MAX_TRIES);

        if (lorawan_send_command(LORA_TEST, response_buffer, LORA_TEST_OUTCOME)) {
            printf("Successfully connected to LoRaWAN module, trying to join network...\n");

            // try join once per attempt
            if (try_join()) {
                printf("Connected to network\n");
                return true;
            } else {
                printf("Failed to join network, will retry connection sequence...\n");
                sleep_ms(1000); // wait a sec before reattemptpuing
            }
        } else {
            printf("No response from LoRaWAN module on attempt %d\n", i + 1);
        }
    }

    printf("Failed to establish connection after %d attempts\n", LORAWAN_MAX_TRIES);
    return false;
}