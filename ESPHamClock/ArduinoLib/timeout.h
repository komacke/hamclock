// timeout.h
#ifndef TIMEOUT_H
#define TIMEOUT_H

#include <stdint.h>

// Default timeout in milliseconds
static constexpr uint16_t TIMEOUT_DEFAULT_MS = 10000;
static constexpr uint16_t TIMEOUT_MAX_MS     = 65535;

// Get the current timeout in milliseconds
uint16_t get_timeout_ms();

// Set timeout in milliseconds
void set_timeout_ms(uint16_t ms);

// Set timeout in seconds (converted to ms internally)
void set_timeout_s(uint8_t s);

#endif // TIMEOUT_H
