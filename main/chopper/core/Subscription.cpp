#include "chopper/core/Subscription.h"
#include "chopper/core/MessageBroker.h"

namespace chopper::core {

Subscription::Subscription(const char* topic, TypeId type_id, const QoSProfile& qos)
    : topic_(topic)
    , type_id_(type_id)
    , qos_(qos)

{}

Subscription::~Subscription() {
    MessageBroker::getInstance().unregisterSubscription(this);
}

void Subscription::incrementCounters(bool dropped) {
    if (dropped) {
        dropped_count_++;
    } else {
        message_count_++;
    }
}

}  // namespace chopper::core
