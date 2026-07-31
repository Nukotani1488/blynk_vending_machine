#include "storage.h"

StorageManager& get_storage_manager()
{
    static StorageManager manager;
    return manager;
}

void StorageManager::add_object(StorageBackedObject& object) {
    // TODO: handle full manager
    managed_objects[size++] = &object;
}

void StorageManager::flush_all() const {
    for (size_t i = 0; i < size; i++) {
        if (managed_objects[i]->is_dirty()) {
            managed_objects[i]->flush();
        }
    }
}

StorageBackedObject::StorageBackedObject() {
    get_storage_manager().add_object(*this);
}

void storage_flusher_task(void* pvParameters) {
    const TickType_t flush_interval = pdMS_TO_TICKS(STORAGE_FLUSH_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, flush_interval);
        get_storage_manager().flush_all();
    }
}

bool start_storage_subsystem() {
    BaseType_t task_created = xTaskCreatePinnedToCore(
        storage_flusher_task,
        "Storage Flusher",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    return task_created == pdPASS;
}