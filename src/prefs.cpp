#include "prefs.h"
#include "storage.h"
#include "config.h"

void PrefsBackedStructMemberBase::register_member(const char* key) {
    parent->add_member(*this, key);
}

void PrefsBackedStructMemberBase::mark_parent_dirty()
{
    parent->mark_dirty();
}

template<typename T>
void* PrefsBackedStructMember<T>::data() {
    return &value;
}

template<typename T>
size_t PrefsBackedStructMember<T>::size() const {
    return sizeof(T);
}

template<typename T>
PrefsBackedStructMember<T>::PrefsBackedStructMember(PrefsBackedStructBase& parent,const char* key)
    : parent(&parent) {
    register_member(key);
}

template<typename T>
PrefsBackedStructMember<T>::operator T() const {
    return value;
}

template<typename T>
PrefsBackedStructMember<T>& PrefsBackedStructMember<T>::operator=(const T& v) {
    value = v;
    mark_parent_dirty();
    return *this;
}

template<size_t capacity>
bool PrefsBackedStruct<capacity>::is_dirty() const {
    return dirty;
}

template<size_t capacity>
void PrefsBackedStruct<capacity>::mark_dirty() {
    dirty = true;
}

template<size_t capacity>
void PrefsBackedStruct<capacity>::add_member(PrefsBackedStructMemberBase& member, const char* key) {
    if (size >= capacity) {
        // handle error
        return;
    }
    
    member_keys[size] = key;
    members[size] = &member;
    size++;
}

template<size_t capacity>
void PrefsBackedStruct<capacity>::load() {
    if (loaded) {
        return;
    }

    prefs.begin(ns, true);
    for (size_t i = 0; i < size; i++) {
        prefs.getBytes(
            member_keys[i],
            members[i]->data(),
            members[i]->size()
        );
    }
    prefs.end();

    loaded = true;
}

template<size_t capacity>
void PrefsBackedStruct<capacity>::flush() {
    bool success = true;
    if (!dirty) {
        return;
    }

    if (!prefs.begin(ns, false)) {
        return;
    }

    for (size_t i = 0; i < size; i++) {
        if (prefs.putBytes(
            member_keys[i],
            members[i]->data(),
            members[i]->size()
        ) != members[i]->size()) {
            success = false;
        }
    }
    prefs.end();

    if (success) {
        dirty = false;
    }
}

template<size_t capacity>
PrefsBackedStruct<capacity>::PrefsBackedStruct(Preferences& prefs, const char* ns)
    : prefs(prefs), ns(ns)
{
    prefs.begin(ns, false);
    prefs.end();
}

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