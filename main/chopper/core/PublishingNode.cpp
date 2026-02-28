#include "chopper/core/PublishingNode.h"

namespace chopper {
namespace core {

PublishingNode::PublishingNode(const char* name)
    : Node(name)
    , pub_count_(0)
    , sub_count_(0)
{
    memset(publishers_, 0, sizeof(publishers_));
    memset(subscriptions_, 0, sizeof(subscriptions_));
}

} // namespace core
} // namespace chopper
