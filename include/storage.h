#pragma once

#include <Arduino.h>
#include "config.h"

class StorageBackedObject;

class StorageManager
{
    StorageBackedObject* managed_objects[STORAGE_MANAGER_CAPACITY]{};
    size_t size = 0;

public:
    void add_object(StorageBackedObject& object);
    void flush_all() const;
};

StorageManager& get_storage_manager();

class StorageBackedObject
{
    friend class StorageManager;

protected:
    virtual bool is_dirty() const = 0;
    virtual void flush() = 0;

    StorageBackedObject();

public:
    virtual ~StorageBackedObject() = default;
};

bool start_storage_subsystem();