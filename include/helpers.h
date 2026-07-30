#pragma once

#include <Arduino.h>

#define BITS_FOR_SIZE(size) (32 - __builtin_clz((size) - 1))
#define EXTRACT_LOW_BITS(value, size) ((value) & ((1u << BITS_FOR_SIZE(size)) - 1))
#define BIT_MASK(width, shift) (((1U << (width)) - 1) << (shift))

bool constant_time_equal(const String& a, const String& b);
String hash_password(const String& password);
bool verify_admin_password(const String& password, const String& hash);

template<typename T>
class LazyValue {
public:
    using LoadFn = bool(*)(T&);
    using SaveFn = bool(*)(T);

    LazyValue(LoadFn loader, SaveFn saver, T default_value = T())
        : loader_(loader), saver_(saver), value_(default_value), loaded_(false) {}

    bool get(T& out) {
        if (!loaded_) {
            if (!loader_(value_)) {
                return false;
            }
            loaded_ = true;
        }
        out = value_;
        return true;
    }

    bool set(const T& value) {
        if (!saver_(value)) {
            return false;
        }
        value_ = value;
        loaded_ = true;
        return true;
    }

    void invalidate() {
        loaded_ = false;
    }

    bool is_loaded() const {
        return loaded_;
    }

private:
    LoadFn loader_;
    SaveFn saver_;
    T value_;
    bool loaded_;
};