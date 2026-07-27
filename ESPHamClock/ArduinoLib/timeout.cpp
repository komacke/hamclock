// timeout.cpp
#include "timeout.h"
#include <algorithm>

static uint16_t s_timeout_ms = TIMEOUT_DEFAULT_MS;

uint16_t get_timeout_ms()
{
    return s_timeout_ms;
}

void set_timeout_ms(uint16_t ms)
{
    s_timeout_ms = ms;
}

void set_timeout_s(uint8_t s)
{
    s_timeout_ms = static_cast<uint16_t>(
        std::min(static_cast<uint32_t>(s) * 1000u,
                 static_cast<uint32_t>(TIMEOUT_MAX_MS)));
}
