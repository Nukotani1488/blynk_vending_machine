#include "system.h"
#include "helpers.h"

Preferences prefs;

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
    flag &= prefs.putString("ssid", ssid);
    flag &= prefs.putString("pass", password);
    flag &= prefs.putBool("isSetup", true);
    prefs.end();

    return flag;
}

bool set_blynk_credentials(const String& auth_token) {
    bool flag = true;

    flag &= prefs.begin("blynk", false);
    flag &= prefs.putString("token", auth_token);
    flag &= prefs.putBool("isSetup", true);
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
    flag &= prefs.putString("ssid", ssid);
    flag &= prefs.putString("pass", password);
    flag &= prefs.putBool("isSetup", true);
    prefs.end();

    return flag;
}

bool set_admin_credentials(const String& username, const String& password) {
    bool flag = true;

    if (username.length() < 8 || password.length() < 8) {
        return false;
    }

    flag &= prefs.begin("admin", false);
    flag &= prefs.putString("uname", username);
    flag &= prefs.putString("hash", hash_password(password));
    flag &= prefs.putBool("isSetup", true);
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

