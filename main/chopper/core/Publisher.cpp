#include "chopper/core/Publisher.h"
#include "chopper/core/MessageBroker.h"
#include "esp_log.h"

static const char* TAG = "Publisher";

namespace chopper {
namespace core {

Publisher::Publisher(const char* topic, TypeId type_id, const QoSProfile& qos)
    : topic_(topic)
    , type_id_(type_id)
    , qos_(qos)
    , message_count_(0)
    , subscriber_count_(0)
{
    memset(subscribers_, 0, sizeof(subscribers_));
}

Publisher::~Publisher() {
    MessageBroker::getInstance().unregisterPublisher(this);
}

size_t Publisher::getSubscriberCount() const {
    return subscriber_count_;
}

bool Publisher::addSubscription(Subscription* sub) {
    if (!sub) return false;

    if (subscriber_count_ >= limits::MAX_SUBSCRIBERS_PER_TOPIC) {
        ESP_LOGE(TAG, "Cannot add subscriber to '%s': max %zu reached",
                 topic_, limits::MAX_SUBSCRIBERS_PER_TOPIC);
        return false;
    }

    // Check for duplicate
    for (size_t i = 0; i < subscriber_count_; i++) {
        if (subscribers_[i] == sub) {
            return true; // Already registered
        }
    }

    subscribers_[subscriber_count_++] = sub;
    return true;
}

void Publisher::removeSubscription(Subscription* sub) {
    for (size_t i = 0; i < subscriber_count_; i++) {
        if (subscribers_[i] == sub) {
            // Shift remaining elements
            for (size_t j = i; j < subscriber_count_ - 1; j++) {
                subscribers_[j] = subscribers_[j + 1];
            }
            subscriber_count_--;
            subscribers_[subscriber_count_] = nullptr;
            return;
        }
    }
}

void Publisher::deliver(const Message& message) {
    for (size_t i = 0; i < subscriber_count_; i++) {
        Subscription* sub = subscribers_[i];
        if (sub && sub->matchesType(message.getTypeId())) {
            sub->deliver(message);
        }
    }
    message_count_++;
}

} // namespace core
} // namespace chopper
