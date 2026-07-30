#include "system.h"
#include "helpers.h"
#include "config.h"

#include <BlynkSimpleEsp32.h>
#include <Arduino.h>
#include <WiFi.h>

Preferences prefs;

static BlynkCallback callback = nullptr;

#define ADMIN_SHIFT 0
#define WIFI_SHIFT  2
#define BLYNK_SHIFT 4
#define AP_SHIFT    6

#define CACHED_BIT  0b01
#define VALUE_BIT   0b10

static uint8_t config_cache = 0;

bool check_configured(const char* ns, uint8_t shift) {
    uint8_t bits = (config_cache >> shift) & 0b11;

    if (bits & CACHED_BIT) {
        return bits & VALUE_BIT;
    }

    prefs.begin(ns, true);
    bool configured = prefs.getBool("isSetup", false);
    prefs.end();

    uint8_t new_bits = CACHED_BIT | (configured ? VALUE_BIT : 0);
    config_cache &= ~(0b11 << shift);
    config_cache |= (new_bits << shift);

    return configured;
}

void set_config_cache(uint8_t shift, bool configured) {
    uint8_t new_bits = CACHED_BIT | (configured ? VALUE_BIT : 0);
    config_cache &= ~(0b11 << shift);
    config_cache |= (new_bits << shift);
}

struct {
    String token = "";
    bool loaded = false;
} blynk_cache;

bool init_prefs() {
    const char *namespaces[] = {"prefs", "admin", "wifi", "ap", "blynk", "price", "log"};
    if(prefs.begin(namespaces[0], true)) {
        prefs.end();
        return true;
    }

    for (auto ns : namespaces) {
        if (!prefs.begin(ns, false)) {
            return false;
        }
        prefs.end();
    }

    prefs.begin(namespaces[5]);
    char buf[4];
    for (uint8_t i; i < SLOT_COUNT; i++) {
        itoa(i, buf, 10);
        prefs.putLong(buf, 0);        
    }
    prefs.end();

    return true;
}

bool is_admin_configured() { return check_configured("admin", ADMIN_SHIFT); }
bool is_wifi_configured()  { return check_configured("wifi",  WIFI_SHIFT); }
bool is_blynk_configured() { return check_configured("blynk", BLYNK_SHIFT); }
bool is_ap_configured()    { return check_configured("ap",    AP_SHIFT); }

bool set_wifi_credentials(const String& ssid, const String& password) {
    bool flag = true;

    if (ssid.length() < 1 || ssid.length() > 32) {
        return false;
    }

    if (password.length() != 0 && (password.length() < 8 || password.length() > 63)) {
        return false;
    }

    flag &= prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.putBool("isSetup", true);
    prefs.end();

    if (flag) set_config_cache(WIFI_SHIFT, true);

    return flag;
}

bool set_blynk_credentials(const String& auth_token) {
    bool flag = true;

    flag &= prefs.begin("blynk", false);
    prefs.putString("token", auth_token);
    prefs.putBool("isSetup", true);
    prefs.end();

    if (flag) {
        blynk_cache.loaded = true;
        blynk_cache.token = auth_token;
        set_config_cache(BLYNK_SHIFT, true);
    }

    return flag;
}

bool set_ap_credentials(const String& ssid, const String& password) {
    bool flag = true;
    if (ssid.length() < 1 || ssid.length() > 32) {
        return false;
    }

    if (password.length() != 0 && (password.length() < 8 || password.length() > 63)) {
        return false;
    }

    flag &= prefs.begin("ap", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.putBool("isSetup", true);
    prefs.end();

    if (flag) set_config_cache(AP_SHIFT, true);

    return flag;
}

bool set_admin_credentials(const String& username, const String& password) {
    bool flag = true;

    if (username.length() < 8 || password.length() < 8) {
        return false;
    }

    flag &= prefs.begin("admin", false);
    prefs.putString("uname", username);
    prefs.putString("hash", hash_password(password));
    prefs.putBool("isSetup", true);
    prefs.end();

    if (flag) set_config_cache(ADMIN_SHIFT, true);

    return flag;
}

bool get_wifi_credentials(String& ssid, String& password) {
    bool flag = true;

    flag &= prefs.begin("wifi", true);
    if (!prefs.getBool("isSetup", false)) {
        prefs.end();
        return false;
    }

    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    prefs.end();
    return flag;
}

bool get_blynk_credentials(String& auth_token) {
    bool flag = true;

    if (blynk_cache.loaded) {
        auth_token = blynk_cache.token;
        return true;
    }

    prefs.begin("blynk", true);
    if (!prefs.getBool("isSetup", false)) {
        prefs.end();
        return false;
    }

    auth_token = prefs.getString("token", "");
    prefs.end();

    blynk_cache.loaded = true;
    blynk_cache.token = auth_token;
    return flag;
}

bool get_ap_credentials(String& ssid, String& password) {
    bool flag = true;
    flag &= prefs.begin("ap", true);
    if (!prefs.getBool("isSetup", false)) {
        prefs.end();
        return false;
    }
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    prefs.end();
    return flag;
}

bool get_admin_username(String& username) {
    bool flag = true;
    prefs.begin("admin", true);
    if (!prefs.getBool("isSetup", false)) {
        prefs.end();
        return false;
    }
    username = prefs.getString("uname", "");
    prefs.end();
    return flag;
}

bool get_admin_password_hash(String& hash) {
    bool flag = true;
    flag &= prefs.begin("admin", true);
    if (!prefs.getBool("isSetup", false)) {
        prefs.end();
        return false;
    }
    hash = prefs.getString("hash", "");
    prefs.end();
    return flag;
}

bool network_begin() {
    WiFi.mode(WIFI_AP_STA);

    #ifdef WOKWI_TEST
    prefs.begin("wifi", false);
    prefs.putString("ssid", "Wokwi-GUEST");
    prefs.putString("pass", "");
    prefs.putBool("isSetup", true);
    prefs.end();
    #endif

    wifi_connect();
    blynk_connect();
    return ap_begin();
}

bool store_price(uint8_t slot, uint32_t price) {
    if(!prefs.begin("price", false)) {
        return false;
    }
    char slot_string[4];
    itoa(slot, slot_string, 10);

    size_t written = prefs.putLong(slot_string, (int32_t)price);
    prefs.end();

    return written == sizeof(int32_t);
}

bool fetch_price(uint8_t slot, uint32_t &price) {
    if(!prefs.begin("price", true)) {
        return false;
    }

    char slot_string[4];
    itoa(slot, slot_string, 10);

    int32_t price_buf = prefs.getLong(slot_string, -1);
    prefs.end();

    if (price_buf == -1) {
        return false;
    }

    price = (uint32_t)price_buf;
    return true;
}

bool store_log_level(uint8_t level) {
    if (!prefs.begin("log", false)) {
        return false;
    }
    prefs.putInt("level", (int8_t)level);
    prefs.end();
}

bool fetch_log_level(uint8_t& level) {
    if (!prefs.begin("log", true)) {
        return false;
    }
    int8_t stored_level = prefs.getInt("level", -1);

    if (stored_level < 0) {
        return false;
    }
    level = stored_level;
    return true;
}

bool store_log_counter(uint32_t counter) {
    if (!prefs.begin("log", false)) {
        return false;
    }
    prefs.putLong("counter", (int32_t)counter);
    prefs.end();
}

bool fetch_log_counter(uint32_t& counter) {
    if (!prefs.begin("log", true)) {
        return false;
    }
    int8_t stored_counter = prefs.getLong("counter", -1);

    if (stored_counter < 0) {
        return false;
    }
    counter = stored_counter;
    return true;
}

void set_blynk_callback(BlynkCallback cb) {
    callback = cb;
}

BLYNK_WRITE(V0) {
    if (callback) callback(param.asInt());
}

bool blynk_run() {
    if (!is_blynk_configured()) {
        return false;
    }
    if (!is_blynk_connected()) {
        blynk_connect();
    }
    return Blynk.run();
}

bool wifi_connect() {
    String wifi_ssid, wifi_password;

    if (is_wifi_configured()) {
        if (get_wifi_credentials(wifi_ssid, wifi_password)) {
            WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
            return true;
        }
    }

    return false;
}

bool is_wifi_connected() {
    return (WiFi.status() == WL_CONNECTED);
}

bool ap_begin() {
    String ap_ssid, ap_password;
    
    if (is_ap_configured()) {
        if (get_ap_credentials(ap_ssid, ap_password)) {
            return WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
        }
    }

    return WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);
}

bool blynk_connect() {
    String stored_token;
    if (!get_blynk_credentials(stored_token)) {
        return false;
    }
    Blynk.config(stored_token.c_str());
    Blynk.connect();
    return true;
}

bool is_blynk_connected() {
    if (is_blynk_configured()) {
        return Blynk.connected();
    }
    return false;
}