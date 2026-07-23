#include "system.h"
#include "helpers.h"
#include "config.h"

#include <BlynkSimpleEsp32.h>
#include <Arduino.h>
#include <WiFi.h>

Preferences prefs;

static BlynkCallback callback = nullptr;

bool init_prefs() {
    const char *namespaces[] = {"prefs", "admin", "wifi", "ap", "blynk", "price"};
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

bool is_admin_configured() {
    prefs.begin("admin", true);
    bool configured = prefs.getBool("isSetup", false);
    prefs.end();
    return configured;
}

bool is_wifi_configured() {
    prefs.begin("wifi", true);
    bool configured = prefs.getBool("isSetup", false);
    prefs.end();
    return configured;
}

bool is_blynk_configured() {
    prefs.begin("blynk", true);
    bool configured = prefs.getBool("isSetup", false);
    prefs.end();
    return configured;
}

bool is_ap_configured() {
    prefs.begin("ap", true);
    bool configured = prefs.getBool("isSetup", false);
    prefs.end();
    return configured;
}

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

    return flag;
}

bool set_blynk_credentials(const String& auth_token) {
    bool flag = true;

    flag &= prefs.begin("blynk", false);
    prefs.putString("token", auth_token);
    prefs.putBool("isSetup", true);
    prefs.end();

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
    prefs.begin("blynk", true);
    if (!prefs.getBool("isSetup", false)) {
        prefs.end();
        return false;
    }

    auth_token = prefs.getString("token", "");
    prefs.end();
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

void set_blynk_callback(BlynkCallback cb) {
    callback = cb;
}

BLYNK_WRITE(V0) {
    if (callback) callback(param.asInt());
}

bool blynk_run() {
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
    return true;
}

bool is_blynk_connected() {
    if (is_blynk_configured()) {
        return Blynk.connected();
    }
    return false;
}