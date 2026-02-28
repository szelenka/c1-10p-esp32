#pragma once

#include "Node.h"
#include "MessageBroker.h"
#include "chopper/chopper_limits.h"

namespace chopper {
namespace core {

/**
 * @brief Base class for nodes that publish and subscribe to messages.
 *
 * Provides convenience methods for creating publishers and subscriptions
 * and manages their lifecycle with the node.
 */
class PublishingNode : public Node {
public:
    explicit PublishingNode(const char* name);
    virtual ~PublishingNode() = default;

protected:
    /**
     * @brief Create a typed publisher.
     */
    template<typename MessageT>
    TypedPublisherPtr<MessageT> createPublisher(const char* topic,
                                                const QoSProfile& qos = QoSProfile::systemDefault()) {
        auto publisher = MessageBroker::getInstance().createPublisher<MessageT>(topic, qos);
        if (pub_count_ < MAX_PUB_SUB) {
            publishers_[pub_count_++] = publisher;
        }
        return publisher;
    }

    /**
     * @brief Create a subscription with a member function callback.
     *
     * Wraps the member function via a static trampoline + this pointer.
     * No std::function, no heap allocation in the callback path.
     *
     * Usage:
     *   createSubscription<MotorCommand>("drive/cmd",
     *       &MyNode::onMotorCommand, this);
     */
    template<typename MessageT, typename NodeT>
    TypedSubscriptionPtr<MessageT> createSubscription(const char* topic,
                                                      void (NodeT::*method)(const MessageT&),
                                                      NodeT* instance,
                                                      const QoSProfile& qos = QoSProfile::systemDefault()) {
        // Store method pointer in a static trampoline context.
        // This works because each unique (NodeT, method) produces a unique
        // template instantiation with its own static trampoline.
        struct CallbackContext {
            NodeT* instance;
            void (NodeT::*method)(const MessageT&);
        };

        // Allocate context — stored for lifetime of subscription.
        // This is a one-time init allocation, not in the hot path.
        auto* ctx = new CallbackContext{instance, method};

        auto trampoline = [](const MessageT& msg, void* context) {
            auto* c = static_cast<CallbackContext*>(context);
            (c->instance->*(c->method))(msg);
        };

        auto sub = MessageBroker::getInstance().createSubscription<MessageT>(
            topic, trampoline, ctx, qos);
        if (sub_count_ < MAX_PUB_SUB) {
            subscriptions_[sub_count_++] = sub;
        }
        return sub;
    }

    /**
     * @brief Create a subscription with a plain function pointer callback.
     */
    template<typename MessageT>
    TypedSubscriptionPtr<MessageT> createSubscription(const char* topic,
                                                      MessageCallbackFn<MessageT> callback,
                                                      void* context = nullptr,
                                                      const QoSProfile& qos = QoSProfile::systemDefault()) {
        auto sub = MessageBroker::getInstance().createSubscription<MessageT>(
            topic, callback, context, qos);
        if (sub_count_ < MAX_PUB_SUB) {
            subscriptions_[sub_count_++] = sub;
        }
        return sub;
    }

    size_t getPublisherCount() const { return pub_count_; }
    size_t getSubscriptionCount() const { return sub_count_; }

private:
    static constexpr size_t MAX_PUB_SUB = 8;

    // Store shared_ptrs to keep publishers/subscriptions alive.
    // These are base-class pointers since we just need lifetime management.
    PublisherPtr publishers_[MAX_PUB_SUB];
    size_t pub_count_;

    SubscriptionPtr subscriptions_[MAX_PUB_SUB];
    size_t sub_count_;
};

} // namespace core
} // namespace chopper
