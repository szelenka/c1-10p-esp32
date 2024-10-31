#ifndef Potentiometer_h
#define Potentiometer_h

#include <Arduino.h>

class DomePotentiometer {
public:
    /** \brief Constructor
      *
      * Only a single instance of WifiSerialBridge should be created per sketch.
      *
      * \param port the port number of this service
      */
    DomePotentiometer::DomePotentiometer(int pin, float referenceVoltage) : 
        fControllers(ctl)
    {
        fPinIn = pin;
        // pinMode(fPinIn, INPUT);
        fMinValue = 1/referenceVoltage;
        fMaxValue = 2/referenceVoltage;
    }

    virtual void setup() override
    {
    }

    double normalize(double value) {
        return (value - fMinValue) / (fMaxValue - fMinValue)
    }

    double round(double value, int decimal_places) {
        const double multiplier = pow(10.0, decimal_places);
        return round(value * multiplier) / multiplier;
    }

    double raw_angle(int decimal_places = 2) {
        double raw = analogRead(fPinIn);
        double angle = max(0.0, min(1.0, DomePotentiometer::normalize(raw))) * 360.0;
        return angle;
    }

    void update() {
        double currentAngle = DomePotentiometer::raw_angle();
        // TODO: add angle to small list, and get average from that list to return
    }

    double position() {
        // TODO: get average from angle list to return

        double val = DomePotentiometer::round(avgAngle, decimal_places);
    }
private:

protected:
    int fPinIn;
    double fMinValue;
    double fMaxValue;
}

#endif
