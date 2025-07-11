#ifndef CHOPPER_CORE_BUTTONSTATE_H
#define CHOPPER_CORE_BUTTONSTATE_H

#include <cstdint>
#include <cmath>
#include "include/chopper/Timer.h"

class ButtonState {
public:
    // Maximum size of press history array (fixed to conserve memory)
    static const uint8_t MAX_HISTORY_SIZE = 10;

    // Bit flags for button states
    enum InternalStates {
        PRESSED = 0x01,           // Button is currently pressed
        JUST_PRESSED = 0x02,      // Button was pressed this frame
        JUST_RELEASED = 0x04,     // Button was released this frame
        DOUBLE_CLICKED = 0x08,    // Button was double-clicked
        LONG_PRESSED = 0x10,      // Button is being held longer than threshold
        SEQUENCE_MATCH = 0x20,    // Button is part of a matched sequence
        COMBO_MATCH = 0x40        // Button is part of a combo
    };

    enum class State : uint8_t
    {
        None = 0,
        Pressed = PRESSED,
        JustPressed = JUST_PRESSED,
        JustReleased = JUST_RELEASED,
        DoubleClicked = DOUBLE_CLICKED,
        LongPressed = LONG_PRESSED,
        SequenceMatch = SEQUENCE_MATCH,
        ComboMatch = COMBO_MATCH
    };

    // Input timing configuration
    struct Config {
        uint32_t doubleClickThresholdMs;   // Max time between clicks for double-click
        uint32_t longPressThresholdMs;     // Min time for long press
        uint32_t sequenceTimeWindowMs;     // Max time window for sequence detection
        uint32_t comboTimeWindowMs;        // Max time window for combo (simultaneous) presses

        // Constructor with default values
        Config() : 
            doubleClickThresholdMs(500),
            longPressThresholdMs(1000),
            sequenceTimeWindowMs(1500),
            comboTimeWindowMs(200)
        {}
    };

    ButtonState(const Config& config = Config()) 
        : config_(config), 
          state_(0), 
          is_pressed(false), 
          last_press_time(0),
          last_release_time(0),
          history_size_(0),
          history_start_(0) {
        // Initialize the press history array to zeros
        for (uint8_t i = 0; i < MAX_HISTORY_SIZE; i++) {
            press_history_[i] = 0;
        }
    }

    // Implicit bool conversion for simple pressed check
    operator bool() const { return isPressed(); }

    // State accessors - all inline for efficiency
    bool isPressed() const { return state_ & PRESSED; }
    bool isJustPressed() const { return state_ & JUST_PRESSED; }
    bool isJustReleased() const { return state_ & JUST_RELEASED; }
    bool isDoubleClicked() const { return state_ & DOUBLE_CLICKED; }
    bool isLongPressed() const { return state_ & LONG_PRESSED; }
    bool isSequenceMatch() const { return state_ & SEQUENCE_MATCH; }
    bool isComboMatch() const { return state_ & COMBO_MATCH; }

    // Time accessors
    uint64_t lastPressTime() const { return last_press_time; }
    uint64_t lastReleaseTime() const { return last_release_time; }
    
    // Duration accessors
    uint64_t pressedDuration() const {
        if (!isPressed()) return 0;
        return Timer::GetFPGATimestamp() - last_press_time;
    }

    uint64_t releasedDuration() const {
        if (isPressed()) return 0;
        return Timer::GetFPGATimestamp() - last_release_time;
    }

    // Duration-based checks
    bool isPressedFor(uint32_t holdThreshold = 1000) const {
        if (!isPressed()) return false;
        // Cache timestamp to avoid multiple calls
        uint64_t current = Timer::GetFPGATimestamp();
        return (current - last_press_time) >= holdThreshold;
    }

    bool isReleasedFor(uint32_t releaseThreshold = 1000) const {
        if (isPressed()) return false;
        // Cache timestamp to avoid multiple calls
        uint64_t current = Timer::GetFPGATimestamp();
        return (current - last_release_time) >= releaseThreshold;
    }

    // Update and track button state - optimized for esp32
    void updateState(bool button_down) {
        // Cache timestamp - single call to reduce overhead
        uint64_t currentMillis = Timer::GetFPGATimestamp();
        
        // Clear one-frame states
        state_ &= ~(JUST_PRESSED | JUST_RELEASED | DOUBLE_CLICKED);
        
        // Handle button press
        if (button_down) {
            if (!is_pressed) {
                // Just pressed this frame
                state_ |= JUST_PRESSED;
                
                // Add to circular buffer efficiently without dynamic allocation
                addPressToHistory(currentMillis);
                
                // Check for double click
                if (history_size_ >= 2) {
                    uint64_t prevPress = getPressHistoryEntry(1);  // Get second-most recent entry
                    if (prevPress != 0 && currentMillis - prevPress <= config_.doubleClickThresholdMs) {
                        state_ |= DOUBLE_CLICKED;
                    }
                }
                
                last_press_time = currentMillis;
                is_pressed = true;
            }
            
            // Set pressed state
            state_ |= PRESSED;
            
            // Check for long press
            if (currentMillis - last_press_time >= config_.longPressThresholdMs) {
                state_ |= LONG_PRESSED;
            }
        } 
        else {
            // Button released
            if (is_pressed) {
                state_ |= JUST_RELEASED;
                last_release_time = currentMillis;
            }
            
            // Clear pressed states
            state_ &= ~(PRESSED | LONG_PRESSED);
            is_pressed = false;
        }
    }

    // Optimized sequence detection method that avoids dynamic memory allocations
    bool checkSequence(const uint32_t* pattern, uint8_t patternSize) const {
        if (history_size_ < patternSize) return false;
        
        // Get current time once to avoid multiple calls
        uint64_t currentMillis = Timer::GetFPGATimestamp();
        
        // Check if the entire sequence is within the time window
        uint64_t oldestPress = getPressHistoryEntry(patternSize - 1);
        if (oldestPress == 0 || currentMillis - oldestPress > config_.sequenceTimeWindowMs) {
            return false;
        }
        
        // Match the pattern - direct array access is more efficient
        for (uint8_t i = 0; i < patternSize - 1; i++) {
            uint64_t time1 = getPressHistoryEntry(i);
            uint64_t time2 = getPressHistoryEntry(i + 1);
            
            // Calculate time between consecutive presses
            if (time1 == 0 || time2 == 0) return false;
            
            uint64_t elapsed = time1 - time2;
            
            // Each value in the pattern represents the max allowed time between presses
            if (elapsed > pattern[i]) {
                return false;
            }
        }
        
        return true;
    }
    
    // Optimized rhythm detection that avoids dynamic allocations
    bool checkRhythm(const uint32_t* rhythmPattern, uint8_t patternSize) const {
        if (history_size_ < patternSize + 1) return false;
        
        for (uint8_t i = 0; i < patternSize; i++) {
            uint64_t time1 = getPressHistoryEntry(i);
            uint64_t time2 = getPressHistoryEntry(i + 1);
            
            if (time1 == 0 || time2 == 0) return false;
            
            uint64_t actualGap = time1 - time2;
            
            // Allow 20% tolerance in rhythm timing
            uint32_t tolerance = rhythmPattern[i] / 5;
            if (actualGap < rhythmPattern[i] - tolerance || actualGap > rhythmPattern[i] + tolerance) {
                return false;
            }
        }
        
        return true;
    }

    // Efficiently detect combo presses with another button
    bool checkCombo(const ButtonState& otherButton) const {
        // Early-out check for better performance
        if (!isPressed() || !otherButton.isPressed()) return false;
        
        // Check if both buttons are pressed within the combo time window
        int64_t timeDiff = static_cast<int64_t>(last_press_time) - static_cast<int64_t>(otherButton.last_press_time);
        return (timeDiff >= 0 ? timeDiff : -timeDiff) <= config_.comboTimeWindowMs;
    }
    
    // Reset all state history - efficient implementation
    void reset() {
        state_ = 0;
        is_pressed = false;
        history_size_ = 0;
        history_start_ = 0;
        last_press_time = 0;
        last_release_time = 0;
        
        // Clear history buffer
        for (uint8_t i = 0; i < MAX_HISTORY_SIZE; i++) {
            press_history_[i] = 0;
        }
    }

private:
    // Helper method to add a timestamp to the circular buffer
    void addPressToHistory(uint64_t timestamp) {
        // Calculate the next position in the circular buffer
        uint8_t nextPos = (history_start_ + history_size_) % MAX_HISTORY_SIZE;
        
        // If buffer is full, we need to make room by removing oldest entry
        if (history_size_ >= MAX_HISTORY_SIZE) {
            history_start_ = (history_start_ + 1) % MAX_HISTORY_SIZE;
            history_size_--;
        }
        
        // Add new timestamp at calculated position
        press_history_[nextPos] = timestamp;
        history_size_++;
    }
    
    // Get an entry from the press history by relative index (0 = most recent)
    uint64_t getPressHistoryEntry(uint8_t index) const {
        if (index >= history_size_) return 0; // Out of bounds
        
        // Calculate the actual array index from the relative index
        uint8_t actualIndex = (history_start_ + history_size_ - 1 - index) % MAX_HISTORY_SIZE;
        return press_history_[actualIndex];
    }
    
    Config config_;                     // Configuration parameters
    uint8_t state_;                     // Bitmask for button states
    bool is_pressed;                    // Current press state
    uint64_t last_press_time;           // Last time button was pressed
    uint64_t last_release_time;         // Last time button was released
    uint64_t press_history_[MAX_HISTORY_SIZE];  // Fixed-size array for press history
    uint8_t history_size_;              // Current number of entries in the history
    uint8_t history_start_;             // Start index of the circular buffer
};

#endif // CHOPPER_CORE_BUTTONSTATE_H