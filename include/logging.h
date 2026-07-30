#pragma once

#include <stdint.h>

#define LOG_INFO        0
#define LOG_WARN        1     
#define LOG_ERROR       2    
#define LOG_CRITICAL    3

#define LOG_NETWORK     0
#define LOG_PAYMENT     1
#define LOG_SYSTEM      2
#define LOG_WEB         3

#define EVENT_UNKNOWN           0
#define EVENT_PAYMENT_FAILED    1

typedef struct __attribute__((packed)) {
    uint32_t timestamp;
    int32_t content;
    uint32_t id;
    uint8_t header;
} LogEntry;

bool start_log_subsystem();

bool set_log_level(uint8_t level);
bool get_log_level(uint8_t& level);

bool write_log(uint8_t level, uint8_t type, uint8_t event, int32_t* content = nullptr);
bool get_log(uint32_t id);
bool get_logs(uint8_t size, uint8_t offset, bool newest, LogEntry* buf);