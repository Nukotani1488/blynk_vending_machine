#include "price.h"
#include "system.h"
#include "helpers.h"

typedef struct {
    uint32_t value = 0;
    bool loaded = false;
} Price;

static Price prices[SLOT_COUNT] = {};

bool set_price(uint8_t slot, uint32_t price) {
    if (slot >= SLOT_COUNT) {
        return false;
    }

    if (!store_price(slot, price)) {
        return false;
    }

    prices[slot].loaded = true;
    prices[slot].value = price;

    return true;
}

bool get_price(uint8_t slot, uint32_t &price) {
    if (slot >= SLOT_COUNT) {
        return false;
    }

    if (prices[slot].loaded) {
        price = prices[slot].value;
        return true;
    }

    if (!fetch_price(slot, price)) {
        prices[slot].loaded = false;
        return false;
    }

    prices[slot].loaded = true;
    prices[slot].value = price;

    return true;
}

bool validate_price(uint8_t slot, uint32_t price) {
    uint32_t stored_price;
    if(!get_price(slot, stored_price)) {
        return false;
    }

    return stored_price == price;
}

uint32_t extract_price_from_value(uint32_t value, uint8_t slot) {
    return value - slot;
}

bool parse_paid(int32_t paid, uint8_t& slot, uint32_t& price) {
    uint8_t extracted_slot = EXTRACT_LOW_BITS(paid, SLOT_COUNT);
    if (extracted_slot >= SLOT_COUNT) {
        return false;
    }

    slot = extracted_slot;

    uint32_t extracted_price = paid - slot;
    if (!validate_price(extracted_slot,  extracted_price)) {
        return false;
    }
    price = extracted_price;

    return true;
}

