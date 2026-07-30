#include "logging.h"
#include "system.h"
#include "helpers.h"
#include "config.h"

#include <LittleFS.h>

#define LEVEL_SHIFT   0
#define TYPE_SHIFT    2
#define EVENT_SHIFT   4
#define CONTENT_SHIFT 7

#define LEVEL_INFO      (0 << LEVEL_SHIFT)
#define LEVEL_WARN      (1 << LEVEL_SHIFT)
#define LEVEL_ERROR     (2 << LEVEL_SHIFT)
#define LEVEL_CRITICAL  (3 << LEVEL_SHIFT)

#define TYPE_NETWORK    (0 << TYPE_SHIFT)
#define TYPE_PAYMENT    (1 << TYPE_SHIFT)
#define TYPE_SYSTEM     (2 << TYPE_SHIFT)
#define TYPE_WEB        (3 << TYPE_SHIFT)

#define EVENT_UNKNOWN   0

#define LOG_HAS_CONTENT (1 << CONTENT_SHIFT)

#define INFO_ENABLED        1 << 0
#define WARN_ENABLED        1 << 1
#define ERROR_ENABLED       1 << 2
#define CRITICAL_ENABLED    1 << 3

struct {
    LazyValue<uint32_t> flash_counter{fetch_log_counter, store_log_counter, 0};
    uint32_t counter = 0;
    LazyValue<uint8_t> level{fetch_log_level, store_log_level, 0};
} logger_state;
//SemaphoreHandle_t logger_state_mutex;

struct {
    LogEntry entries[LOG_BUFFER_MAX_SIZE] = {};
    uint32_t size = {};
} write_buffer = {};
SemaphoreHandle_t write_buffer_mutex;

struct {
    LogEntry entries[LOG_BUFFER_MAX_SIZE] = {};
    uint32_t head = 0;
    uint32_t size = 0;
    uint32_t tail = 0;
} read_buffer = {};

bool create_log_entry(uint8_t level, uint8_t type, uint8_t event, int32_t* content, LogEntry& buf) {
    uint8_t current_level;
    if (!logger_state.level.get(current_level)) {
        return false;
    }

    if ((current_level & (1 << level)) == 0) {
        return false;
    }

    LogEntry new_entry;
    uint8_t new_header = 0;
    new_entry.timestamp = millis();
    new_entry.id = logger_state.counter;
    logger_state.counter++;

    if (content != nullptr) {
        new_entry.content = *content;
        new_header |= LOG_HAS_CONTENT;
    }
    new_header |= level << LEVEL_SHIFT;
    new_header |= type << TYPE_SHIFT;
    new_header |= event << EVENT_SHIFT;
    new_entry.header = new_header;

    buf = new_entry;
    return true;
}

bool store_logs(LogEntry* entries, uint32_t count) {
    //buka file logs.bin dalam mode append
    File file = LittleFS.open("/logs.bin", "a");

    //kembali jika gagal membuka file
    if (!file) {
        return false;
    }

    //besar semua entri yang akan ditulis dalam byte
    size_t to_write = sizeof(LogEntry) * count;
    size_t written = file.write((uint8_t*)entries, to_write);

    //tutup file
    file.close();

    //cek apakah entri-entri berhasil tertulis di file
    return written == to_write;
}

bool flush_write_buffer() {
    //jangan eksekusi jika write_buffer kosong
    if (write_buffer.size == 0) {
        return true;
    }

    //tulis seluruh entri yang ada di write_buffer ke flash
    if (!store_logs(write_buffer.entries, write_buffer.size)) {
        return false;
    }

    logger_state.flash_counter.set(write_buffer.entries[0].id + write_buffer.size);

    //reset banyak entri pada write_buffer ke 0
    write_buffer.size = 0;

    return true;
}

bool write_log(uint8_t level, uint8_t type, uint8_t event, int32_t* content) {
    if (xSemaphoreTake(write_buffer_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    //buat entri baru dari parameter
    LogEntry entry;
    if (!create_log_entry(level, type, event, content, entry)) {
        xSemaphoreGive(write_buffer_mutex);
        return false;
    }
    
    //tulis seluruh entri pada write_buffer ke flash jika write_buffer penuh
    if (write_buffer.size >= LOG_BUFFER_MAX_SIZE) {
        if(!flush_write_buffer()) {
            xSemaphoreGive(write_buffer_mutex);
            return false;
        }
    }

    //masukkan entri baru ke write_buffer
    write_buffer.entries[write_buffer.size] = entry;
    write_buffer.size++;
    
    //tulis semua entri pada write_buffer ke flash jika level log baru adalah error atau critical
    //semua entri sebelum log baru tersebut ditulis juga untuk menjaga urutan entri pada flash
    if (level == LOG_ERROR || level == LOG_CRITICAL) {
        bool ok = flush_write_buffer();
        xSemaphoreGive(write_buffer_mutex);
        return ok;
    }

    xSemaphoreGive(write_buffer_mutex);
    return true;
}

bool copy_from_read_buffer(LogEntry* dest, uint32_t count) {
    //jangan eksekusi jika count lebih besar dari banyak entri yang ada di read_buffer
    if (count > read_buffer.size) {
        return false;
    }

    //jumlah entri pada chunk pertama (setelah head sampai alamat memori paling akhir)
    uint32_t first_chunk = LOG_BUFFER_MAX_SIZE - read_buffer.head;

    //jalankan jika semua entri yang akan disalin ada di chunk pertama
    if (count <= first_chunk) {
        //salin seluruh entri yang ingin dibaca ke dest
        memcpy(dest, &read_buffer.entries[read_buffer.head], count * sizeof(LogEntry));
    //jalankan jika tidak semua entri yang akan disalin ada di chunk pertama
    } else {
        //salin semua entri pada chunk pertama ke dest
        memcpy(dest, &read_buffer.entries[read_buffer.head], first_chunk * sizeof(LogEntry));
        //salin seluruh entry yang ingin dibaca pada chunk kedua ke dest
        memcpy(dest + first_chunk, &read_buffer.entries[0], (count - first_chunk) * sizeof(LogEntry));
    }

    return true;
}

void push_front_to_read_buffer(const LogEntry* entries, uint32_t count) {
    if (count == 0 || count > LOG_BUFFER_MAX_SIZE) {
        return;
    }

    bool contiguous = (read_buffer.size > 0) &&
        (entries[count - 1].id == read_buffer.entries[read_buffer.head].id - 1);

    if (!contiguous) {
        memcpy(&read_buffer.entries[0], entries, count * sizeof(LogEntry));
        read_buffer.head = 0;
        read_buffer.tail = count - 1;
        read_buffer.size = count;
        return;
    }

    uint32_t space_left = LOG_BUFFER_MAX_SIZE - read_buffer.size;

    if (count > space_left) {
        return;
    }

    uint32_t new_head = (read_buffer.head + LOG_BUFFER_MAX_SIZE - count) % LOG_BUFFER_MAX_SIZE;
    uint32_t first_chunk = LOG_BUFFER_MAX_SIZE - new_head;

    if (count <= first_chunk) {
        memcpy(&read_buffer.entries[new_head], entries, count * sizeof(LogEntry));
    } else {
        memcpy(&read_buffer.entries[new_head], entries, first_chunk * sizeof(LogEntry));
        memcpy(&read_buffer.entries[0], entries + first_chunk, (count - first_chunk) * sizeof(LogEntry));
    }

    read_buffer.head = new_head;
    read_buffer.size += count;
}


void push_to_read_buffer(const LogEntry* entries, uint32_t count) {
    //jangan eksekusi jika count bernilai 0
    if (count == 0 || count > LOG_BUFFER_MAX_SIZE) {
        return;
    }

    //cek apakah entri yang akan disimpan ke read_buffer berurutan dengan entri-entri yang sudah ada di read_buffer
    bool contiguous = (read_buffer.size > 0) && (entries[0].id == read_buffer.entries[read_buffer.tail].id + 1);

    //hapus seluruh entri pada read_buffer jika tidak berurutan
    if (!contiguous) {
        read_buffer.head = 0;
        read_buffer.tail = 0;
        read_buffer.size = 0;
    }

    //hitung banyak ruang kosong pada read_buffer
    uint32_t space_left = LOG_BUFFER_MAX_SIZE - read_buffer.size;

    //kosongkan read_buffer jika ruang kosong lebih kecil daripada banyak entri yang akan disimpan ke read_buffer
    if (count > space_left) {
        uint32_t to_evict = count - space_left;
        read_buffer.head = (read_buffer.head + to_evict) % LOG_BUFFER_MAX_SIZE;
        read_buffer.size -= to_evict;
    }

    uint32_t start = (read_buffer.size == 0) ? read_buffer.head : (read_buffer.tail + 1) % LOG_BUFFER_MAX_SIZE;

    uint32_t first_chunk = LOG_BUFFER_MAX_SIZE - start;

    if (count <= first_chunk) {
        memcpy(&read_buffer.entries[start], entries, count * sizeof(LogEntry));
    } else {
        memcpy(&read_buffer.entries[start], entries, first_chunk * sizeof(LogEntry));
        memcpy(&read_buffer.entries[0], entries + first_chunk, (count - first_chunk) * sizeof(LogEntry));
    }

    read_buffer.tail = (start + count - 1) % LOG_BUFFER_MAX_SIZE;
    read_buffer.size = (read_buffer.size + count > LOG_BUFFER_MAX_SIZE) ? LOG_BUFFER_MAX_SIZE : read_buffer.size + count;
}

bool copy_from_read_buffer_range(LogEntry* dest, uint32_t offset, uint32_t count) {
    if (offset + count > read_buffer.size) {
        return false;
    }

    uint32_t start = (read_buffer.head + offset) % LOG_BUFFER_MAX_SIZE;
    uint32_t first_chunk = LOG_BUFFER_MAX_SIZE - start;

    if (count <= first_chunk) {
        memcpy(dest, &read_buffer.entries[start], count * sizeof(LogEntry));
    } else {
        memcpy(dest, &read_buffer.entries[start], first_chunk * sizeof(LogEntry));
        memcpy(dest + first_chunk, &read_buffer.entries[0], (count - first_chunk) * sizeof(LogEntry));
    }

    return true;
}

bool read_logs(LogEntry* buff, uint32_t count, uint32_t index) {
    if (count == 0) {
        return true;
    }

    if (xSemaphoreTake(write_buffer_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    uint32_t flash_counter_value;
    if (!logger_state.flash_counter.get(flash_counter_value)) {
        xSemaphoreGive(write_buffer_mutex);
        return false;
    }

    uint32_t end = index + count - 1;
    uint32_t current_id = index;
    uint32_t filled = 0;
    bool ok = true;

    while (current_id <= end) {
        uint32_t remaining = end - current_id + 1;

        // segment belongs to write_buffer (unflushed, newest data)
        if (current_id >= flash_counter_value) {
            uint32_t offset_in_wb = current_id - flash_counter_value;

            if (offset_in_wb >= write_buffer.size) {
                ok = false;
                break;
            }

            uint32_t available = write_buffer.size - offset_in_wb;
            uint32_t take = (available < remaining) ? available : remaining;

            memcpy(&buff[filled], &write_buffer.entries[offset_in_wb], take * sizeof(LogEntry));

            filled += take;
            current_id += take;
            continue;
        }

        // segment belongs to read_buffer cache
        if (read_buffer.size > 0 &&
            current_id >= read_buffer.entries[read_buffer.head].id &&
            current_id <= read_buffer.entries[read_buffer.tail].id) {

            uint32_t cache_start_id = read_buffer.entries[read_buffer.head].id;
            uint32_t offset_in_cache = current_id - cache_start_id;
            uint32_t last_cache_id = read_buffer.entries[read_buffer.tail].id;
            uint32_t take = (remaining < (last_cache_id - current_id + 1))
                            ? remaining
                            : (last_cache_id - current_id + 1);

            if (!copy_from_read_buffer_range(&buff[filled], offset_in_cache, take)) {
                ok = false;
                break;
            }

            filled += take;
            current_id += take;
            continue;
        }

        // segment must come from flash
        uint32_t flash_end = (end < flash_counter_value) ? end : flash_counter_value - 1;

        if (read_buffer.size > 0 &&
            read_buffer.entries[read_buffer.head].id > current_id &&
            (read_buffer.entries[read_buffer.head].id - 1) < flash_end) {
            flash_end = read_buffer.entries[read_buffer.head].id - 1;
        }

        uint32_t take = flash_end - current_id + 1;

        File file = LittleFS.open("/logs.bin", "r");
        if (!file) {
            ok = false;
            break;
        }

        size_t byte_offset = sizeof(LogEntry) * current_id;
        if (!file.seek(byte_offset)) {
            file.close();
            ok = false;
            break;
        }

        size_t to_read = sizeof(LogEntry) * take;
        size_t did_read = file.read((uint8_t*)&buff[filled], to_read);
        file.close();

        if (did_read != to_read) {
            ok = false;
            break;
        }

        bool extends_tail = (read_buffer.size == 0) ||
            (buff[filled].id == read_buffer.entries[read_buffer.tail].id + 1);

        bool extends_head = (read_buffer.size > 0) &&
            (buff[filled + take - 1].id == read_buffer.entries[read_buffer.head].id - 1);

        if (extends_tail) {
            push_to_read_buffer(&buff[filled], take);
        } else if (extends_head) {
            push_front_to_read_buffer(&buff[filled], take);
        }

        filled += take;
        current_id += take;
    }

    xSemaphoreGive(write_buffer_mutex);
    return ok;
}

void flusher_task(void* pvParameters) {
    const TickType_t flush_interval = pdMS_TO_TICKS(LOG_FLUSH_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, flush_interval);

        if (write_buffer.size > 0) {
            if (xSemaphoreTake(write_buffer_mutex, portMAX_DELAY) == pdTRUE) {
                flush_write_buffer();
                xSemaphoreGive(write_buffer_mutex);
            }
        }
    }
}

bool start_log_subsystem() {
    uint32_t flash_counter_value;
    if (logger_state.flash_counter.get(flash_counter_value)) {
        logger_state.counter = flash_counter_value;
    }

    write_buffer_mutex = xSemaphoreCreateMutex();
    //logger_state_mutex = xSemaphoreCreateMutex();

    if (write_buffer_mutex == NULL) {
        return false;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(
        flusher_task,
        "Flusher",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    return task_created == pdPASS;
}