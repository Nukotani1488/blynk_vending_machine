#include "web_server.h"
#include "helpers.h"
#include "system.h"
#include "price.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <esp_random.h>

AsyncWebServer server(80);

const size_t MAX_POST_SIZE = 1024;

static String post_buf = "";
static AsyncWebServerRequest *post_owner = nullptr;

static AsyncWebServerRequest *post_failed_request = nullptr;

static uint32_t post_start_time = 0;
const uint32_t POST_TIMEOUT = 5000;
static bool post_timed_out = false;

void release_post_buffer(AsyncWebServerRequest *request) {
    if (post_owner == request || post_timed_out) {
        post_buf = "";
        post_owner = nullptr;
        post_start_time = 0;
    }

    if (post_failed_request == request) {
        post_failed_request = nullptr;
    }
}

bool append_post_data(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total
) {
    if (index == 0) {
        if (post_owner != nullptr) {
            post_failed_request = request;
            request->send(429, "text/plain", "another request is being processed");
            return false;
        }

        if (total > MAX_POST_SIZE) {
            post_failed_request = request;
            request->send(413, "text/plain", "request too large");
            return false;
        }

        post_owner = request;
        post_buf = "";
        post_buf.reserve(total);
        post_start_time = millis();
    }

    if ((uint32_t)(millis() - post_start_time) > POST_TIMEOUT) {
        post_timed_out = true;
        release_post_buffer(request);
        post_timed_out = false;
        request->send(408, "text/plain", "request timeout");
        post_failed_request = request;
        return false;
    }
    
    if (post_owner != request) {
        post_failed_request = request;
        return false;
    }

    if (post_buf.length() + len > MAX_POST_SIZE) {
        release_post_buffer(request);
        request->send(413, "text/plain", "request too large");
        post_failed_request = request;
        return false;
    }

    post_buf.concat((const char *)data, len);

    return true;
}

static String session_token = "";
static uint32_t session_token_expiration = 0;
const uint32_t SESSION_TIMEOUT = 30UL * 60 * 1000;


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

    if ((int32_t)(millis() - session_token_expiration) > 0) {
        session_token = "";
        session_token_expiration = 0;
        return false;
    }

    return true;
}

bool is_session_valid(const String& token) {
    return is_session_occupied() && constant_time_equal(token, session_token);
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
    return is_session_valid(get_cookie_value(request, "session"));
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
            release_post_buffer(request);
            request->send(403, "text/plain", "admin belum dikonfigurasi");
            return;
        }
        if (!request_has_valid_session(request)) {
            release_post_buffer(request);
            request->send(401, "text/plain", "tidak terautentikasi");
            return;
        }
        handler(request);
    };
}


void start_web_server() {
    if (!LittleFS.begin(true)) {
        Serial.println("gagal mount LittleFS");
        return;
    }

    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (is_admin_configured()) {
            request->redirect("/");
            return;
        }
        request->send(LittleFS, "/setup.html", "text/html");
    });

    server.on("/setup", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (post_failed_request == request) {
                post_failed_request = nullptr;
                return;
            }
            if (is_admin_configured()) {
                release_post_buffer(request);
                request->send(403, "text/plain", "telah dikonfigurasi");
                return;
            }

            if (is_session_occupied()) {
                release_post_buffer(request);
                request->send(400, "text/plain", "sesi lain sedang berlangsung");
                return;
            }

            String body = post_buf;
            release_post_buffer(request);

            JsonDocument doc;
            auto err = deserializeJson(doc, body);


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

            if (!set_admin_credentials(username, password)) {
                request->send(400, "text/plain", "gagal menyimpan kredensial");
                return;
            }

            request->send(200, "text/plain", "ok");
        },
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            append_post_data(request, data, len, index, total);
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
            if (post_failed_request == request) {
                post_failed_request = nullptr;
                return;
            }
            if (!is_admin_configured()) {
                release_post_buffer(request);
                request->redirect("/setup");
                return;
            }

            if (is_session_occupied()) {
                release_post_buffer(request);
                request->send(400, "text/plain", "sesi lain sedang berlangsung");
                return;
            }

            String body = post_buf;
            release_post_buffer(request);

            JsonDocument doc;
            auto err = deserializeJson(doc, body);

            if (err) {
                request->send(400, "text/plain", "json tidak valid");
                return;
            }

            String username = doc["username"] | "";
            String password = doc["password"] | "";

            String storedUsername;
            String storedHash;

            if (!get_admin_username(storedUsername) || !get_admin_password_hash(storedHash)) {
                request->send(400, "text/plain", "autentikasi gagal, mohon coba lagi");
                return;
            }

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
            append_post_data(request, data, len, index, total);
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
            if (post_failed_request == request) {
                post_failed_request = nullptr;
                return;
            }
            String body = post_buf;
            release_post_buffer(request);

            JsonDocument doc;
            auto err = deserializeJson(doc, body);

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

            if (password.length() != 0 && (password.length() < 8 || password.length() > 63)) {
                request->send(400, "text/plain", "password tidak valid");
                return;
            }

            if (!set_wifi_credentials(ssid, password)) {
                request->send(400, "text/plain", "gagal menyimpan password. mohon coba lagi");
                return;
            }

            request->send(200, "text/plain", "ok");
        }),
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            append_post_data(request, data, len, index, total);
        }
    );

    server.on("/blynk", HTTP_GET, require_auth_page([](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/blynk.html", "text/html");
    }));

    server.on("/blynk", HTTP_POST,
        require_auth_api([](AsyncWebServerRequest *request) {
            Serial.println("Received blynk request");
            if (post_failed_request == request) {
                post_failed_request = nullptr;
                return;
            }

            String body = post_buf;
            release_post_buffer(request);

            JsonDocument doc;
            auto err = deserializeJson(doc, body);

            if (err) {
                request->send(400, "text/plain", "json tidak valid");
                return;
            }

            String token = doc["token"] | "";

            if (!set_blynk_credentials(token)) {
                request->send(400, "text/plain", "gagal menyimpan token");
                return;
            }

            request->send(200, "text/plain", "ok");
        }),
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            append_post_data(request, data, len, index, total);
        }
    );

    server.on("/ap", HTTP_GET, require_auth_page([](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/ap.html", "text/html");
    }));

    server.on("/ap", HTTP_POST,
        require_auth_api([](AsyncWebServerRequest *request) {
            if (post_failed_request == request) {
                post_failed_request = nullptr;
                return;
            }

            String body = post_buf;
            release_post_buffer(request);

            JsonDocument doc;
            auto err = deserializeJson(doc, body);

            if (err) {
                request->send(400, "text/plain", "json tidak valid");
                return;
            }

            String ssid = doc["ssid"] | "";
            String password = doc["password"] | "";

            if (!set_ap_credentials(ssid, password)) {
                request->send(400, "text/plain", "gagal menyimpan kredensial ap. mohon coba lagi");
                return;
            }

            request->send(200, "text/plain", "ok");
        }),
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            append_post_data(request, data, len, index, total);
        }
    );

    server.on("/harga", HTTP_GET, require_auth_page([](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/price.html", "text/html",false, [](const String& var) -> String {

            if (!var.startsWith("PRICE_")) {
                    return String();
                }

                uint8_t slot = atoi(var.substring(6).c_str());

                if (slot >= SLOT_COUNT) {
                    return String("0");
                }

                uint32_t price;

                if (!get_price(slot, price)) {
                    return String("0");
                }

                return String(price);
            });
        })
    );

    server.on("/harga", HTTP_POST, require_auth_api([](AsyncWebServerRequest *request) {
        if (post_failed_request == request) {
            post_failed_request = nullptr;
            return;
        }

        String body = post_buf;
        release_post_buffer(request);

        JsonDocument doc;

        auto err = deserializeJson(doc, body);

        if (err) {
            request->send(400, "text/plain", "json tidak valid");
            return;
        }

        JsonArray prices = doc["prices"].as<JsonArray>();

        if (prices.size() != SLOT_COUNT) {
            request->send(400, "text/plain", "jumlah harga tidak sesuai");
            return;
        }

        for (uint8_t i = 0; i < SLOT_COUNT; i++) {

        if (!prices[i].is<uint32_t>()) {
            request->send(400, "text/plain", "harga tidak valid");
                return;
            }

            uint32_t price = prices[i].as<uint32_t>();

            if (!set_price(i, price)) {
                request->send(500, "text/plain", "gagal menyimpan harga");
                return;
            }
        }

        request->send(200, "text/plain", "ok");
    }),
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        append_post_data(request, data, len, index, total);
    });

    server.on("/", HTTP_GET, require_auth_page([](AsyncWebServerRequest *request) {
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