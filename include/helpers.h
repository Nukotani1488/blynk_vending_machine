#pragma once

#include <Arduino.h>

#define BITS_FOR_SIZE(size) (32 - __builtin_clz((size) - 1))
#define EXTRACT_LOW_BITS(value, size) ((value) & ((1u << BITS_FOR_SIZE(size)) - 1))

bool constant_time_equal(const String& a, const String& b);
String hash_password(const String& password);
bool verify_admin_password(const String& password, const String& hash);