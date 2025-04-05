#ifndef CHOPPER_MP3TRIGGER_H
#define CHOPPER_MP3TRIGGER_H

#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>

#include <MP3Trigger.h>
#include "SettingsSystem.h"
#include "SettingsUser.h"

class ExtendedMP3Trigger : public MP3Trigger {
public:
    ExtendedMP3Trigger() : MP3Trigger() {
        // Seed the random number generator
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
    }

    // Method to get a random sound define
    uint8_t getRandomSound() const {
        if (soundDefines.empty()) {
            return 0; // Default sound if the list is empty
        }
        int randomIndex = std::rand() % soundDefines.size();
        return soundDefines[randomIndex];
    }

    // Override the trigger method to add logging
    void trigger(byte track) {
        if (DEBUG_SOUND) {
            DEBUG_PRINTF("Triggering sound: %d\n", track);
        }

        // Call the parent class's trigger method
        MP3Trigger::trigger(track);
    }

    // Method to trigger a random sound
    void triggerRandom() {
        uint8_t randomSound = getRandomSound();
        trigger(randomSound);
    }


private:
    // List of sound defines from SettingsUser.h
    const std::vector<uint8_t> soundDefines = {
        C110P_SOUND_GRUMBLY01,
        C110P_SOUND_OKAYOKAY,
        C110P_SOUND_OKAYFOLLOWME,
        C110P_SOUND_GRUMBLY02,
        C110P_SOUND_YESIWOULD,
        C110P_SOUND_GRUMPY03,
        C110P_SOUND_NOW,
        C110P_SOUND_WHATGROAN,
        C110P_SOUND_3WAH,
        C110P_SOUND_TADA,
        C110P_SOUND_CHATTY,
        C110P_SOUND_EXTENDEDGRUMBLE,
        C110P_SOUND_GRUMBLY1,
        C110P_SOUND_UHOH,
        C110P_SOUND_SWRSTINGER,
        C110P_SOUND_PURR3
    };
};

#endif // CHOPPER_MP3TRIGGER_H