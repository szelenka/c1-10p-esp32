#pragma once

#include "Message.h"
#include <cstdint>
#include <memory>

namespace chopper {
namespace core {

/**
 * @brief Base subscription class.
 *
 * Subscriptions receive messages from publishers on matching topics.
 */
class Subscription {
public:
    /**
     * @brief Constructor
     * @param topic Topic name (must be a string literal or static storage)
     * @param type_id TypeId of the expected message type
     * @param qos Quality of service profile
     */
    Subscription(const char* topic, TypeId type_id,
                 const QoSProfile& qos = QoSProfile::systemDefault());

    virtual ~Subscription();

    const char* getTopic() const { return topic_; }
    const QoSProfile& getQoS() const { return qos_; }
    TypeId getExpectedTypeId() const { return type_id_; }

    /**
     * @brief Check if this subscription accepts the given message type.
     * Uses pointer comparison — O(1), no string ops.
     */
    bool matchesType(TypeId msg_type_id) const {
        return type_id_ == msg_type_id;
    }

    /**
     * @brief Deliver a message to this subscription.
     * The message pointer must be of the expected concrete type.
     * @param message Pointer to the concrete message (caller guarantees type match)
     */
    virtual void deliver(const Message& message) = 0;

    uint64_t getMessageCount() const { return message_count_; }
    uint64_t getDroppedCount() const { return dropped_count_; }

protected:
    void incrementCounters(bool dropped = false);

private:
    const char* topic_;
    TypeId type_id_;
    QoSProfile qos_;
    uint64_t message_count_;
    uint64_t dropped_count_;

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
};

using SubscriptionPtr = std::shared_ptr<Subscription>;

/**
 * @brief Callback type for typed message subscriptions.
 * Uses a C function pointer + void* context to avoid std::function heap allocation.
 */
template<typename MessageT>
using MessageCallbackFn = void(*)(const MessageT&, void* context);

/**
 * @brief Type-safe subscription with function-pointer callback.
 * No RTTI, no dynamic_cast, no std::function, no heap allocation in delivery path.
 */
template<typename MessageT>
class TypedSubscription : public Subscription {
public:
    TypedSubscription(const char* topic,
                      MessageCallbackFn<MessageT> callback,
                      void* context,
                      const QoSProfile& qos = QoSProfile::systemDefault())
        : Subscription(topic, core::getTypeId<MessageT>(), qos)
        , callback_(callback)
        , context_(context) {}

    void deliver(const Message& message) override {
        // Type is already verified by Publisher before calling deliver().
        // static_cast is safe here — no RTTI needed.
        const auto& typed = static_cast<const MessageT&>(message);
        callback_(typed, context_);
        incrementCounters(false);
    }

private:
    MessageCallbackFn<MessageT> callback_;
    void* context_;
};

template<typename MessageT>
using TypedSubscriptionPtr = std::shared_ptr<TypedSubscription<MessageT>>;

} // namespace core
} // namespace chopper
