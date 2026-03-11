#include "chopper/core/PublishingNode.h"

namespace chopper::core {

PublishingNode::PublishingNode(const char* name) : Node(name) {
    // shared_ptr arrays are value-initialized (null) by default
}

}  // namespace chopper::core
