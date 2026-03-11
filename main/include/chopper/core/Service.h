#pragma once

#include <cstdint>
#include <cstddef>
#include "chopper/core/Message.h"
#include "chopper/chopper_limits.h"
#include "esp_log.h"

namespace chopper {
namespace core {

/**
 * @brief Static service registry.
 *
 * Holds a fixed array of service entries matched by TypeId of the request type.
 * Synchronous: client calls server handler directly (single-thread executor model).
 */
class ServiceRegistry {
public:
    /// Handler type: takes raw request/response pointers + context.
    using RawHandler = bool (*)(const void* request, void* response, void* context);

    static ServiceRegistry& getInstance() {
        static ServiceRegistry instance;
        return instance;
    }

    struct Entry {
        TypeId request_type_id;
        RawHandler handler;
        void* context;
        bool active;
    };

    /**
     * @brief Register a service handler for the given request type.
     * @return true on success, false if registry full or duplicate type.
     */
    bool registerService(TypeId request_type_id, RawHandler handler, void* context) {
        // Check for duplicate
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].active && entries_[i].request_type_id == request_type_id) {
                return false;  // already registered
            }
        }
        if (count_ >= limits::MAX_SERVICES) {
            return false;
        }
        entries_[count_].request_type_id = request_type_id;
        entries_[count_].handler = handler;
        entries_[count_].context = context;
        entries_[count_].active = true;
        ++count_;
        return true;
    }

    /**
     * @brief Look up a handler by request TypeId.
     * @return Pointer to the entry, or nullptr if not found.
     */
    const Entry* find(TypeId request_type_id) const {
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].active && entries_[i].request_type_id == request_type_id) {
                return &entries_[i];
            }
        }
        return nullptr;
    }

    /**
     * @brief Unregister a service handler for the given request type.
     */
    void unregisterService(TypeId request_type_id) {
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].active && entries_[i].request_type_id == request_type_id) {
                entries_[i].active = false;
                return;
            }
        }
    }

    size_t activeCount() const {
        size_t n = 0;
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i].active)
                ++n;
        }
        return n;
    }

private:
    ServiceRegistry() : count_(0) {}

    Entry entries_[limits::MAX_SERVICES];
    size_t count_;

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;
};

/**
 * @brief Type-safe service server.
 *
 * Registers a handler for requests of type RequestT and produces ResponseT.
 * The handler is a C function pointer + void* context (no std::function).
 *
 * @tparam RequestT  The request message type.
 * @tparam ResponseT The response message type.
 */
template <typename RequestT, typename ResponseT>
class ServiceServer {
public:
    using Handler = bool (*)(const RequestT& request, ResponseT& response, void* context);

    ServiceServer() : handler_(nullptr), context_(nullptr), registered_(false) {}

    ~ServiceServer() {
        if (registered_) {
            ServiceRegistry::getInstance().unregisterService(getTypeId<RequestT>());
        }
    }

    /**
     * @brief Register a handler for this service.
     * @param handler Function pointer that handles the request.
     * @param context Opaque context passed to the handler.
     * @return true on success.
     */
    bool registerHandler(Handler handler, void* context = nullptr) {
        if (!handler)
            return false;
        handler_ = handler;
        context_ = context;

        // Register via a type-erased trampoline
        registered_ =
            ServiceRegistry::getInstance().registerService(getTypeId<RequestT>(), &ServiceServer::trampoline, this);

        return registered_;
    }

private:
    static bool trampoline(const void* request, void* response, void* self_ptr) {
        auto* self = static_cast<ServiceServer*>(self_ptr);
        return self->handler_(*static_cast<const RequestT*>(request), *static_cast<ResponseT*>(response),
                              self->context_);
    }

    Handler handler_;
    void* context_;
    bool registered_;

    ServiceServer(const ServiceServer&) = delete;
    ServiceServer& operator=(const ServiceServer&) = delete;
};

/**
 * @brief Type-safe service client.
 *
 * Calls a registered ServiceServer synchronously within the executor.
 * Single-thread model: the response is available immediately after call().
 *
 * @tparam RequestT  The request message type.
 * @tparam ResponseT The response message type.
 */
template <typename RequestT, typename ResponseT>
class ServiceClient {
public:
    enum class Status {
        SUCCESS,
        NO_SERVER,
        HANDLER_ERROR
    };

    ServiceClient() = default;

    /**
     * @brief Synchronous service call.
     * @param request  The request to send.
     * @param response Filled in on success.
     * @return Status indicating outcome.
     */
    Status call(const RequestT& request, ResponseT& response) {
        const auto* entry = ServiceRegistry::getInstance().find(getTypeId<RequestT>());
        if (!entry) {
            return Status::NO_SERVER;
        }
        bool ok = entry->handler(&request, &response, entry->context);
        return ok ? Status::SUCCESS : Status::HANDLER_ERROR;
    }
};

}  // namespace core
}  // namespace chopper
