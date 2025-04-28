#pragma once

// align identifier with playerLED on controller for easy identifcation of role
enum class ControllerRoles
{
    Drive = 1,      // 0001
    Dome = 3,       // 0011
    Animation = 7,  // 0111
    Camera = 15,    // 1111
};
