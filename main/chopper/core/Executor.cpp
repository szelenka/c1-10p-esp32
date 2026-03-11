#include "chopper/core/Executor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <mutex>

static const char* const TAG = "Executor";

namespace {
bool isSoftStopExcludedNode(const char* name) {
    if (name == nullptr) {
        return false;
    }
    return (strcmp(name, "safety") == 0) || (strcmp(name, "telemetry_node") == 0) ||
           (strcmp(name, "telemetry_io_tap") == 0);
}
}  // namespace

namespace chopper::core {

Executor::Executor() : Executor(Config{}) {}

Executor::Executor(const Config& config)
    : config_(config)
    , message_broker_(MessageBroker::getInstance())
    , task_handle_(nullptr)
    , is_running_(false)
    , should_stop_(false)
    , emergency_stop_(false)
    , soft_stop_(false)
    , last_loop_time_(0)
    , last_watchdog_feed_(0)
    , emergency_callback_(nullptr)
    , emergency_callback_context_(nullptr)
    , loop_time_accumulator_(0) {
    nodes_.reserve(config_.max_nodes);
    ESP_LOGI(TAG, "Executor created with %u Hz loop frequency", config_.loop_frequency_hz);
}

Executor::~Executor() {
    stop();
}

bool Executor::addNode(const NodePtr& node) {
    if (!node) {
        ESP_LOGE(TAG, "Cannot add null node");
        return false;
    }

    if (is_running_.load()) {
        ESP_LOGE(TAG, "Cannot add node while executor is running");
        return false;
    }

    if (nodes_.size() >= config_.max_nodes) {
        ESP_LOGE(TAG, "Cannot add node: maximum nodes (%zu) reached", config_.max_nodes);
        return false;
    }

    // Check for duplicate names
    const char* name = node->getName();
    for (const auto& existing : nodes_) {
        if (existing && strcmp(existing->getName(), name) == 0) {
            ESP_LOGE(TAG, "Cannot add node: name '%s' already exists", name);
            return false;
        }
    }

    nodes_.push_back(node);
    ESP_LOGI(TAG, "Added node: %s", name);
    return true;
}

bool Executor::removeNode(const NodePtr& node) {
    if (!node) {
        return false;
    }

    if (is_running_.load()) {
        ESP_LOGE(TAG, "Cannot remove node while executor is running");
        return false;
    }

    auto it = std::find(nodes_.begin(), nodes_.end(), node);
    if (it != nodes_.end()) {
        if ((*it)->getState() == Node::State::ACTIVE) {
            (*it)->deactivate();
        }
        nodes_.erase(it);
        ESP_LOGI(TAG, "Removed node: %s", node->getName());
        return true;
    }
    return false;
}

bool Executor::initializeNodes() {
    ESP_LOGI(TAG, "Initializing %zu nodes...", nodes_.size());

    for (auto& node : nodes_) {
        if (!node) {
            continue;
        }
        if (!node->initialize()) {
            ESP_LOGE(TAG, "Failed to initialize node: %s", node->getName());
            return false;
        }
        ESP_LOGD(TAG, "Initialized node: %s", node->getName());
    }

    ESP_LOGI(TAG, "All nodes initialized successfully");
    return true;
}

bool Executor::activateNodes() {
    ESP_LOGI(TAG, "Activating %zu nodes...", nodes_.size());

    for (auto& node : nodes_) {
        if (!node) {
            continue;
        }
        if (!node->activate()) {
            ESP_LOGE(TAG, "Failed to activate node: %s", node->getName());
            deactivateNodes();
            return false;
        }
        ESP_LOGD(TAG, "Activated node: %s", node->getName());
    }

    ESP_LOGI(TAG, "All nodes activated successfully");
    return true;
}

bool Executor::deactivateNodes() {
    ESP_LOGI(TAG, "Deactivating nodes...");
    bool success = true;

    for (auto& node : nodes_) {
        if (!node) {
            continue;
        }
        if (node->getState() == Node::State::ACTIVE) {
            if (!node->deactivate()) {
                ESP_LOGE(TAG, "Failed to deactivate node: %s", node->getName());
                success = false;
            }
        }
    }
    return success;
}

bool Executor::start() {
    if (is_running_.load()) {
        ESP_LOGW(TAG, "Executor already running");
        return false;
    }

    should_stop_.store(false);
    emergency_stop_.store(false);
    soft_stop_.store(false);
    last_watchdog_feed_ = esp_timer_get_time();

    BaseType_t result = pdPASS;
#if defined(ESP_PLATFORM)
    if (config_.executor_task_core >= 0) {
        result = xTaskCreatePinnedToCore(taskWrapper, "chopper_exec",
                                         8192,  // 8KB stack as recommended by architecture doc
                                         this, configMAX_PRIORITIES - 1, &task_handle_, config_.executor_task_core);
    } else
#endif
    {
        result = xTaskCreate(taskWrapper, "chopper_exec",
                             8192,  // 8KB stack as recommended by architecture doc
                             this, configMAX_PRIORITIES - 1, &task_handle_);
    }

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create executor task");
        return false;
    }

    is_running_.store(true);
    ESP_LOGI(TAG, "Executor started");
    return true;
}

bool Executor::stop() {
    if (!is_running_.load()) {
        return true;
    }

    should_stop_.store(true);

    // Wait for task to finish gracefully (up to 500ms)
    if (task_handle_ != nullptr) {
        for (int i = 0; i < 50; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
            if (!is_running_.load()) {
                break;
            }
        }

        if (is_running_.load() && eTaskGetState(task_handle_) != eDeleted) {
            ESP_LOGW(TAG, "Executor task did not exit gracefully, force deleting");
            vTaskDelete(task_handle_);
        }

        task_handle_ = nullptr;
    }

    is_running_.store(false);
    ESP_LOGI(TAG, "Executor stopped");
    return true;
}

void Executor::emergencyStop(const char* reason) {
    bool expected = false;
    if (emergency_stop_.compare_exchange_strong(expected, true)) {
        stats_.emergency_stops++;

        ESP_LOGE(TAG, "EMERGENCY STOP: %s", reason);

        // Emergency stop all nodes
        for (auto& node : nodes_) {
            if (node && node->getState() == Node::State::ACTIVE) {
                node->emergencyStop();
            }
        }

        // Call user callback if set
        if (emergency_callback_ != nullptr) {
            emergency_callback_(reason, emergency_callback_context_);
        }
    }
}

void Executor::softStop(const char* reason) {
    bool was_soft = soft_stop_.exchange(true);
    if (!was_soft) {
        ESP_LOGW(TAG, "SOFT STOP: %s", reason ? reason : "(none)");
    }
    for (auto& node : nodes_) {
        if (node && node->getState() == Node::State::ACTIVE) {
            if (isSoftStopExcludedNode(node->getName())) {
                continue;
            }
            node->emergencyStop();
        }
    }
}

void Executor::clearSoftStop(const char* reason) {
    bool was_soft = soft_stop_.exchange(false);
    if (was_soft) {
        ESP_LOGW(TAG, "SOFT STOP CLEARED: %s", reason ? reason : "(none)");
    }
}

void Executor::taskWrapper(void* parameter) {
    auto* executor = static_cast<Executor*>(parameter);
    executor->executionLoop();
    vTaskDelete(nullptr);
}

void Executor::executionLoop() {
    ESP_LOGI(TAG, "Execution loop started");

    const TickType_t period_ticks = pdMS_TO_TICKS(1000 / config_.loop_frequency_hz);
    TickType_t last_wake_time = xTaskGetTickCount();

    while (!should_stop_.load() && !emergency_stop_.load()) {
        uint64_t loop_start = esp_timer_get_time();

        // Process messages (reserved for future batched delivery)
        message_broker_.processPendingMessages();

        // Process all nodes
        uint64_t execution_time = processNodes(loop_start);

        // Check safety conditions
        checkSafety(execution_time);

        // Update statistics
        if (config_.enable_statistics) {
            updateStatistics(execution_time);
        }

        // Feed watchdog
        feedWatchdog();

        // Use vTaskDelayUntil for deterministic periodic timing
        if (period_ticks > 0) {
            vTaskDelayUntil(&last_wake_time, period_ticks);
        } else {
            taskYIELD();
        }
    }

    is_running_.store(false);
    ESP_LOGI(TAG, "Execution loop finished");
}

uint64_t Executor::processNodes(uint64_t now) {
    uint64_t start_time = esp_timer_get_time();

    for (auto& node : nodes_) {
        if (!node || node->getState() != Node::State::ACTIVE) {
            continue;
        }

        if (soft_stop_.load()) {
            const char* name = node->getName();
            const bool allowed = (strcmp(name, "bluepad_input") == 0) || (strcmp(name, "safety") == 0) ||
                                 (strcmp(name, "telemetry_node") == 0) || (strcmp(name, "telemetry_io_tap") == 0);
            if (!allowed) {
                continue;
            }
        }

        // Check if node should be processed based on frequency
        double frequency = node->getUpdateFrequency();
        if (frequency > 0.0) {
            auto period_us = static_cast<uint64_t>(1000000.0 / frequency);
            uint64_t last_process = node->getLastProcessTime();

            // last_process == 0 means never processed, so run immediately
            if (last_process > 0 && (now - last_process) < period_us) {
                continue;
            }
        }

        // Process node with execution time monitoring
        uint64_t node_start = esp_timer_get_time();

        // Important: process() is cooperative and non-preemptive in this task.
        // If a node blocks here, other nodes are delayed until it returns.
        // Timeout handling below is post-facto (it cannot interrupt mid-call).
        node->process(now);

        // Update last process time so frequency gating works
        node->setLastProcessTime(now);

        uint64_t node_time = esp_timer_get_time() - node_start;

        // Check execution time limit
        if (node_time > node->getMaxExecutionTime()) {
            ESP_LOGW(TAG, "Node %s exceeded exec time: %llu us (limit: %llu us)", node->getName(), node_time,
                     node->getMaxExecutionTime());
            stats_.missed_deadlines++;

            if (node_time > node->getMaxExecutionTime() * 2) {
                if (config_.node_timeout_triggers_estop) {
                    emergencyStop("Node timeout");
                    break;
                }
                ESP_LOGE(TAG, "Node %s timeout -> node emergencyStop only (executor continues)", node->getName());
                node->emergencyStop();
            }
        }
    }

    return esp_timer_get_time() - start_time;
}

void Executor::checkSafety(uint64_t execution_time_us) {
    uint64_t now = esp_timer_get_time();

    if (execution_time_us > config_.max_loop_time_us) {
        ESP_LOGW(TAG, "Loop exec time exceeded: %llu us (limit: %u us)", execution_time_us, config_.max_loop_time_us);
        stats_.missed_deadlines++;

        if (execution_time_us > static_cast<uint64_t>(config_.max_loop_time_us) * 2) {
            if (config_.loop_timeout_triggers_estop) {
                emergencyStop("Loop timeout");
                return;
            }
        }
    }

    if ((now - last_watchdog_feed_) > (config_.watchdog_timeout_ms * 1000ULL)) {
        emergencyStop("Watchdog timeout");
        stats_.watchdog_resets++;
        return;
    }

    last_loop_time_ = now;
}

void Executor::updateStatistics(uint64_t execution_time_us) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    stats_.loop_count++;

    if (execution_time_us > stats_.max_loop_time_us) {
        stats_.max_loop_time_us = execution_time_us;
    }

    loop_time_accumulator_ += execution_time_us;
    stats_.avg_loop_time_us = loop_time_accumulator_ / stats_.loop_count;

    stats_.total_nodes = nodes_.size();
    stats_.active_nodes = 0;
    for (const auto& node : nodes_) {
        if (node && node->getState() == Node::State::ACTIVE) {
            stats_.active_nodes++;
        }
    }
}

Executor::Statistics Executor::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void Executor::resetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = Statistics{};
    loop_time_accumulator_ = 0;
}

void Executor::setEmergencyStopCallback(EmergencyStopCallback callback, void* context) {
    emergency_callback_ = callback;
    emergency_callback_context_ = context;
}

void Executor::feedWatchdog() {
    last_watchdog_feed_ = esp_timer_get_time();
}

}  // namespace chopper::core
