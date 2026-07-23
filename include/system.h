#pragma once

#include <Preferences.h>

extern Preferences prefs;

bool init_prefs();

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

bool store_price(uint8_t slot, uint32_t price);
bool fetch_price(uint8_t slot, uint32_t &Price);

bool network_begin();

typedef void (*BlynkCallback)(int32_t value);
void set_blynk_callback(BlynkCallback callback);

bool blynk_run();

bool wifi_connect();
bool is_wifi_connected();
bool ap_begin();
bool blynk_connect();
bool is_blynk_connected();

