#ifndef CHOPPER_CORE_BUTTONSTATE_H
#define CHOPPER_CORE_BUTTONSTATE_H

#include <cstdint>
#include <Bluepad32.h>
#inclue "include/Timer.h"

class ButtonState {
public:
    enum InternalStates {
        PRESSED = 0x01,
        DOUBLE_CLICKED = 0x02,
        // Add other states as needed
    };
    enum class State : uint8_t
    {
        None = 0,
        Pressed = PRESSED,
        DoubleClicked = DOUBLE_CLICKED
    };

    ButtonState() 
        : state_(0), is_pressed(false), last_press_time(0), double_click_threshold_ms(500) {}

    operator bool() const { return isPressed(); }

    bool isPressed() const { return state_ & PRESSED; }
    bool isDoubleClicked() const { return state_ & DOUBLE_CLICKED; }

    void updateState(bool button_down)
    {

        unsigned long currentMillis = Timer::GetFPGATimestamp();
        if (button_down)
        {
            if (!is_pressed)
            {
                auto time_since_last_press = currentMillis - last_press_time;
                if (time_since_last_press <= double_click_threshold_ms)
                {
                    state_ |= PRESSED | DOUBLE_CLICKED; // Set Pressed and DoubleClicked bits
                }
                else
                {
                    state_ = PRESSED; // Set Pressed bit
                    last_press_time = currentMillis;
                }
                is_pressed = true;
            }
        }
        else if (is_pressed)
        {
            is_pressed = false;
            state_ &= ~PRESSED; // Clear Pressed bit
            state_ &= ~DOUBLE_CLICKED; // Clear DoubleClicked bit
            last_release_time = currentMillis;
        }
    }

    uint32_t lastPressTime() const
    {
        return last_press_time;
    }

    uint32_t lastReleaseTime() const
    {
        return last_release_time;
    }
    
    uint32_t pressedDuration() const
    {
        unsigned long currentMillis = Timer::GetFPGATimestamp();
        return ((state_ & PRESSED) ? (currentMillis - last_press_time) : 0);
    }

    bool isPressedFor(uint32_t holdThreshold = 1000) const
    {
        unsigned long currentMillis = Timer::GetFPGATimestamp();
        return pressedDuration() >= holdThreshold;
    }

    uint32_t releasedDuration() const
    {
        unsigned long currentMillis = Timer::GetFPGATimestamp();
        return ((state_ & PRESSED) == 0 ? (currentMillis - last_release_time) : 0);
    }

    bool isReleasedFor(uint32_t releaseThreshold = 1000) const
    {
        unsigned long currentMillis = Timer::GetFPGATimestamp();
        return releasedDuration() >= releaseThreshold;
    }

private:
    int double_click_threshold_ms;
    uint32_t last_press_time;
    uint32_t last_release_time;
    bool is_pressed;
    uint8_t state_;              // Bitmask for button state
};

#endif // CHOPPER_CORE_BUTTONSTATE_H