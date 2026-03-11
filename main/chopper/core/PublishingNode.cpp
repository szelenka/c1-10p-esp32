#include "chopper/core/PublishingNode.h"
#include "chopper/core/ParameterServer.h"

namespace chopper::core {

PublishingNode::PublishingNode(const char* name) : Node(name) {
    // shared_ptr arrays are value-initialized (null) by default
}

PublishingNode::~PublishingNode() {
    // Remove any ParameterServer onChange listeners that used this node
    // as their context, preventing dangling pointers after destruction.
    ParameterServer::getInstance().removeListenersByContext(this);
}

}  // namespace chopper::core
