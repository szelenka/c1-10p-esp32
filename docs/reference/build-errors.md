# Common Build Errors

> Referenced from CLAUDE.md. Update this file when adding a new build error pattern.

| Error | Cause | Fix |
|-------|-------|-----|
| `undefined reference to TypeTag<X>` or `getTypeId` | Missing `#include` for the message type | Add `#include "chopper/messages/CommonMessages.h"` |
| `no matching function for call to 'createSubscription'` | Wrong callback signature | Member fn: `void method(const MsgType&)`. Free fn: `void fn(const MsgType&, void*)`. |
| `exceeds MAX_SUBSCRIBERS` / array bounds | Too many subscribers on one topic | Audit topic usage or ask user about increasing limit in `chopper_limits.h` |
| `multiple definition of ...` | Non-inline function defined in header | Add `inline` keyword, or move definition to `.cpp` |
| Mock header not found | `test/mocks/` structure doesn't mirror ESP-IDF path | Check include path: `g++ -I test/mocks -I main/include` |
| `use of undeclared identifier 'ESP_LOG...'` | Missing mock header | Ensure `test/mocks/esp_log.h` is on the include path |
| Linker errors for `Node::*` or `MessageBroker::*` | Missing `.cpp` in compile command | Check the compilation comment at top of existing test files for required `.cpp` list |
