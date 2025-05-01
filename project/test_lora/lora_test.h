//
// Created by yep on 30/04/2025.
//

#ifndef LORA_TEST_H
#define LORA_TEST_H

#include <stdbool.h>
#include "hardware/uart.h"

void init_lorawan(void);

bool lorawan_try_connect(void);

bool lorawan_send_command(const char *command, char *where_to_store_response, const char *expected_outcome);

typedef bool (*ResponseValidator)(const char* response, void* context);

bool lorawan_read_response(uint64_t timeout_us, ResponseValidator validator, void* context);

bool try_join(void);

void lorawan_send_text(bool connected, const char* text);

#endif //LORA_TEST_H
