//lorawan.h

#ifndef LORAWAN_H
#define LORAWAN_H

#include <stdbool.h>

bool lorawan_try_connect(void);
bool lorawan_send_command(const char *command, char *response, const char *expected_outcome);
void lorawan_send_text(bool connected, const char* text);
void init_lorawan(void);
bool try_join(void);

#endif // LORAWAN_H
