# DomeControlFirmware — Random Movement & Controller Override Analysis

Source: https://github.com/reeltwo/DomeControlFirmware

## Architecture Overview

The system has two key classes:
- **`DomePosition`** (in ReelTwo library `drive/DomePosition.h`) — State machine holding the current mode, target positions, speeds, and delays
- **`DomeDrive`** (in ReelTwo library `drive/DomeDrive.h`) — The animation loop that reads joystick input, manages auto-dome movement, and drives the motor

## Dome Modes (State Machine)

```cpp
enum Mode { kOff, kHome, kRandom, kTarget };
```

- **`kOff`** — No automatic movement
- **`kHome`** — Drive to home position, then revert to default mode
- **`kRandom`** — Autonomous random movement
- **`kTarget`** — Drive to a specific commanded position, then revert to default mode

There's a **`fDomeMode`** (current active mode) and **`fDomeDefaultMode`** (what to revert to after completing a target move). `setDomeDefaultMode()` also sets the current mode.

## Random Movement Logic (`DomeDrive::domeStick`, kRandom case)

The random dome algorithm in `DomeDrive.h` works as follows:

1. **Entry**: When mode first becomes `kRandom` (`newMode == true`):
   - Pick a random delay between `minDelay` and `maxDelay` (default 6-8 seconds)
   - Set `fAutoDomeTargetPos = -1` (no target yet)
   - Randomly pick initial direction (`fAutoDomeLeft`)

2. **Target selection** (when `fAutoDomeTargetPos == -1` and delay has elapsed):
   - **10% chance: go home** — if `fAutoDomeGoHome` is set, target = home position
   - **10% chance: do nothing** — pick a new random delay and wait again
   - **Otherwise: random turn** — pick a random distance (0 to `fDomeAutoLeft`/`fDomeAutoRight` degrees, default 80 deg), clamped to min 5 deg, relative to home position
   - After each move, there's a **10% chance** to flag "go home next" and a **10% chance** to switch direction

3. **Movement**: Uses `moveDomeToTarget()` which applies deceleration scaling as the dome approaches the target (within a "fudge" window of +/-5 deg)

4. **Arrival**: When target is reached:
   - Pick a new random delay (6-8s default)
   - Reset `fAutoDomeTargetPos = -1` to trigger new target selection after delay

## Controller Override — The Key Part

The priority system is in `DomeDrive::domeStick()`:

```cpp
if (m != 0)            // Joystick has input
{
    fDrive = 0;        // Cancel programmatic drive
    fAutoDrive = 0;    // Cancel auto drive
}
else if (fDrive != 0)  // Programmatic drive (serial commands)
{
    m = fDrive;
    fAutoDrive = 0;
}
else
{
    m = fAutoDrive;    // Lowest priority: auto drive
}
```

**Priority order**: Joystick > Programmatic (`fDrive`) > Auto (`fAutoDrive`)

Any joystick movement immediately zeroes both `fDrive` and `fAutoDrive`.

## Return-to-Random Timer

When the joystick produces movement (`abs(m) != 0`):

```cpp
if (abs(m) != 0.0 || fAutoDrive != 0)
{
    if (fIdle)
    {
        fDomePosition->reachedTarget();
        fDomePosition->resetDefaultMode();   // Revert to default mode (kRandom if configured)
        fLastDomeMovement = currentMillis;    // Reset the idle timer
        fDomeMovementStarted = false;
    }
    fIdle = false;
}
```

When the joystick is released (`m == 0`):

```cpp
if (domeMode != DomePosition::kOff && abs(m) == 0.0)
{
    uint32_t minDelay = fDomePosition->getDomeMinDelay() * 1000L;
    if (fLastDomeMovement + minDelay < currentMillis)
    {
        fIdle = true;
        // Auto-dome logic takes over...
    }
}
else
{
    fLastDomeMovement = currentMillis;  // Keep resetting while stick is active
}
```

**The flow is:**
1. User moves joystick -> `fIdle = false`, `fLastDomeMovement` keeps updating
2. User releases joystick -> `m == 0`, `fLastDomeMovement` stops updating
3. After `minDelay` seconds of idle (default **6 seconds** for kRandom), `fIdle = true`
4. Random movement resumes from a fresh random delay

## Safety Gate (`sDomeHasMovedManually`)

There's an **auto-safety** feature (default enabled):
```cpp
#define DEFAULT_AUTO_SAFETY true
```
When `fAutoSafety` is true, no automatic movement happens until the dome has been moved manually at least once (joystick or serial command). This prevents random movement at power-on before the operator confirms the dome is clear.

## Configurable Parameters

| Parameter | Default | Description |
|---|---|---|
| `fDomeAutoMinDelay` | 6s | Min pause between random moves |
| `fDomeAutoMaxDelay` | 8s | Max pause between random moves |
| `fDomeAutoLeft` | 80 deg | Max random left rotation |
| `fDomeAutoRight` | 80 deg | Max random right rotation |
| `fDomeSpeedAuto` | 30% | Speed during random moves |
| `fDomeSpeedHome` | 40% | Speed when returning home |
| `fDomeFudge` | 5 deg | Position tolerance (dead zone) |
| `fTimeout` | 5s | Watchdog — if dome doesn't move for this long during auto, switch to kOff (error) |
| `fDomeSpeedMin` | 15% | Below this speed, motor output = 0 (dead zone) |

## Summary for Chopper Implementation

The key patterns to replicate:

1. **Three-priority input**: joystick > serial command > auto-movement
2. **Idle timer**: Track `last_dome_movement` timestamp. After `min_delay` seconds of no input, transition to idle and let auto-dome take over
3. **Random target selection**: Pick random angles relative to home within configurable left/right bounds, with probabilistic "go home" and "do nothing" behaviors
4. **Move-to-target with deceleration**: Slow down as the dome approaches target (deceleration scale)
5. **Watchdog timeout**: If the dome position sensor shows no movement during auto mode for N seconds, error out to kOff
6. **Auto-safety**: Require at least one manual movement before enabling random mode
