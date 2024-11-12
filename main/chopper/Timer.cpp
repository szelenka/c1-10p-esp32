// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "chopper/Timer.h"

#include <chrono>
#include <thread>
// #include <time.h>

// #include "frc/DriverStation.h"
// #include "RobotController.h"

void Wait(uint64_t milliseconds) {
  std::this_thread::sleep_for(std::chrono::duration<double>(milliseconds/1000.0));}

uint64_t GetTime() {
  // using std::chrono::duration;
  using std::chrono::duration_cast;
  using std::chrono::system_clock;

  return duration_cast<std::chrono::milliseconds>(system_clock::now().time_since_epoch())
          .count();
}

Timer::Timer() {
  Reset();
}

uint64_t Timer::Get() const {
  if (m_running) {
    return (GetFPGATimestamp() - m_startTime) + m_accumulatedTime;
  } else {
    return m_accumulatedTime;
  }
}

void Timer::Reset() {
  m_accumulatedTime = 0;
  m_startTime = GetFPGATimestamp();
}

void Timer::Start() {
  if (!m_running) {
    m_startTime = GetFPGATimestamp();
    m_running = true;
  }
}

void Timer::Restart() {
  if (m_running) {
    Stop();
  }
  Reset();
  Start();
}

void Timer::Stop() {
  if (m_running) {
    m_accumulatedTime = Get();
    m_running = false;
  }
}

bool Timer::HasElapsed(uint64_t period) const {
  return Get() >= period;
}

bool Timer::AdvanceIfElapsed(uint64_t period) {
  if (Get() >= period) {
    // Advance the start time by the period.
    m_startTime += period;
    // Don't set it to the current time... we want to avoid drift.
    return true;
  } else {
    return false;
  }
}

bool Timer::IsRunning() const {
  return m_running;
}

uint64_t Timer::GetFPGATimestamp() {
  return GetTime();
}
