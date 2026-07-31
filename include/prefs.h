#pragma once

#include <Preferences.h>
#include <Arduino.h>
#include "storage.h"

class PrefsBackedStructBase;

class PrefsBackedStructMemberBase
{
    friend class PrefsBackedStructBase;

protected:
    PrefsBackedStructBase* parent;
    virtual void* data() = 0;
    virtual size_t size() const = 0;

    void register_member(const char*);
    void mark_parent_dirty();

public:
    virtual ~PrefsBackedStructMemberBase() = default;
};

template<typename T>
class PrefsBackedStructMember : public PrefsBackedStructMemberBase
{
    T value;

protected:
    void* data() override;
    size_t size() const override;

public:
    PrefsBackedStructMember(PrefsBackedStructBase&, const char*);
    operator T() const;

    PrefsBackedStructMember& operator=(const T& v);
};

class PrefsBackedStructBase : public StorageBackedObject
{
    friend class PrefsBackedStructMemberBase;
protected:
    virtual void mark_dirty() = 0;
    virtual void add_member(PrefsBackedStructMemberBase&, const char*) = 0;

public:
    virtual ~PrefsBackedStructBase() = default;
};

template<size_t capacity>
class PrefsBackedStruct : public PrefsBackedStructBase
{
protected:
    Preferences& prefs;
    const char* ns;

    size_t size = 0;
    const char* member_keys[capacity]{};
    PrefsBackedStructMemberBase* members[capacity] {};

    bool dirty = false;
    bool loaded = false;

    bool is_dirty() const override;
    void flush() override;
    
    void mark_dirty() override;
    void add_member(PrefsBackedStructMemberBase& member, const char* key) override;

    void load();

public:
    PrefsBackedStruct(Preferences& prefs, const char* ns);
};


template<typename T, size_t capacity>
class PrefsBackedArray;

template<typename T>
class PrefsBackedArrayProxy
{
    PrefsBackedArray<T>& parent;
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