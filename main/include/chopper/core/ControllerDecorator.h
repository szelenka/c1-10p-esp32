#pragma once

#include <Bluepad32.h>
#include <ArduinoController.h>
#include "chopper/filter/SlewRateLimiter.h"

class ControllerDecorator {
public:
    ControllerDecorator(ControllerPtr ctl) : m_ctl(ctl) {}
    ~ControllerDecorator()
    {
        if (m_axisXslew) { delete m_axisXslew; }
        if (m_axisYslew) { delete m_axisYslew; }
        if (m_axisRXslew) { delete m_axisRXslew; }
        if (m_axisRYslew) { delete m_axisRYslew; }
    }
    //
    // Gamepad Related
    //
    uint8_t dpad() const { return m_ctl->dpad(); }

    // Axis
    int32_t axisX() const { return m_axisXInvert ? m_ctl->axisX()*-1 : m_ctl->axisX(); }
    int32_t axisY() const { return m_axisYInvert ? m_ctl->axisY()*-1 : m_ctl->axisY(); }
    int32_t axisRX() const { return m_axisRXInvert ? m_ctl->axisRX()*-1 : m_ctl->axisRX(); }
    int32_t axisRY() const { return m_axisRYInvert ? m_ctl->axisRY()*-1 : m_ctl->axisRY(); }

    // Brake & Throttle
    int32_t brake() const { return m_ctl->brake(); }
    int32_t throttle() const { return m_ctl->throttle(); }

    // Gyro / Accel
    int32_t gyroX() const { return m_ctl->gyroX(); }
    int32_t gyroY() const { return m_ctl->gyroY(); }
    int32_t gyroZ() const { return m_ctl->gyroZ(); }
    int32_t accelX() const { return m_ctl->accelX(); }
    int32_t accelY() const { return m_ctl->accelY(); }
    int32_t accelZ() const { return m_ctl->accelZ(); }

    uint16_t buttons() const { return m_ctl->buttons(); }
    uint16_t miscButtons() const { return m_ctl->miscButtons(); }

    bool a() const { return m_ctl->a(); }
    bool b() const { return m_ctl->b(); }
    bool x() const { return m_ctl->x(); }
    bool y() const { return m_ctl->y(); }
    bool l1() const { return m_ctl->l1(); }
    bool l2() const { return m_ctl->l2(); }
    bool r1() const { return m_ctl->r1(); }
    bool r2() const { return m_ctl->r2(); }
    bool thumbL() const { return m_ctl->thumbL(); }
    bool thumbR() const { return m_ctl->thumbR(); }

    // Misc buttons
    bool miscSystem() const { return m_ctl->miscSystem(); }
    bool miscSelect() const { return m_ctl->miscSelect(); }
    bool miscStart() const { return m_ctl->miscStart(); }
    bool miscCapture() const { return m_ctl->miscCapture(); }

    //
    // Shared among all
    //

    // 0 = Unknown Battery state
    // 1 = Battery Empty
    // 255 = Battery full
    uint8_t battery() const { return m_ctl->battery(); }

    // Returns whether the controller has received data since the last time BP32.updated() was called.
    bool hasData() const { return m_ctl->hasData(); }

    bool isGamepad() const { return m_ctl->isGamepad(); }
    bool isMouse() const { return m_ctl->isMouse(); }
    bool isBalanceBoard() const { return m_ctl->isBalanceBoard(); }
    bool isKeyboard() const { return m_ctl->isKeyboard(); }
    int8_t index() const { return m_ctl->index(); }

    bool isConnected() const { return m_ctl->isConnected(); }
    void disconnect() { m_ctl->disconnect(); }

    uni_controller_class_t getClass() const { return m_ctl->getClass(); }
    // Returns the controller model.
    int getModel() const { return m_ctl->getModel(); }
    String getModelName() const { return m_ctl->getModelName(); }
    ControllerProperties getProperties() const { return m_ctl->getProperties(); }

    // "Output" functions.

    // Bitmap for the LEDs to turn on / off.
    void setPlayerLEDs(uint8_t led) const { m_ctl->setPlayerLEDs(led); }

    // RGB for the lightbar in controllers like DualShock 4 and DualSense.
    void setColorLED(uint8_t red, uint8_t green, uint8_t blue) const { m_ctl->setColorLED(red, green, blue); }
    void playDualRumble(uint16_t delayedStartMs,
                        uint16_t durationMs,
                        uint8_t weakMagnitude,
                        uint8_t strongMagnitude) const { m_ctl->playDualRumble(delayedStartMs, durationMs, weakMagnitude, strongMagnitude); }


/*
    Sabertooth has range -127 to 127, meaning there are only 255 steps we can take
    JoyCon has input in range -512 to 512, meaning there are only 1024 steps we can take
      fraction  || decimal      || step (255)   || full-to-stop
    - 1/1024    ~= 0.0009766    ~= +/- 0
    - 1/255     ~= 0.003922/s   ~= +/- 1/s
    - 1/4       ~= 0.25         ~= +/- 64/s
    - 1/2       ~= 0.5/s        ~= +/- 127/s
    -           ~= 0.75/s       ~= +/- 192/s    ~= 1.3s
    - 2/1       ~= 2.0/s        ~= +/- 510/s    ~= 0.5s
    - 4/1       ~= 4.0/s        ~= +/- 1020/s   ~= 0.3s       
    - 6/1       ~= 6.0/s        ~= +/- 1530/s   ~= 0.001s  
    - 8/1       ~= 8.0/s        ~= +/- 2040/s   ~= 0.001s
*/
    //     // TODO: similar logic to this
    //    Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
    // char * bd_addr_to_str(int8_t bt_addr[6])
    // {
    //     static char bd_addr_to_str_buffer[6*3];  // 12:45:78:01:34:67\0
    //     char * p = bd_addr_to_str_buffer;
    //     int i;
    //     for (i = 0; i < 6 ; i++) {
    //         uint8_t byte = addr[i];
    //         *p++ = char_for_high_nibble(byte);
    //         *p++ = char_for_low_nibble(byte);
    //         *p++ = delimiter;
    //     }
    //     *--p = 0;
    //     return (char *) bd_addr_to_str_buffer;
    // }
    void setInputRange(int32_t _min, int32_t _max) {
        if (_max <= _min)
        {
            Console.println("[ControllerDecorator::setInputRange] max is less-than-or-equal-to min");
        }
        m_maxInput = _max;
        m_minInput = _min;
    }
    void setOutputRange(int32_t _min, int32_t _max) {
        if (_max <= _min)
        {
            Console.println("[ControllerDecorator::setOutputRange] max is less-than-or-equal-to min");
        }
        m_maxOutput = _max;
        m_minOutput = _min;
    }
    void setAxisXOffset(int32_t offset) { m_axisXOffset = offset; }
    void setAxisYOffset(int32_t offset) { m_axisYOffset = offset; }
    void setAxisRXOffset(int32_t offset) { m_axisRXOffset = offset; }
    void setAxisRYOffset(int32_t offset) { m_axisRYOffset = offset; }

    void setAxisXInvert(bool invert) { m_axisXInvert = invert; }
    void setAxisYInvert(bool invert) { m_axisYInvert = invert; }
    void setAxisRXInvert(bool invert) { m_axisRXInvert = invert; }
    void setAxisRYInvert(bool invert) { m_axisRYInvert = invert; }

    void setAxisXSlew(int32_t positiveStep, int32_t negativeStep)
    { 
        setAxisXSlew(normalizeInput(positiveStep), normalizeInput(negativeStep)); 
    }
    void setAxisXSlew(double positiveStep, double negativeStep)
    {
        if (positiveStep <= negativeStep) {
            Console.println("[ControllerDecorator::setAxisXSlew] positiveStep is less-than-or-equal-to negativeStep");
        }
        if (m_axisXslew)
        {
            m_axisXslew->Reset(positiveStep, negativeStep);
        } 
        else 
        {
            m_axisXslew = new SlewRateLimiter(positiveStep, negativeStep);
        }
    }
    void setAxisYSlew(int32_t positiveStep, int32_t negativeStep)
    { 
        setAxisYSlew(normalizeInput(positiveStep), normalizeInput(negativeStep)); 
    }
    void setAxisYSlew(double positiveStep, double negativeStep) {
        if (positiveStep <= negativeStep) {
            Console.println("[ControllerDecorator::setAxisYSlew] positiveStep is less-than-or-equal-to negativeStep");
        }
        if (m_axisYslew)
        {
            m_axisYslew->Reset(positiveStep, negativeStep);
        }
        else
        {
            m_axisYslew = new SlewRateLimiter(positiveStep, negativeStep);
        }
    }
    // void SetAxisRXSlew(double positiveStep, double negativeStep) { m_axisRXslew(positiveStep, negativeStep); }
    // void SetAxisRYSlew(double positiveStep, double negativeStep) { m_axisRYslew(positiveStep, negativeStep); }
    double axisXslew() { 
        if (!m_axisXslew)
        {
            m_axisXslew = new SlewRateLimiter(kDefaultPositiveLimit, kDefaultNegativeLimit);
        }
        return m_axisXslew->Calculate(normalizeInput(axisX()+m_axisXOffset));
    }
    double axisYslew() {
        if (!m_axisYslew)
        {
            m_axisYslew = new SlewRateLimiter(kDefaultPositiveLimit, kDefaultNegativeLimit);
        }
        return m_axisYslew->Calculate(normalizeInput(axisY()+m_axisYOffset));
    }
    // double axisRXslew() const { return m_axisRXslew.Calculate(normalizeInput(this->axisRX()+m_axisRXOffset)); }
    // double axisRYslew() const { return m_axisRYslew.Calculate(normalizeInput(this->axisRY()+m_axisRYOffset)); }

    double normalizeInput(int32_t rawValue) 
    {
        return std::clamp(
            (m_maxOutput-m_minOutput)*((rawValue-m_minInput)/(double)(m_maxInput-m_minInput))+(m_minOutput),
            m_minOutput, 
            m_maxOutput
        );
    }

private:

    static constexpr double kDefaultPositiveLimit = 0.75;
    static constexpr double kDefaultNegativeLimit = -0.75;
    static constexpr int32_t kMaxInput = 512;
    static constexpr int32_t kMinInput = -512;
    static constexpr double kMaxOutput = 1.0;
    static constexpr double kMinOutput = -1.0;
  
    int32_t m_maxInput = kMaxInput;
    int32_t m_minInput = kMinInput;
    double m_maxOutput = kMaxOutput;
    double m_minOutput = kMinOutput;

    SlewRateLimiter* m_axisXslew = nullptr;
    SlewRateLimiter* m_axisYslew = nullptr;
    SlewRateLimiter* m_axisRXslew = nullptr;
    SlewRateLimiter* m_axisRYslew = nullptr;

    int32_t m_axisXOffset = 0;
    int32_t m_axisYOffset = 0;
    int32_t m_axisRXOffset = 0;
    int32_t m_axisRYOffset = 0;

    bool m_axisXInvert = false;
    bool m_axisYInvert = false;
    bool m_axisRXInvert = false;
    bool m_axisRYInvert = false;

    ControllerPtr m_ctl;
};

typedef ControllerDecorator* ControllerDecoratorPtr;
