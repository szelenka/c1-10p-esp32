#include "chopper/core/ParameterServer.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "ParameterServer";

namespace chopper {
namespace core {

ParameterServer& ParameterServer::getInstance() {
    static ParameterServer instance;
    return instance;
}

ParameterServer::ParameterServer() : count_(0), listener_count_(0) {
    memset(params_, 0, sizeof(params_));
    memset(listeners_, 0, sizeof(listeners_));
}

size_t ParameterServer::findIndex(const char* name) const {
    for (size_t i = 0; i < count_; ++i) {
        if (params_[i].active && strcmp(params_[i].name, name) == 0) {
            return i;
        }
    }
    return count_; // not found sentinel
}

// --- Declare ---

bool ParameterServer::declare(const char* name, int32_t default_val,
                              int32_t min_val, int32_t max_val,
                              bool persistent) {
    if (!name || count_ >= limits::MAX_PARAMETERS) {
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

bool ParameterServer::declare(const char* name, float default_val,
                              float min_val, float max_val,
                              bool persistent) {
    if (!name || count_ >= limits::MAX_PARAMETERS) {
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

bool ParameterServer::declare(const char* name, bool default_val,
                              bool persistent) {
    if (!name || count_ >= limits::MAX_PARAMETERS) {
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
    if (idx >= count_) return false;
    if (params_[idx].type != ParamType::INT32) return false;
    out = params_[idx].value.i;
    return true;
}

bool ParameterServer::get(const char* name, float& out) const {
    size_t idx = findIndex(name);
    if (idx >= count_) return false;
    if (params_[idx].type != ParamType::FLOAT) return false;
    out = params_[idx].value.f;
    return true;
}

bool ParameterServer::get(const char* name, bool& out) const {
    size_t idx = findIndex(name);
    if (idx >= count_) return false;
    if (params_[idx].type != ParamType::BOOL) return false;
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
        ESP_LOGW(TAG, "set: value %d out of range [%d, %d] for '%s'",
                 (int)value, (int)p.min_value.i, (int)p.max_value.i, name);
        return false;
    }
    p.value.i = value;
    p.change_count++;
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
    notifyListeners(idx);
    return true;
}

// --- Change notifications ---

bool ParameterServer::onChange(const char* name, ParamChangeCallback callback, void* context) {
    if (!callback) return false;

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

void ParameterServer::notifyListeners(size_t param_index) {
    const char* name = params_[param_index].name;
    for (size_t i = 0; i < listener_count_; ++i) {
        if (listeners_[i].active && listeners_[i].param_index == param_index) {
            listeners_[i].callback(name, listeners_[i].context);
        }
    }
}

// --- NVS stubs ---

void ParameterServer::loadFromNVS() {
    ESP_LOGI(TAG, "loadFromNVS: stub (not yet implemented)");
}

void ParameterServer::saveToNVS() {
    ESP_LOGI(TAG, "saveToNVS: stub (not yet implemented)");
}

// --- Introspection ---

void ParameterServer::forEach(void(*visitor)(const Parameter&, void*), void* ctx) const {
    if (!visitor) return;
    for (size_t i = 0; i < count_; ++i) {
        if (params_[i].active) {
            visitor(params_[i], ctx);
        }
    }
}

} // namespace core
} // namespace chopper
