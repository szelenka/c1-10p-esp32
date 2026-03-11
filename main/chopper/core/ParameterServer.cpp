#include "chopper/core/ParameterServer.h"
#include "esp_log.h"
#include <cstring>

#if defined(__has_include)
#if __has_include(<nvs.h>) && __has_include(<nvs_flash.h>)
#include <nvs.h>
#include <nvs_flash.h>
#define CHOPPER_HAS_NVS 1
#endif
#endif

#ifndef CHOPPER_HAS_NVS
#define CHOPPER_HAS_NVS 0
#endif

static const char* const TAG = "ParameterServer";

namespace chopper::core {

namespace {
constexpr uint32_t kSnapshotMagic = 0x43505356;  // "CPSV"
constexpr uint16_t kSnapshotVersion = 1;
constexpr const char* kNvsNamespace = "chopperps";
constexpr const char* kNvsKey = "param_blob";

struct PersistValue {
    int32_t i;
    float f;
    uint8_t b;
};

struct PersistEntry {
    char name[limits::MAX_PARAM_NAME_LEN];
    uint8_t type;
    PersistValue value;
};

struct PersistSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    PersistEntry entries[limits::MAX_PARAMETERS];
};

#if !CHOPPER_HAS_NVS
// Host-test fallback persistence backend.
static PersistSnapshot g_host_snapshot{};
static bool g_host_snapshot_valid = false;
#endif
}  // namespace

ParameterServer& ParameterServer::getInstance() {
    static ParameterServer instance;
    return instance;
}

ParameterServer::ParameterServer() {
    memset(params_, 0, sizeof(params_));
    memset(listeners_, 0, sizeof(listeners_));
}

size_t ParameterServer::findIndex(const char* name) const {
    for (size_t i = 0; i < count_; ++i) {
        if (params_[i].active && strcmp(params_[i].name, name) == 0) {
            return i;
        }
    }
    return count_;  // not found sentinel
}

// --- Declare ---

bool ParameterServer::declare(const char* name, int32_t default_val, int32_t min_val, int32_t max_val,
                              bool persistent) {
    if ((name == nullptr) || count_ >= limits::MAX_PARAMETERS) {
        ESP_LOGE(TAG, "Cannot declare param '%s': null name or table full", name ? name : "(null)");
        return false;
    }
    if (findIndex(name) < count_) {
        ESP_LOGW(TAG, "Parameter '%s' already declared", name);
        return false;
    }
    if (strlen(name) >= limits::MAX_PARAM_NAME_LEN) {
        ESP_LOGE(TAG, "Parameter name '%s' exceeds max length", name);
        return false;
    }

    Parameter& p = params_[count_];
    strncpy(p.name, name, limits::MAX_PARAM_NAME_LEN - 1);
    p.name[limits::MAX_PARAM_NAME_LEN - 1] = '\0';
    p.type = ParamType::INT32;
    p.value.i = default_val;
    p.min_value.i = min_val;
    p.max_value.i = max_val;
    p.has_range = (min_val != INT32_MIN || max_val != INT32_MAX);
    p.persistent = persistent;
    p.change_count = 0;
    p.active = true;
    ++count_;

    ESP_LOGI(TAG, "Declared int32 '%s' = %d", name, (int)default_val);
    return true;
}

bool ParameterServer::declare(const char* name, float default_val, float min_val, float max_val, bool persistent) {
    if ((name == nullptr) || count_ >= limits::MAX_PARAMETERS) {
        ESP_LOGE(TAG, "Cannot declare param '%s': null name or table full", name ? name : "(null)");
        return false;
    }
    if (findIndex(name) < count_) {
        ESP_LOGW(TAG, "Parameter '%s' already declared", name);
        return false;
    }
    if (strlen(name) >= limits::MAX_PARAM_NAME_LEN) {
        ESP_LOGE(TAG, "Parameter name '%s' exceeds max length", name);
        return false;
    }

    Parameter& p = params_[count_];
    strncpy(p.name, name, limits::MAX_PARAM_NAME_LEN - 1);
    p.name[limits::MAX_PARAM_NAME_LEN - 1] = '\0';
    p.type = ParamType::FLOAT;
    p.value.f = default_val;
    p.min_value.f = min_val;
    p.max_value.f = max_val;
    p.has_range = (min_val != -FLT_MAX || max_val != FLT_MAX);
    p.persistent = persistent;
    p.change_count = 0;
    p.active = true;
    ++count_;

    ESP_LOGI(TAG, "Declared float '%s' = %f", name, (double)default_val);
    return true;
}

bool ParameterServer::declare(const char* name, bool default_val, bool persistent) {
    if ((name == nullptr) || count_ >= limits::MAX_PARAMETERS) {
        ESP_LOGE(TAG, "Cannot declare param '%s': null name or table full", name ? name : "(null)");
        return false;
    }
    if (findIndex(name) < count_) {
        ESP_LOGW(TAG, "Parameter '%s' already declared", name);
        return false;
    }
    if (strlen(name) >= limits::MAX_PARAM_NAME_LEN) {
        ESP_LOGE(TAG, "Parameter name '%s' exceeds max length", name);
        return false;
    }

    Parameter& p = params_[count_];
    strncpy(p.name, name, limits::MAX_PARAM_NAME_LEN - 1);
    p.name[limits::MAX_PARAM_NAME_LEN - 1] = '\0';
    p.type = ParamType::BOOL;
    p.value.b = default_val;
    p.has_range = false;
    p.persistent = persistent;
    p.change_count = 0;
    p.active = true;
    ++count_;

    ESP_LOGI(TAG, "Declared bool '%s' = %s", name, default_val ? "true" : "false");
    return true;
}

// --- Get ---

bool ParameterServer::get(const char* name, int32_t& out) const {
    size_t idx = findIndex(name);
    if (idx >= count_) {
        return false;
    }
    if (params_[idx].type != ParamType::INT32) {
        return false;
    }
    out = params_[idx].value.i;
    return true;
}

bool ParameterServer::get(const char* name, float& out) const {
    size_t idx = findIndex(name);
    if (idx >= count_) {
        return false;
    }
    if (params_[idx].type != ParamType::FLOAT) {
        return false;
    }
    out = params_[idx].value.f;
    return true;
}

bool ParameterServer::get(const char* name, bool& out) const {
    size_t idx = findIndex(name);
    if (idx >= count_) {
        return false;
    }
    if (params_[idx].type != ParamType::BOOL) {
        return false;
    }
    out = params_[idx].value.b;
    return true;
}

// --- Set ---

bool ParameterServer::set(const char* name, int32_t value) {
    size_t idx = findIndex(name);
    if (idx >= count_) {
        ESP_LOGW(TAG, "set: param '%s' not found", name);
        return false;
    }
    Parameter& p = params_[idx];
    if (p.type != ParamType::INT32) {
        ESP_LOGW(TAG, "set: type mismatch for '%s'", name);
        return false;
    }
    if (p.has_range && (value < p.min_value.i || value > p.max_value.i)) {
        ESP_LOGW(TAG, "set: value %d out of range [%d, %d] for '%s'", (int)value, (int)p.min_value.i,
                 (int)p.max_value.i, name);
        return false;
    }
    p.value.i = value;
    p.change_count++;
    if (p.persistent) {
        saveToNVS();
    }
    notifyListeners(idx);
    return true;
}

bool ParameterServer::set(const char* name, float value) {
    size_t idx = findIndex(name);
    if (idx >= count_) {
        ESP_LOGW(TAG, "set: param '%s' not found", name);
        return false;
    }
    Parameter& p = params_[idx];
    if (p.type != ParamType::FLOAT) {
        ESP_LOGW(TAG, "set: type mismatch for '%s'", name);
        return false;
    }
    if (p.has_range && (value < p.min_value.f || value > p.max_value.f)) {
        ESP_LOGW(TAG, "set: value %f out of range for '%s'", (double)value, name);
        return false;
    }
    p.value.f = value;
    p.change_count++;
    if (p.persistent) {
        saveToNVS();
    }
    notifyListeners(idx);
    return true;
}

bool ParameterServer::set(const char* name, bool value) {
    size_t idx = findIndex(name);
    if (idx >= count_) {
        ESP_LOGW(TAG, "set: param '%s' not found", name);
        return false;
    }
    Parameter& p = params_[idx];
    if (p.type != ParamType::BOOL) {
        ESP_LOGW(TAG, "set: type mismatch for '%s'", name);
        return false;
    }
    p.value.b = value;
    p.change_count++;
    if (p.persistent) {
        saveToNVS();
    }
    notifyListeners(idx);
    return true;
}

// --- Change notifications ---

bool ParameterServer::onChange(const char* name, ParamChangeCallback callback, void* context) {
    if (callback == nullptr) {
        return false;
    }

    size_t idx = findIndex(name);
    if (idx >= count_) {
        ESP_LOGW(TAG, "onChange: param '%s' not found", name);
        return false;
    }
    if (listener_count_ >= MAX_LISTENERS) {
        ESP_LOGE(TAG, "onChange: listener table full");
        return false;
    }

    listeners_[listener_count_].param_index = idx;
    listeners_[listener_count_].callback = callback;
    listeners_[listener_count_].context = context;
    listeners_[listener_count_].active = true;
    ++listener_count_;
    return true;
}

void ParameterServer::removeListenersByContext(void* context) {
    if (context == nullptr) {
        return;
    }
    size_t write = 0;
    for (size_t read = 0; read < listener_count_; ++read) {
        if (listeners_[read].context != context) {
            if (write != read) {
                listeners_[write] = listeners_[read];
            }
            ++write;
        }
    }
    for (size_t i = write; i < listener_count_; ++i) {
        listeners_[i] = {};
    }
    listener_count_ = write;
}

void ParameterServer::notifyListeners(size_t param_index) {
    const char* name = params_[param_index].name;
    for (size_t i = 0; i < listener_count_; ++i) {
        if (listeners_[i].active && listeners_[i].param_index == param_index) {
            listeners_[i].callback(name, listeners_[i].context);
        }
    }
}

// --- Persistence ---

void ParameterServer::loadFromNVS() {
    PersistSnapshot snapshot{};

#if CHOPPER_HAS_NVS
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "loadFromNVS: nvs_open failed (%d)", static_cast<int>(err));
        return;
    }

    size_t blob_size = sizeof(snapshot);
    err = nvs_get_blob(handle, kNvsKey, &snapshot, &blob_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "loadFromNVS: nvs_get_blob failed (%d)", static_cast<int>(err));
        return;
    }
    if (blob_size != sizeof(snapshot)) {
        ESP_LOGW(TAG, "loadFromNVS: unexpected blob size %u", static_cast<unsigned>(blob_size));
        return;
    }
#else
    if (!g_host_snapshot_valid) {
        ESP_LOGI(TAG, "loadFromNVS: no host snapshot available");
        return;
    }
    snapshot = g_host_snapshot;
#endif

    if (snapshot.magic != kSnapshotMagic || snapshot.version != kSnapshotVersion) {
        ESP_LOGW(TAG, "loadFromNVS: invalid snapshot header");
        return;
    }
    if (snapshot.count > limits::MAX_PARAMETERS) {
        ESP_LOGW(TAG, "loadFromNVS: invalid entry count %u", static_cast<unsigned>(snapshot.count));
        return;
    }

    size_t applied = 0;
    for (size_t i = 0; i < snapshot.count; ++i) {
        const PersistEntry& entry = snapshot.entries[i];
        size_t idx = findIndex(entry.name);
        if (idx >= count_) {
            continue;
        }

        Parameter& p = params_[idx];
        if (!p.persistent || static_cast<uint8_t>(p.type) != entry.type) {
            continue;
        }

        bool valid = true;
        switch (p.type) {
            case ParamType::INT32:
                if (p.has_range && (entry.value.i < p.min_value.i || entry.value.i > p.max_value.i)) {
                    valid = false;
                }
                if (valid) {
                    p.value.i = entry.value.i;
                }
                break;
            case ParamType::FLOAT:
                if (p.has_range && (entry.value.f < p.min_value.f || entry.value.f > p.max_value.f)) {
                    valid = false;
                }
                if (valid) {
                    p.value.f = entry.value.f;
                }
                break;
            case ParamType::BOOL:
                p.value.b = (entry.value.b != 0);
                break;
        }

        if (valid) {
            ++applied;
        }
    }

    ESP_LOGI(TAG, "loadFromNVS: applied %u persistent parameters", static_cast<unsigned>(applied));
}

void ParameterServer::saveToNVS() {
    PersistSnapshot snapshot{};
    snapshot.magic = kSnapshotMagic;
    snapshot.version = kSnapshotVersion;
    snapshot.count = 0;

    for (size_t i = 0; i < count_ && snapshot.count < limits::MAX_PARAMETERS; ++i) {
        const Parameter& p = params_[i];
        if (!p.active || !p.persistent) {
            continue;
        }

        PersistEntry& entry = snapshot.entries[snapshot.count++];
        strncpy(entry.name, p.name, limits::MAX_PARAM_NAME_LEN - 1);
        entry.name[limits::MAX_PARAM_NAME_LEN - 1] = '\0';
        entry.type = static_cast<uint8_t>(p.type);

        switch (p.type) {
            case ParamType::INT32:
                entry.value.i = p.value.i;
                break;
            case ParamType::FLOAT:
                entry.value.f = p.value.f;
                break;
            case ParamType::BOOL:
                entry.value.b = p.value.b ? 1 : 0;
                break;
        }
    }

#if CHOPPER_HAS_NVS
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saveToNVS: nvs_open failed (%d)", static_cast<int>(err));
        return;
    }

    err = nvs_set_blob(handle, kNvsKey, &snapshot, sizeof(snapshot));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saveToNVS: failed (%d)", static_cast<int>(err));
        return;
    }
#else
    g_host_snapshot = snapshot;
    g_host_snapshot_valid = true;
#endif

    ESP_LOGI(TAG, "saveToNVS: saved %u persistent parameters", static_cast<unsigned>(snapshot.count));
}

// --- Reset ---

void ParameterServer::reset() {
    memset(params_, 0, sizeof(params_));
    memset(listeners_, 0, sizeof(listeners_));
    count_ = 0;
    listener_count_ = 0;
}

// --- Introspection ---

void ParameterServer::forEach(void (*visitor)(const Parameter&, void*), void* ctx) const {
    if (visitor == nullptr) {
        return;
    }
    for (size_t i = 0; i < count_; ++i) {
        if (params_[i].active) {
            visitor(params_[i], ctx);
        }
    }
}

}  // namespace chopper::core
