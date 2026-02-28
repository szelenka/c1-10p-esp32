#pragma once

#include <cstdint>
#include <cstddef>
#include <cfloat>
#include <climits>
#include "chopper/chopper_limits.h"

namespace chopper {
namespace core {

/// Parameter value types.
enum class ParamType : uint8_t {
    INT32,
    FLOAT,
    BOOL
};

/// A single parameter entry.
struct Parameter {
    char      name[limits::MAX_PARAM_NAME_LEN];
    ParamType type;
    union {
        int32_t i;
        float   f;
        bool    b;
    } value;
    union {
        int32_t i;
        float   f;
    } min_value;
    union {
        int32_t i;
        float   f;
    } max_value;
    bool     has_range;
    bool     persistent;  ///< Save to NVS on change (future)
    uint32_t change_count;
    bool     active;
};

/// Callback when a parameter value changes.
using ParamChangeCallback = void(*)(const char* name, void* context);

/**
 * @brief Lightweight runtime parameter server.
 *
 * Fixed-size array storage (no heap). Name lookup is O(n) via strcmp,
 * which is acceptable given MAX_PARAMETERS is small.
 * NVS persistence is stubbed for future implementation.
 */
class ParameterServer {
public:
    static ParameterServer& getInstance();

    // --- Declare parameters with default values ---

    bool declare(const char* name, int32_t default_val,
                 int32_t min_val = INT32_MIN, int32_t max_val = INT32_MAX,
                 bool persistent = false);

    bool declare(const char* name, float default_val,
                 float min_val = -FLT_MAX, float max_val = FLT_MAX,
                 bool persistent = false);

    bool declare(const char* name, bool default_val,
                 bool persistent = false);

    // --- Get parameter values ---

    bool get(const char* name, int32_t& out) const;
    bool get(const char* name, float& out) const;
    bool get(const char* name, bool& out) const;

    // --- Set parameter values (validates range, notifies listeners) ---

    bool set(const char* name, int32_t value);
    bool set(const char* name, float value);
    bool set(const char* name, bool value);

    // --- Change notifications ---

    /**
     * @brief Register a callback for when a named parameter changes.
     * @return true on success, false if listener table full.
     */
    bool onChange(const char* name, ParamChangeCallback callback, void* context);

    // --- NVS persistence stubs ---

    void loadFromNVS();
    void saveToNVS();

    // --- Introspection ---

    void forEach(void(*visitor)(const Parameter&, void*), void* ctx) const;

    size_t count() const { return count_; }

    /// Reset all parameters and listeners. Primarily for testing.
    void reset();

private:
    ParameterServer();

    /// Find parameter index by name. Returns count_ if not found.
    size_t findIndex(const char* name) const;

    /// Notify all listeners registered for the parameter at the given index.
    void notifyListeners(size_t param_index);

    Parameter params_[limits::MAX_PARAMETERS];
    size_t    count_;

    struct ChangeListener {
        size_t              param_index;
        ParamChangeCallback callback;
        void*               context;
        bool                active;
    };

    static constexpr size_t MAX_LISTENERS = 16;
    ChangeListener listeners_[MAX_LISTENERS];
    size_t         listener_count_;

    ParameterServer(const ParameterServer&) = delete;
    ParameterServer& operator=(const ParameterServer&) = delete;
};

} // namespace core
} // namespace chopper
