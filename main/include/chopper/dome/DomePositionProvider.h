#pragma once

class DomePositionProvider
{
public:
    virtual bool ready() = 0;
    virtual int getAngle() = 0;
};
