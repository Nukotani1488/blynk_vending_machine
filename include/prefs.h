#pragma once
#include <Preferences.h>
#include <Arduino.h>

class PrefsBackedStruct;

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
void PrefsBackedStruct::read(const char* key, T& value)
{
    prefs.begin(ns, true);
    prefs.getBytes(key, &value, sizeof(T));
    prefs.end();
}


template<typename T>
void PrefsBackedStruct::write(const char* key, const T& value)
{
    prefs.begin(ns, false);
    prefs.putBytes(key, &value, sizeof(T));
    prefs.end();
}

template<>
void PrefsBackedStruct::read<String>(const char* key, String& value)
{
    prefs.begin(ns, true);
    value = prefs.getString(key, "");
    prefs.end();
}


template<>
void PrefsBackedStruct::write<String>(const char* key, const String& value)
{
    prefs.begin(ns, false);
    prefs.putString(key, value);
    prefs.end();
}

template<>
void PrefsBackedStruct::read<uint8_t>(const char* key, uint8_t& value)
{
    prefs.begin(ns, true);
    value = prefs.getUChar(key, value);
    prefs.end();
}

template<>
void PrefsBackedStruct::write<uint8_t>(const char* key, const uint8_t& value)
{
    prefs.begin(ns, false);
    prefs.putUChar(key, value);
    prefs.end();
}

template<>
void PrefsBackedStruct::read<uint32_t>(const char* key, uint32_t& value)
{
    prefs.begin(ns, true);
    value = prefs.getULong(key, value);
    prefs.end();
}

template<>
void PrefsBackedStruct::write<uint32_t>(const char* key, const uint32_t& value)
{
    prefs.begin(ns, false);
    prefs.putULong(key, value);
    prefs.end();
}


template<typename T>
T& PrefsBackedMember<T>::get()
{
    if (!loaded)
    {
        parent.read(key, cache);
        loaded = true;
    }

    return cache;
}


template<typename T>
void PrefsBackedMember<T>::save()
{
    parent.write(key, cache);
}