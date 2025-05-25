#include "chopper/MotorSafety.h"

#include <inttypes.h>
#include <algorithm>
#include <utility>
#include <atomic>
#include <vector>
#include <Bluepad32.h>
#include "wpi/condition_variable.h"
#include "wpi/mutex.h"
#include <esp_pthread.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
class Task {
 public:
  Task() { }

  // Main loop function for the FreeRTOS task
  static void Main(void* pvParameters);

  void Start(std::string name);
  void Stop();

 private:
  static constexpr size_t STACK_SIZE = 3 * 1024;  // Adjust stack size as needed
  static constexpr UBaseType_t TASK_PRIORITY = 5; // Task priority

  std::atomic_bool m_active{true};
  wpi::condition_variable m_cond;
  std::string m_name;
};

// Task's main loop function
void Task::Main(void* pvParameters) {
  Task* task = static_cast<Task*>(pvParameters);
  Console.printf("Task::Main [%s]\n", task->m_name.c_str());
  
  int safetyCounter = 0;
  while (task->m_active) {
    if (++safetyCounter >= 4) {
      MotorSafety::CheckMotors();
      safetyCounter = 0;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);  // Delay 100 ms between checks
  }
}

void Task::Start(std::string name) {
  m_name = std::move(name);

  // Create the FreeRTOS task
  xTaskCreatePinnedToCore(
      Main,              // Task function
      m_name.c_str(),    // Task name
      STACK_SIZE,        // Stack size (in words, not bytes)
      this,              // Parameter to pass to the task (this pointer)
      TASK_PRIORITY,     // Task priority
      nullptr,           // Handle for the task (not needed here)
      1);                // Pin to core 1
}

void Task::Stop() {
  m_active = false;
  m_cond.notify_all();
}
}  // namespace

static std::atomic_bool gShutdown{false};

namespace {
struct MotorSafetyManager {
  ~MotorSafetyManager() { gShutdown = true; }

  Task task;  // Replacing the thread with a FreeRTOS task
  std::vector<MotorSafety*> instanceList;
  wpi::mutex listMutex;
  bool taskStarted = false;
};
}  // namespace

static MotorSafetyManager& GetManager() {
  static MotorSafetyManager manager;
  return manager;
}

MotorSafety::MotorSafety() {
  auto& manager = GetManager();
  std::scoped_lock lock(manager.listMutex);
  manager.instanceList.push_back(this);
  if (!manager.taskStarted) {
    manager.taskStarted = true;
    manager.task.Start("MotorSafetyTask");
  }
}

MotorSafety::~MotorSafety() {
  auto& manager = GetManager();
  std::scoped_lock lock(manager.listMutex);
  manager.task.Stop();
  manager.instanceList.erase(std::remove(manager.instanceList.begin(), manager.instanceList.end(), this), manager.instanceList.end());
}

MotorSafety::MotorSafety(MotorSafety&& rhs)
    : m_expiration(std::move(rhs.m_expiration)),
      m_enabled(std::move(rhs.m_enabled)),
      m_stopTime(std::move(rhs.m_stopTime)) {}

MotorSafety& MotorSafety::operator=(MotorSafety&& rhs) {
  std::scoped_lock lock(m_thisMutex, rhs.m_thisMutex);

  m_expiration = std::move(rhs.m_expiration);
  m_enabled = std::move(rhs.m_enabled);
  m_stopTime = std::move(rhs.m_stopTime);

  return *this;
}

void MotorSafety::Feed() {
  std::scoped_lock lock(m_thisMutex);
  m_stopTime = Timer::GetFPGATimestamp() + m_expiration;
}

void MotorSafety::SetExpiration(uint64_t expirationTime) {
  std::scoped_lock lock(m_thisMutex);
  m_expiration = expirationTime;
}

uint64_t MotorSafety::GetExpiration() const {
  std::scoped_lock lock(m_thisMutex);
  return m_expiration;
}

bool MotorSafety::IsAlive() const {
  std::scoped_lock lock(m_thisMutex);
  return !m_enabled || m_stopTime > Timer::GetFPGATimestamp();
}

void MotorSafety::SetSafetyEnabled(bool enabled) {
  std::scoped_lock lock(m_thisMutex);
  m_enabled = enabled;
}

bool MotorSafety::IsSafetyEnabled() const {
  std::scoped_lock lock(m_thisMutex);
  return m_enabled;
}

void MotorSafety::Check() {
  bool enabled;
  uint64_t stopTime;

  {
    std::scoped_lock lock(m_thisMutex);
    enabled = m_enabled;
    stopTime = m_stopTime;
  }

  if (!enabled) {
    return;
  }

  if (stopTime < Timer::GetFPGATimestamp()) {
    StopMotor();
  }
}

void MotorSafety::CheckMotors() {
  auto& manager = GetManager();
  std::scoped_lock lock(manager.listMutex);
  for (auto elem : manager.instanceList) {
    elem->Check();
  }
}
