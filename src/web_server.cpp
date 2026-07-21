// web_server.cpp
#include "web_server.h"
#include "helpers.h"
#include "system.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>

AsyncWebServer server(80);

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
        if (!is_admin_configured()) {
            request->redirect("/setup");
            return;
        }
        if (!request_has_valid_session(request)) {
            request->redirect("/login");
            return;
        }
        handler(request);
    };
}

ArRequestHandlerFn require_auth_api(ArRequestHandlerFn handler) {
    return [handler](AsyncWebServerRequest *request) {
        if (!is_admin_configured()) {
            request->send(403, "text/plain", "admin belum dikonfigurasi");
            return;
        }
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

            if (set_admin_credentials(username, password)) {
                request->send(400, "text/plain", "gagal menyimpan kredensial");
            }

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

            String storedUsername;
            String storedHash;

            if (get_admin_username(storedUsername) || get_admin_password_hash(storedHash)) {
                request->send(400, "text/plain", "autentikasi gagal, mohon coba lagi");
                return;
            }

            prefs.begin("admin", true);
            String storedUsername = prefs.getString("username", "");
            String storedHash = prefs.getString("passHash", "");
            prefs.end();

            bool usernameMatch = constant_time_equal(username, storedUsername);
            bool passwordMatch = constant_time_equal(hash_password(password),storedHash);

            if (!usernameMatch || !passwordMatch) {
                request->send(401, "text/plain", "username/password salah");
                return;
            }

            session_token = generate_session_token();
            session_token_expiration = millis() + SESSION_TIMEOUT;

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

    server.on("/logout", HTTP_POST, 
        require_auth_api([](AsyncWebServerRequest *request) {
        session_token = "";
        session_token_expiration = 0;
        request->send(200, "text/plain", "ok");
    }));

    server.on("/wifi", HTTP_GET, require_auth_page([](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/wifi.html", "text/html");
    }));

    server.on("/wifi", HTTP_POST,
        require_auth_api([](AsyncWebServerRequest *request) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, post_buf);
            post_buf = "";
            post_buf_lock = false;
            if (err) {
                request->send(400, "text/plain", "json tidak valid");
                return;
            }

            String ssid = doc["ssid"] | "";
            String password = doc["password"] | "";

            if (ssid.length() < 1 || ssid.length() > 32) {
                request->send(400, "text/plain", "SSID tidak valid");
                return;
            }

            if (password.length() < 8 || password.length() > 64) {
                request->send(400, "text/plain", "password tidak valid");
                return;
            }

            if (set_wifi_credentials(ssid, password)) {
                request->send(400, "text/plain", "gagal menyimpan password. mohon coba lagi");
                return;
            }

            request->send(200, "text/plain", "ok");
        }),
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

    server.on("/blynk", HTTP_GET, require_auth_page([](AsyncWebServerRequest *request) {
        if (!is_admin_configured()) {
            request->redirect("/setup");
            return;
        }
        request->send(LittleFS, "/blynk.html", "text/html");
    }));

    server.on("/blynk", HTTP_POST,
        require_auth_api([](AsyncWebServerRequest *request) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, post_buf);
            post_buf = "";
            post_buf_lock = false;
            if (err) {
                request->send(400, "text/plain", "json tidak valid");
                return;
            }

            String auth_token = doc["auth_token"] | "";

            if(set_blynk_credentials(auth_token)) {
                request->send(400, "text/plain", "gagal menyimpan token");
                return;
            }

            request->send(200, "text/plain", "ok");
        }),
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

    server.on("/ap", HTTP_GET, require_auth_page([](AsyncWebServerRequest *request) {
        if (!is_admin_configured()) {
            request->redirect("/setup");
            return;
        }
        request->send(LittleFS, "/ap.html", "text/html");
    }));

    server.on("/ap", HTTP_POST,
        require_auth_api([](AsyncWebServerRequest *request) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, post_buf);
            post_buf = "";
            post_buf_lock = false;
            if (err) {
                request->send(400, "text/plain", "json tidak valid");
                return;
            }

            String ssid = doc["ssid"] | "";
            String password = doc["password"] | "";

            if (set_ap_credentials(ssid, password)) {
                request->send(400, "text/plain", "gagal menyimpan kredensial ap. mohon coba lagi");
                return;
            }

            request->send(200, "text/plain", "ok");
        }),
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

    server.on("/", HTTP_GET, require_auth_page([](AsyncWebServerRequest *request) {
        if (!is_admin_configured()) {
            request->redirect("/setup");
            return;
        }

        if (!is_wifi_configured()) {
            request->redirect("/wifi");
            return;
        }

        if (!is_blynk_configured()) {
            request->redirect("/blynk");
            return;
        }

        if (!is_ap_configured()) {
            request->redirect("/ap");
            return;
        }

        request->send(LittleFS, "/index.html", "text/html");
    }));

    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "not found");
    });

    server.begin();
}