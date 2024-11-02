#ifndef DomeSensorAnalogPositionProvider_h
#define DomeSensorAnalogPositionProvider_h

//#include "ReelTwo.h"
#include "drive/DomePositionProvider.h"
#include "core/AnalogMonitor.h"

class DomeSensorAnalogPositionProvider : public DomePositionProvider
{
public:
    DomeSensorAnalogPositionProvider(uint8_t analogPin) : 
        fAnalogMonitor(analogPin)
    {
    }

    virtual bool ready() override
    {
        return true;
    }

    virtual int getAngle() override
    {
        fAnalogMonitor.animate();
        unsigned val = fAnalogMonitor.getValue();
        // Serial.print("val : "); Serial.print(val); Serial.print(" : ");
        return map(val, 0, 1024, 0, 359);
        // val = min(max(val, fParams.domespmin), fParams.domespmax);
        // int pos = map(val, fParams.domespmin, fParams.domespmax, 0, 359);
        // return 0;
    }

protected:
    AnalogMonitor fAnalogMonitor;
};

#endif
