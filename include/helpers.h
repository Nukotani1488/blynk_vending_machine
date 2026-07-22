#pragma once

#include <Arduino.h>

bool constant_time_equal(const String& a, const String& b);
String hash_password(const String& password);
bool verify_admin_password(const String& password);