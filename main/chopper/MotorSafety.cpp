// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "chopper/MotorSafety.h"

#include <inttypes.h>
#include <algorithm>
#include <utility>
#include <thread>
#include <atomic>
#include <vector>
#include <Bluepad32.h>

// #include <hal/DriverStation.h>
// #include <wpi/SafeThread.h>
// #include <wpi/SmallPtrSet.h>

// #include "frc/DriverStation.h"
// #include "frc/Errors.h"

// using namespace frc;
#include "wpi/condition_variable.h"
#include "wpi/mutex.h"
#include <esp_pthread.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>

namespace {
class Thread {
 public:
  Thread() { }
  void Main();
  void Start(std::string name);
  void Stop();
  // void Join();
  
  esp_pthread_cfg_t m_cfg = esp_pthread_get_default_config();

  mutable wpi::mutex m_mutex;
  std::thread m_stdThread;
  std::atomic_bool m_active{true};
  // std::weak_ptr<SafeThreadBase> m_thread;
  std::atomic_bool m_joinAtExit{true};
  std::thread::id m_threadId;
  wpi::condition_variable m_cond;
};

void Thread::Main() {
  Console.printf("Thread::Main [%s]\n", m_cfg.thread_name);
  int safetyCounter = 0;
  while (m_active) {
    if (++safetyCounter >= 4) {
      MotorSafety::CheckMotors();
      safetyCounter = 0;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  // wpi::Event event{false, false};
  // HAL_ProvideNewDataEventHandle(event.GetHandle());

  // int safetyCounter = 0;
  // while (m_active) {
  //   bool timedOut = false;
  //   bool signaled = wpi::WaitForObject(event.GetHandle(), 0.1, &timedOut);
  //   if (signaled) {
  //     HAL_ControlWord controlWord;
  //     std::memset(&controlWord, 0, sizeof(controlWord));
  //     HAL_GetControlWord(&controlWord);
  //     if (!(controlWord.enabled && controlWord.dsAttached)) {
  //       safetyCounter = 0;
  //     }
  //     if (++safetyCounter >= 4) {
  //       MotorSafety::CheckMotors();
  //       safetyCounter = 0;
  //     }
  //   } else {
  //     safetyCounter = 0;
  //   }
  // }

  // HAL_RemoveNewDataEventHandle(event.GetHandle());
}

void Thread::Start(std::string name) {
  m_cfg.thread_name = name.c_str();
  m_cfg.pin_to_core = 1;
  m_cfg.stack_size = 3 * 1024;
  m_cfg.prio = 5;
  esp_pthread_set_cfg(&m_cfg);
  m_stdThread = std::thread([this] { Main(); });
}

void Thread::Stop() {
  m_active = false;
  m_cond.notify_all();
}

// void Thread::Join() {
//   std::unique_lock lock(m_mutex);
//   if (auto thr = m_thread.lock()) {
//     auto stdThread = std::move(m_stdThread);
//     m_thread.reset();
//     lock.unlock();
//     thr->Stop();
//     stdThread.join();
//   } else if (m_stdThread.joinable()) {
//     m_stdThread.detach();
//   }
// }
}  // namespace
static std::atomic_bool gShutdown{false};

namespace {
struct MotorSafetyManager {
  ~MotorSafetyManager() { gShutdown = true; }

  Thread thread;
  std::vector<MotorSafety*> instanceList;
  wpi::mutex listMutex;
  bool threadStarted = false;
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
  if (!manager.threadStarted) {
    manager.threadStarted = true;
    manager.thread.Start("Thread n");
    // manager.thread.m_stdThread = std::thread(&Thread::Main, &manager.thread);
  }
}

MotorSafety::~MotorSafety() {
  auto& manager = GetManager();
  std::scoped_lock lock(manager.listMutex);
  manager.thread.Stop();
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
  // if (!enabled || DriverStation::IsDisabled() || DriverStation::IsTest()) {
  //   return;
  // }

  if (stopTime < Timer::GetFPGATimestamp()) {
    // FRC_ReportError(err::Timeout,
    //                 "{}... Output not updated often enough. See "
    //                 "https://docs.wpilib.org/motorsafety for more information.",
    //                 GetDescription());
    // Console.printf("MotorSafety::Check [%s] -> %llu\n", stopTime, GetDescription());
    StopMotor();
    // try {
    //   StopMotor();
    // } catch (frc::RuntimeError& e) {
    //   e.Report();
    // } catch (std::exception& e) {
    //   FRC_ReportError(err::Error, "{} StopMotor threw unexpected exception: {}",
    //                   GetDescription(), e.what());
    // }
  }
}

void MotorSafety::CheckMotors() {
  auto& manager = GetManager();
  std::scoped_lock lock(manager.listMutex);
  for (auto elem : manager.instanceList) {
    elem->Check();
  }
}
