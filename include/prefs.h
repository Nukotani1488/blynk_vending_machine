#pragma once

#include <Preferences.h>
#include <Arduino.h>
#include "storage.h"
#include "helpers.h"
#include <nvs_flash.h>
#include "esp32/rom/ets_sys.h"

class PrefsBackedStruct;

class PrefsBackedStructMemberBase
{
    friend class PrefsBackedStruct;
    
    bool changed = false;

protected:
    PrefsBackedStruct& parent;
    virtual void* data() = 0;
    virtual size_t size() const = 0;

    virtual void load() = 0;
    virtual bool flush() = 0;

    void register_member();
    void mark_parent_dirty();

    void mark_changed();

    PrefsBackedStructMemberBase(PrefsBackedStruct& parent) : parent(parent) {}

public:
    virtual ~PrefsBackedStructMemberBase() = default;
    void mark_unchanged();
    bool is_changed();
};

template<typename T>
class PrefsBackedStructMember : public PrefsBackedStructMemberBase
{
protected:
    T value;
    const char* key;
    //PrefsBackedStruct* parent;
    void* data() override;
    size_t size() const override;
    void load() override;
    bool flush() override;

public:
    PrefsBackedStructMember(PrefsBackedStruct&, const char*);
    
    operator T() const;
    PrefsBackedStructMember& operator=(const T& v);
    bool operator==(const T& other) const;
    bool operator!=(const T& other) const;
};

class PrefsBackedStruct : public StorageBackedObject
{
    template<typename T>
    friend class PrefsBackedStructMember;
    friend class PrefsBackedStructMemberBase;

protected:
    const char* ns;
    Preferences prefs_handler;

    IncrementalVector<PrefsBackedStructMemberBase*> members;

    bool dirty = false;
    bool loaded = false;

    bool is_dirty() const override;
    void flush() override;
    
    void mark_dirty();
    void add_member(PrefsBackedStructMemberBase& member);
    Preferences& prefs();

    void load();

public:
    PrefsBackedStruct(const char* ns);
    ~PrefsBackedStruct();
};

/*
template<typename T, size_t capacity>
class PrefsBackedArray;

template<typename T>
class PrefsBackedArrayProxy
{
    PrefsBackedArray<T, capacity>& parent;
    size_t index;

public:
    PrefsBackedArrayProxy(PrefsBackedArray<T>& parent, size_t index)
        : parent(parent), index(index)
    {
    }

    operator T() const
    {
        return parent.arr[index];
    }

    PrefsBackedArrayProxy& operator=(const T& value)
    {
        parent.arr[index] = value;
        parent.dirty = true;
        return *this;
    }

    PrefsBackedArrayProxy& operator+=(const T& value)
    {
        parent.arr[index] += value;
        parent.dirty = true;
        return *this;
    }

    PrefsBackedArrayProxy& operator-=(const T& value)
    {
        parent.arr[index] -= value;
        parent.dirty = true;
        return *this;
    }

    PrefsBackedArrayProxy& operator*=(const T& value)
    {
        parent.arr[index] *= value;
        parent.dirty = true;
        return *this;
    }

    PrefsBackedArrayProxy& operator/=(const T& value)
    {
        parent.arr[index] /= value;
        parent.dirty = true;
        return *this;
    }

    T& get()
    {
        return parent.arr[index];
    }
};


template<typename T, size_t capacity>
class PrefsBackedArray : StorageBackedObject
{
    friend class PrefsBackedArrayProxy<T>;

    Preferences& prefs;
    const char* ns;

    size_t size = 0;
    T arr[capacity]{};
    bool dirty = false;

public:
    PrefsBackedArrayProxy<T> operator[](size_t index)
    {
        return PrefsBackedArrayProxy<T>(*this, index);
    }

    T operator[](size_t index) const
    {
        return arr[index];
    }
    
    void clear_dirty() const overrides
    {
        dirty = false;
    }

    bool is_dirty() const override
    {
        return dirty;
    }
    
    void flush() override
    {
        if (!dirty)
            return;

        prefs.begin(ns, false);

        prefs.putUInt("size", size);
        prefs.putBytes("data", arr, sizeof(T) * size);

        prefs.end();

        dirty = false;
    }

protected:
    void load()
    {
        if (loaded)
            return;

        prefs.begin(ns, true);

        size = prefs.getUInt("size", 0);

        if (size > capacity)
            size = capacity;

        prefs.getBytes(
            key,
            arr,
            sizeof(T) * size
        );

        prefs.end();

        loaded = true;
    }
};
*/
#include "prefs.tpp"