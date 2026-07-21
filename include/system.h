#pragma once

#include <Preferences.h>

extern Preferences prefs;

bool is_admin_configured();
bool is_wifi_configured();
bool is_blynk_configured();
bool is_ap_configured();

bool set_wifi_credentials(const String& ssid, const String& password);
bool set_blynk_credentials(const String& auth_token);
bool set_ap_credentials(const String& ssid, const String& password);
bool set_admin_credentials(const String& username, const String& password);

bool get_wifi_credentials(String& ssid, String& password);
bool get_blynk_credentials(String& auth_token);
bool get_ap_credentials(String& ssid, String& password);
bool get_admin_username(String& username);
bool get_admin_password_hash(String& hash);