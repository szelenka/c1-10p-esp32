#pragma once

#include "Message.h"
#include "Subscription.h"
#include "chopper/chopper_limits.h"
#include <memory>
#include <cstring>
#include "esp_timer.h"

namespace chopper::core {

/**
 * @brief Publisher for sending messages to subscribers.
 *
 * The publish path is zero-allocation: messages are delivered synchronously
 * by const reference. No heap allocation, no shared_ptr, no copies.
 */
class Publisher {
public:
    Publisher(const char* topic, TypeId type_id, const QoSProfile& qos = QoSProfile::systemDefault());

    virtual ~Publisher();

    [[nodiscard]] const char* getTopic() const { return topic_; }
    [[nodiscard]] TypeId getTypeId() const { return type_id_; }
    [[nodiscard]] const QoSProfile& getQoS() const { return qos_; }

    /**
     * @brief Get number of active subscribers.
     */
    [[nodiscard]] size_t getSubscriberCount() const;

    [[nodiscard]] bool hasSubscribers() const { return getSubscriberCount() > 0; }

    /**
     * @brief Add a subscription to this publisher's delivery list.
     * Called by MessageBroker during topic matching.
     */
    bool addSubscription(Subscription* sub);

    /**
     * @brief Remove a subscription.
     */
    void removeSubscription(Subscription* sub);

protected:
    /**
     * @brief Deliver a message to all subscribers by const reference.
     * Zero-allocation hot path. Type must match publisher's type_id_.
     */
    void deliver(const Message& message);

    friend class MessageBroker;

private:
    const char* topic_;
    TypeId type_id_;
    QoSProfile qos_;
    uint64_t message_count_ = 0;

    // Fixed-size subscriber array — no heap allocation.
    Subscription* subscribers_[limits::MAX_SUBSCRIBERS_PER_TOPIC]{};
    size_t subscriber_count_ = 0;

public:
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
};

using PublisherPtr = std::shared_ptr<Publisher>;

/**
 * @brief Type-safe publisher. The publish() method delivers by const reference
 * with zero heap allocation.
 */
template <typename MessageT>
class TypedPublisher : public Publisher {
public:
    TypedPublisher(const char* topic, const QoSProfile& qos = QoSProfile::systemDefault())
        : Publisher(topic, core::getTypeId<MessageT>(), qos) {}

    /**
     * @brief Publish a message. Zero-allocation: delivers synchronously
     * by const reference to all subscribers.
     */
    bool publish(const MessageT& message) {
        // Stamp and deliver in-place via a mutable copy on the stack
        MessageT stamped = message;
        stamped.setTimestamp(esp_timer_get_time());
        deliver(stamped);
        return true;
    }

    /**
     * @brief Publish with explicit timestamp.
     */
    bool publish(const MessageT& message, uint64_t timestamp_us) {
        MessageT stamped = message;
        stamped.setTimestamp(timestamp_us);
        deliver(stamped);
        return true;
    }
};

template <typename MessageT>
using TypedPublisherPtr = std::shared_ptr<TypedPublisher<MessageT>>;

}  // namespace chopper::core
