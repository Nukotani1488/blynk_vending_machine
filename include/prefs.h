#pragma once
#include <Preferences.h>
#include <Arduino.h>


class PrefsBackedStruct
{
protected:
    Preferences& prefs;
    const char* ns;

public:
    PrefsBackedStruct(Preferences& prefs, const char* ns)
        : prefs(prefs), ns(ns)
    {
    }

    template<typename T>
    void read(const char* key, T& value);

    template<typename T>
    void write(const char* key, const T& value);
};

template<typename T>
class PrefsBackedMember
{
    PrefsBackedStruct& parent;
    const char* key;

    T cache{};
    bool loaded = false;

public:
    PrefsBackedMember(PrefsBackedStruct& parent, const char* key)
        : parent(parent), key(key)
    {
    }

    T& get();

    operator T&()
    {
        return get();
    }

    PrefsBackedMember& operator=(const T& value)
    {
        get() = value;
        save();
        return *this;
    }

    void save();
};