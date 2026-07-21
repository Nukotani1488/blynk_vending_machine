#pragma once

bool constant_time_equal(const String& a, const String& b);
bool verify_admin_password(const String& password);
String hash_password(const String& password);