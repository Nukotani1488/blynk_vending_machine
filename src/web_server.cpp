// web_server.cpp
#include "web_server.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>

AsyncWebServer server(80);
Preferences prefs;

static String post_buf = "";
static bool post_buf_lock = false;

static String session_token = "";
static uint32_t session_token_expiration = 0;
const uint32_t SESSION_TIMEOUT = 30UL * 60 * 1000;


bool constant_time_equal(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    uint8_t result = 0;
    for (size_t i = 0; i < a.length(); i++) result |= a[i] ^ b[i];
    return result == 0;
}

String generate_session_token() {
    uint8_t buf[24];
    for (int i = 0; i < 24; i++) buf[i] = esp_random() & 0xFF;
    String token = "";
    for (int i = 0; i < 24; i++) {
        char hex[3];
        sprintf(hex, "%02x", buf[i]);
        token += hex;
    }
    return token;
}

bool is_session_occupied() {
    if (session_token_expiration == 0) return false;
    if (millis() > session_token_expiration) {
        session_token = "";
        session_token_expiration = 0;
        return false;
    }
    return true;
}

bool is_session_valid(const String& token) {
    return is_session_occupied() && token == session_token;
}

String hash_password(const String& password) {
    unsigned char hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char*)password.c_str(), password.length());
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    String hex_hash = "";
    for (int i = 0; i < 32; i++) {
        char buf[3];
        sprintf(buf, "%02x", hash[i]);
        hex_hash += buf;
    }
    return hex_hash;
}

bool is_admin_configured() {
    prefs.begin("admin", true);
    bool configured = prefs.getBool("isSetup", false);
    prefs.end();
    return configured;
}

String get_cookie_value(AsyncWebServerRequest *request, const String& cookieName) {
    if (!request->hasHeader("Cookie")) return "";
    String cookies = request->header("Cookie");
    int start = cookies.indexOf(cookieName + "=");
    if (start == -1) return "";
    start += cookieName.length() + 1;
    int end = cookies.indexOf(';', start);
    if (end == -1) end = cookies.length();
    return cookies.substring(start, end);
}

bool request_has_valid_session(AsyncWebServerRequest *request) {
    if (!is_session_occupied()) return false;
    String cookie_token = get_cookie_value(request, "session");
    if (cookie_token == "") return false;
    return constant_time_equal(cookie_token, session_token);
}

typedef std::function<void(AsyncWebServerRequest*)> ArRequestHandlerFn;

ArRequestHandlerFn require_auth_page(ArRequestHandlerFn handler) {
    return [handler](AsyncWebServerRequest *request) {
        if (!request_has_valid_session(request)) {
            request->redirect("/login");
            return;
        }
        handler(request);
    };
}

ArRequestHandlerFn require_auth_api(ArRequestHandlerFn handler) {
    return [handler](AsyncWebServerRequest *request) {
        if (!request_has_valid_session(request)) {
            request->send(401, "text/plain", "tidak terautentikasi");
            return;
        }
        handler(request);
    };
}


void startWebServer() {
    if (!LittleFS.begin(true)) {
        Serial.println("gagal mount LittleFS");
        return;
    }

    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (is_admin_configured()) {
            request->send(403, "text/plain", "telah dikonfigurasi");
            return;
        }
        request->send(LittleFS, "/setup.html", "text/html");
    });

    server.on("/setup", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (is_admin_configured()) {
                post_buf = "";
                post_buf_lock = false;
                request->send(403, "text/plain", "telah dikonfigurasi");
                return;
            }

            if (is_session_occupied()) {
                post_buf = "";
                post_buf_lock = false;
                request->send(400, "text/plain", "sesi lain sedang berlangsung");
                return;
            }

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, post_buf);
            post_buf = "";
            post_buf_lock = false;

            if (err) {
                request->send(400, "text/plain", "json tidak valid");
                return;
            }

            String username = doc["username"] | "";
            String password = doc["password"] | "";
            String confirmPassword = doc["confirmPassword"] | "";

            if (username.length() < 3 || password.length() < 8) {
                request->send(400, "text/plain", "username/password terlalu pendek");
                return;
            }

            if (password != confirmPassword) {
                request->send(400, "text/plain", "password tidak cocok");
                return;
            }

            prefs.begin("admin", false);
            prefs.putString("username", username);
            prefs.putString("passHash", hash_password(password));
            prefs.putBool("isSetup", true);
            prefs.end();

            request->send(200, "text/plain", "ok");
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (post_buf_lock) {
                request->send(400, "text/plain", "sesi lain sedang berlangsung");
                return;
            }
            if (index == 0) post_buf = "";
            post_buf += String((char*)data).substring(0, len);
            post_buf_lock = true;
        }
    );

    server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!is_admin_configured()) {
            request->redirect("/setup");
            return;
        }
        request->send(LittleFS, "/login.html", "text/html");
    });

    server.on("/login", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!is_admin_configured()) {
                post_buf = "";
                post_buf_lock = false;
                request->redirect("/setup");
                return;
            }

            if (is_session_occupied()) {
                post_buf = "";
                post_buf_lock = false;
                request->send(400, "text/plain", "sesi lain sedang berlangsung");
                return;
            }

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, post_buf);
            post_buf = "";
            post_buf_lock = false;
            if (err) {
                request->send(400, "text/plain", "json tidak valid");
                return;
            }

            String username = doc["username"] | "";
            String password = doc["password"] | "";

            prefs.begin("admin", true);
            String storedUsername = prefs.getString("username", "");
            String storedHash = prefs.getString("passHash", "");
            prefs.end();

            bool usernameMatch = (username == storedUsername);
            bool passwordMatch = (hash_password(password) == storedHash);

            if (!usernameMatch || !passwordMatch) {
                request->send(401, "text/plain", "username/password salah");
                return;
            }

            session_token = generate_session_token();
            session_token_expiration = millis() + SESSION_TIMEOUT;

            JsonDocument resDoc;
            resDoc["token"] = session_token;
            resDoc["expiresIn"] = session_token_expiration;

            String out;
            serializeJson(resDoc, out);
            AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "ok");
            response->addHeader("Set-Cookie",
            "session=" + session_token + "; HttpOnly; Path=/; Max-Age=1800; SameSite=Strict");
            request->send(response);
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (post_buf_lock) {
                request->send(400, "text/plain", "sesi lain sedang berlangsung");
                return;
            }
            if (index == 0) post_buf = "";
            post_buf += String((char*)data).substring(0, len);
            post_buf_lock = true;
        }
    );

    server.on("/logout", HTTP_POST, require_auth_api([](AsyncWebServerRequest *request) {
        session_token = "";
        session_token_expiration = 0;
        request->send(200, "text/plain", "ok");
    }));

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!is_admin_configured()) {
            request->redirect("/setup");
            return;
        }
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "not found");
    });

    server.begin();
}