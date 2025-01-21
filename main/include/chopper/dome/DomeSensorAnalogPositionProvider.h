#pragma once
//#include "ReelTwo.h"
#include <Bluepad32.h>
#include "chopper/dome/DomePositionProvider.h"
#include "chopper/sensor/AnalogMonitor.h"

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
        return map(val, 1225, 2500, 0, 359);
        // val = min(max(val, fParams.domespmin), fParams.domespmax);
        // int pos = map(val, fParams.domespmin, fParams.domespmax, 0, 359);
        // return 0;
    }

protected:
    AnalogMonitor fAnalogMonitor;
};
