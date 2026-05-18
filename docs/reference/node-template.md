# Node Template

> Canonical template for new nodes. Referenced from `implementer.md`.

```cpp
// main/include/chopper/nodes/ExampleNode.h
#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/messages/CommonMessages.h"

namespace chopper::nodes {

class ExampleNode : public core::PublishingNode {
public:
    ExampleNode(const char* name, const char* input_topic)
        : PublishingNode(name)
        , input_topic_(input_topic) {}

    bool initialize() override {
        sub_ = createSubscription<messages::MotorCommand>(
            input_topic_, &ExampleNode::onCommand, this);
        pub_ = createPublisher<messages::SystemStatus>("example/status");
        return sub_ != nullptr && pub_ != nullptr;
    }

    void process(uint64_t /*now*/) override {
        // Periodic work (if any). Called at executor tick rate.
    }

    void emergencyStop() override {
        // Stop all outputs immediately.
    }

    ExampleNode(const ExampleNode&) = delete;
    ExampleNode& operator=(const ExampleNode&) = delete;

private:
    void onCommand(const messages::MotorCommand& cmd) {
        // Handle incoming message -- called by pub/sub, not process().
    }

    const char* input_topic_;
    core::TypedSubscriptionPtr<messages::MotorCommand> sub_;
    core::TypedPublisherPtr<messages::SystemStatus> pub_;
};

}  // namespace chopper::nodes
```

Key points: inherit `PublishingNode`, subscribe in `initialize()` with member-function syntax, use `TypedSubscriptionPtr<T>` / `TypedPublisherPtr<T>` for storage, implement `emergencyStop()`.
