# Quick Decisions

> Settled questions. Don't re-ask.

| Question | Answer | Reference |
|----------|--------|-----------|
| `std::string`? | No. `const char*` or fixed `char[]` | CLAUDE.md §Constraints |
| Test framework? | doctest, header-only, vendored | `tester.md` |
| New message type? | `CommonMessages.h`, ask user first | `architect.md` |
| Header-only or .cpp? | Header-only unless static state | Reduces link complexity |
| New node pattern? | `PublishingNode`, trampoline subscription | `docs/reference/node-template.md` |
| Subscribe to topic? | `createSubscription<T>(topic, &Node::method, this)` | No std::function |
| Publish? | `createPublisher<T>(topic)` then `pub->publish(msg)` | |
| Type IDs? | `TypeTag<T>::tag` pointer comparison | No RTTI |
| Base class? | `PublishingNode` (pub/sub) or `Node` (pure logic) | |
| Increase limits? | Ask user. All in `chopper_limits.h` | `architect.md` |
| Boot order? | addMotor/addServo/addAudio/addNode → init → start | Register HW before init |
| Safety gating? | `safety-auditor.md §Invariants` | Canonical source |
| Where does logic go? | Trace `docs/registry.md`, earliest correct stage | `placement-reasoning.md` |
