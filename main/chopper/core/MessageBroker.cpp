#include "chopper/core/MessageBroker.h"
#include "esp_log.h"
#include <cstring>

static const char* const TAG = "MessageBroker";

namespace chopper::core {

MessageBroker::MessageBroker() {
    memset(publishers_, 0, sizeof(publishers_));
    memset(subscriptions_, 0, sizeof(subscriptions_));
}

MessageBroker& MessageBroker::getInstance() {
    static MessageBroker instance;
    return instance;
}

void MessageBroker::registerPublisher(Publisher* publisher) {
    if (publisher == nullptr) {
        return;
    }

    size_t max_pubs = sizeof(publishers_) / sizeof(publishers_[0]);
    if (publisher_count_ >= max_pubs) {
        ESP_LOGE(TAG, "Cannot register publisher for '%s': max reached", publisher->getTopic());
        return;
    }

    publishers_[publisher_count_++] = publisher;
    matchPublisher(publisher);

    ESP_LOGI(TAG, "Registered publisher for topic: %s", publisher->getTopic());
}

void MessageBroker::unregisterPublisher(Publisher* publisher) {
    if (publisher == nullptr) {
        return;
    }

    // Remove from publisher array (compact)
    for (size_t i = 0; i < publisher_count_; i++) {
        if (publishers_[i] == publisher) {
            for (size_t j = i; j < publisher_count_ - 1; j++) {
                publishers_[j] = publishers_[j + 1];
            }
            publisher_count_--;
            publishers_[publisher_count_] = nullptr;
            break;
        }
    }

    // Also remove this publisher's references from all subscriptions
    // (subscriptions don't hold back-pointers, so no cleanup needed there)
}

void MessageBroker::unregisterSubscription(Subscription* subscription) {
    if (subscription == nullptr) {
        return;
    }

    // Remove from subscription array (compact)
    for (size_t i = 0; i < subscription_count_; i++) {
        if (subscriptions_[i] == subscription) {
            for (size_t j = i; j < subscription_count_ - 1; j++) {
                subscriptions_[j] = subscriptions_[j + 1];
            }
            subscription_count_--;
            subscriptions_[subscription_count_] = nullptr;
            break;
        }
    }

    // Remove this subscription from all publishers' delivery lists
    for (size_t i = 0; i < publisher_count_; i++) {
        if (publishers_[i] != nullptr) {
            publishers_[i]->removeSubscription(subscription);
        }
    }
}

void MessageBroker::registerSubscription(Subscription* subscription) {
    if (subscription == nullptr) {
        return;
    }

    size_t max_subs = sizeof(subscriptions_) / sizeof(subscriptions_[0]);
    if (subscription_count_ >= max_subs) {
        ESP_LOGE(TAG, "Cannot register subscription for '%s': max reached", subscription->getTopic());
        return;
    }

    subscriptions_[subscription_count_++] = subscription;
    matchSubscription(subscription);

    ESP_LOGI(TAG, "Registered subscription for topic: %s", subscription->getTopic());
}

void MessageBroker::matchPublisher(Publisher* publisher) {
    // Connect this publisher to all existing subscriptions on the same topic
    for (size_t i = 0; i < subscription_count_; i++) {
        Subscription* sub = subscriptions_[i];
        if ((sub != nullptr) && strcmp(publisher->getTopic(), sub->getTopic()) == 0) {
            publisher->addSubscription(sub);
            ESP_LOGD(TAG, "Matched publisher -> subscription on '%s'", publisher->getTopic());
        }
    }
}

void MessageBroker::matchSubscription(Subscription* subscription) {
    // Connect all existing publishers on the same topic to this subscription
    for (size_t i = 0; i < publisher_count_; i++) {
        Publisher* pub = publishers_[i];
        if ((pub != nullptr) && strcmp(pub->getTopic(), subscription->getTopic()) == 0) {
            pub->addSubscription(subscription);
            ESP_LOGD(TAG, "Matched publisher -> subscription on '%s'", subscription->getTopic());
        }
    }
}

void MessageBroker::processPendingMessages() {
    // Delivery is synchronous — nothing to process.
    // This method is reserved for future batched/queued delivery.
}

MessageBroker::Statistics MessageBroker::getStatistics() const {
    Statistics stats{};
    stats.total_publishers = publisher_count_;
    stats.total_subscriptions = subscription_count_;

    // Count unique topics
    const char* seen_topics[limits::MAX_TOPICS] = {};
    size_t topic_count = 0;

    for (size_t i = 0; i < publisher_count_; i++) {
        if (publishers_[i] == nullptr) {
            continue;
        }
        const char* t = publishers_[i]->getTopic();
        bool found = false;
        for (size_t j = 0; j < topic_count; j++) {
            if (strcmp(seen_topics[j], t) == 0) {
                found = true;
                break;
            }
        }
        if (!found && topic_count < limits::MAX_TOPICS) {
            seen_topics[topic_count++] = t;
        }
    }
    for (size_t i = 0; i < subscription_count_; i++) {
        if (subscriptions_[i] == nullptr) {
            continue;
        }
        const char* t = subscriptions_[i]->getTopic();
        bool found = false;
        for (size_t j = 0; j < topic_count; j++) {
            if (strcmp(seen_topics[j], t) == 0) {
                found = true;
                break;
            }
        }
        if (!found && topic_count < limits::MAX_TOPICS) {
            seen_topics[topic_count++] = t;
        }
    }

    stats.total_topics = topic_count;
    return stats;
}

}  // namespace chopper::core
