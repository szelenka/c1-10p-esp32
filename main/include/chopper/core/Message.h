#pragma once

#include <cstdint>
#include <cstddef>

namespace chopper::core {

/**
 * @brief Type tag for compile-time type identification without RTTI.
 *
 * Each unique T gets a distinct static variable. The address of that
 * variable serves as a collision-free type ID (pointer comparison).
 */
template <typename T>
struct TypeTag {
    static const char tag;
};
template <typename T>
const char TypeTag<T>::tag = 0;

/// Type identifier — pointer to a unique static per-type.
using TypeId = const void*;

/// Get the type ID for a given type T.
template <typename T>
constexpr TypeId getTypeId() {
    return &TypeTag<T>::tag;
}

/**
 * @brief Base class for all messages in the system.
 *
 * Messages are plain-data structs communicated between nodes via pub/sub.
 * They must be lightweight, copyable, and contain no pointers to dynamic memory.
 */
class Message {
public:
    Message() = default;
    virtual ~Message() = default;

    [[nodiscard]] uint64_t getTimestamp() const { return timestamp_us_; }
    void setTimestamp(uint64_t timestamp_us) { timestamp_us_ = timestamp_us; }

    /// Runtime type ID for this message (collision-free pointer comparison).
    [[nodiscard]] virtual TypeId getTypeId() const = 0;

    /// Human-readable type name for debug logging.
    [[nodiscard]] virtual const char* getTypeName() const = 0;

    /// Size of the concrete message struct in bytes.
    [[nodiscard]] virtual size_t getSize() const = 0;

private:
    uint64_t timestamp_us_ = 0;
};

/**
 * @brief CRTP base for typed messages.
 *
 * Provides automatic implementations of getTypeId(), getTypeName(), and getSize().
 * @tparam T The concrete message type (CRTP pattern).
 */
template <typename T>
class TypedMessage : public Message {
    TypedMessage() = default;

public:
    [[nodiscard]] TypeId getTypeId() const override { return core::getTypeId<T>(); }

    [[nodiscard]] const char* getTypeName() const override {
        // Returns the mangled name — sufficient for debug logging.
        // Not used for type matching (TypeId pointer comparison is used instead).
        return __PRETTY_FUNCTION__;
    }

    [[nodiscard]] size_t getSize() const override { return sizeof(T); }
    friend T;
};

/**
 * @brief Message quality of service settings.
 */
struct QoSProfile {
    enum class Reliability : uint8_t {
        BEST_EFFORT,  ///< May lose messages (faster)
        RELIABLE      ///< Guarantee delivery (may block)
    };

    enum class History : uint8_t {
        KEEP_LAST,  ///< Keep only the last N messages
        KEEP_ALL    ///< Keep all (until memory limit)
    };

    Reliability reliability = Reliability::RELIABLE;
    History history = History::KEEP_LAST;
    size_t depth = 1;

    static const QoSProfile& systemDefault() {
        static const QoSProfile profile;
        return profile;
    }

    static const QoSProfile& sensorData() {
        static const QoSProfile profile{Reliability::BEST_EFFORT, History::KEEP_LAST, 1};
        return profile;
    }

    static const QoSProfile& commandAndControl() {
        static const QoSProfile profile{Reliability::RELIABLE, History::KEEP_LAST, 5};
        return profile;
    }
};

}  // namespace chopper::core
