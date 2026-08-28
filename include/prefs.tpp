inline void ensure_nvs_initialized() {
    static bool initialized = false;
    if (initialized) return;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    initialized = true;
}

inline void PrefsBackedStructMemberBase::register_member() {
    parent.add_member(*this);
}

inline void PrefsBackedStructMemberBase::mark_parent_dirty()
{
    parent.mark_dirty();
}

inline void PrefsBackedStructMemberBase::mark_unchanged() {
    changed = false;
}

inline void PrefsBackedStructMemberBase::mark_changed() {
    changed = true;
}

inline bool PrefsBackedStructMemberBase::is_changed() {
    return changed;
}

template<typename T>
inline void* PrefsBackedStructMember<T>::data() {
    return &value;
}

template<typename T>
inline size_t PrefsBackedStructMember<T>::size() const {
    return sizeof(T);
}

template<typename T>
inline PrefsBackedStructMember<T>::PrefsBackedStructMember(PrefsBackedStruct& parent, const char* key)
    : PrefsBackedStructMemberBase(parent), key(key) {
    ets_printf("PrefsBackedStructMember ctor: %s\n", key);
    register_member();
}

template<typename T>
inline void PrefsBackedStructMember<T>::load() {
    parent.prefs().getBytes(key, (void*)&value, sizeof(T));
}

template<typename T>
inline bool PrefsBackedStructMember<T>::flush() {
    return parent.prefs().putBytes(key, (void *)&value, sizeof(T)) == sizeof(T);
}

template<>
inline void PrefsBackedStructMember<String>::load() {
    value = parent.prefs().getString(key, String());
}

template<>
inline bool PrefsBackedStructMember<String>::flush() {
    size_t written = parent.prefs().putString(key, value);
    return value.isEmpty() ? written == 0 : written > 0;
}

template<typename T>
inline PrefsBackedStructMember<T>::operator T() const {
    return value;
}

template<typename T>
inline bool PrefsBackedStructMember<T>::operator!=(const T& other) const {
    return value != other;
}

template<typename T>
inline bool PrefsBackedStructMember<T>::operator==(const T& other) const {
    return value == other;
}

template<typename T>
inline PrefsBackedStructMember<T>& PrefsBackedStructMember<T>::operator=(const T& v) {
    if (v == value) return *this;
    value = v;
    mark_changed();
    mark_parent_dirty();
    return *this;
}

inline bool PrefsBackedStruct::is_dirty() const {
    return dirty;
}

inline void PrefsBackedStruct::mark_dirty() {
    dirty = true;
}

inline Preferences& PrefsBackedStruct::prefs() {
    return prefs_handler;
}

inline void PrefsBackedStruct::add_member(PrefsBackedStructMemberBase& member) {
    members.push_back(&member);
}

inline void PrefsBackedStruct::load() {
    if (loaded) {
        return;
    }

    for (size_t i = 0; i < members.size(); i++) {
        /*prefs.getBytes(
            member_keys[i],
            members[i]->data(),
            members[i]->size()
        );*/
        members[i]->load();
    }

    loaded = true;
}

inline void PrefsBackedStruct::flush() {
    bool success = true;
    if (!dirty) {
        return;
    }

    for (size_t i = 0; i < members.size(); i++) {
        /*if (prefs.putBytes(
            member_keys[i],
            members[i]->data(),
            members[i]->size()
        ) != members[i]->size()) {
            success = false;
        }*/
        if (!members[i]->flush()) {
            success = false;
        }
    }

    if (success) {
        dirty = false;
    }
}

inline PrefsBackedStruct::PrefsBackedStruct(const char* ns)
    : ns(ns)
{
    ets_printf("PrefsBackedStruct ctor: %s\n", ns);
    ensure_nvs_initialized();
    prefs_handler.begin(ns, false);
}

inline PrefsBackedStruct::~PrefsBackedStruct()
{
    prefs_handler.end();
}

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