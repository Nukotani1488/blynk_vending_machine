#pragma once

#include <stdint.h>
#include "config.h"

bool set_price(uint8_t slot, uint32_t price);
bool get_price(uint8_t slot, uint32_t &price);
bool parse_paid(int32_t paid, uint8_t& slot, uint32_t& price);