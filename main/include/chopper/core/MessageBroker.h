#pragma once

#include "Publisher.h"
#include "Subscription.h"
#include "chopper/chopper_limits.h"
#include <cstring>

namespace chopper {
namespace core {

/**
 * @brief Central message broker for topic-based pub/sub.
 *
 * Uses fixed-size flat arrays — no heap allocation after init.
 * Topic matching is done by string comparison on registration (init-time only).
 * Hot-path delivery is direct pointer-based (O(1) per subscriber).
 */
class MessageBroker {
public:
    static MessageBroker& getInstance();

    /**
     * @brief Create a typed publisher.
     * @tparam MessageT Message type
     * @param topic Topic name (string literal or static storage)
     * @param qos Quality of service profile
     */
    template<typename MessageT>
    TypedPublisherPtr<MessageT> createPublisher(const char* topic,
                                                const QoSProfile& qos = QoSProfile::systemDefault()) {
        auto publisher = std::make_shared<TypedPublisher<MessageT>>(topic, qos);
        registerPublisher(publisher.get());
        return publisher;
    }

    /**
     * @brief Create a typed subscription with function pointer callback.
     */
    template<typename MessageT>
    TypedSubscriptionPtr<MessageT> createSubscription(const char* topic,
                                                      MessageCallbackFn<MessageT> callback,
                                                      void* context,
                                                      const QoSProfile& qos = QoSProfile::systemDefault()) {
        auto sub = std::make_shared<TypedSubscription<MessageT>>(topic, callback, context, qos);
        registerSubscription(sub.get());
        return sub;
    }

    /**
     * @brief Process pending messages (called by executor each tick).
     * Currently a no-op since delivery is synchronous. Reserved for
     * future batched/queued delivery.
     */
    void processPendingMessages();

    struct Statistics {
        size_t total_publishers;
        size_t total_subscriptions;
        size_t total_topics;
    };

    Statistics getStatistics() const;

    /// Unregister a publisher (called from Publisher destructor).
    void unregisterPublisher(Publisher* publisher);

    /// Unregister a subscription (called from Subscription destructor).
    void unregisterSubscription(Subscription* subscription);

private:
    MessageBroker();

    void registerPublisher(Publisher* publisher);
    void registerSubscription(Subscription* subscription);

    /**
     * @brief Match a publisher with all subscriptions on the same topic,
     * and vice versa. Called at registration time (init, not hot path).
     */
    void matchPublisher(Publisher* publisher);
    void matchSubscription(Subscription* subscription);

    // Flat arrays of raw pointers. Lifetime managed by nodes via shared_ptr.
    Publisher* publishers_[limits::MAX_TOPICS * 2];   // Allow >1 publisher per topic
    size_t publisher_count_;

    Subscription* subscriptions_[limits::MAX_TOPICS * limits::MAX_SUBSCRIBERS_PER_TOPIC];
    size_t subscription_count_;

    MessageBroker(const MessageBroker&) = delete;
    MessageBroker& operator=(const MessageBroker&) = delete;
};

} // namespace core
} // namespace chopper
