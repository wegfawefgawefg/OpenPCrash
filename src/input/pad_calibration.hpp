#pragma once
// The pads' calibration (PC CONFIG's STICK CALIBRATION, docs/input.md "Calibration"): each stick's
// rest point and reach per axis, each trigger's rest and full pull, measured on the calibration
// panel and kept per kind of pad (its SDL GUID) in the install's gubsy settings. The reads rescale
// a pad's raw axes by it before the dead zones, so a stick that rests off centre or falls short of
// the edge reads as a clean full circle. A pad with none stored reads as it comes. Presentation
// and input only: it changes what a player sends, never how the world steps.
#include <SDL3/SDL.h>
#include <map>
#include <string>
#include <vector>

class GubsyRuntime;

// one axis: where it rests and how far it reaches either way, SDL's raw units
struct AxisCalibration {
    int centre = 0;
    int low = -32768, high = 32767;
};

struct PadCalibration {
    AxisCalibration stick[2][2]; // [left, right][x, y], SDL's sense (y down)
    int trigger_rest[2] = {0, 0};
    int trigger_full[2] = {32767, 32767};
};

struct PadCalibrations {
    // by key: "<GUID>", "<GUID>.serial.<serial>" or "<GUID>.path.<path>"
    std::map<std::string, PadCalibration> by_key;
    // each pad's keys, its own first and its kind's last (SDL never reuses a pad's id in a run)
    mutable std::map<SDL_JoystickID, std::vector<std::string>> keys_of;
};

// the install's settings key prefix: phantomcrash.input.calibration.<key>
inline constexpr const char* calibration_key_prefix = "phantomcrash.input.calibration.";

// every stored calibration from gubsy's install settings (once gubsy has started)
void pad_calibrations_load(PadCalibrations& out, GubsyRuntime& runtime);
// one pad's calibration stored under its own key (and its kind's when it has no serial), or,
// null, taken out from all of them, and the install's settings written
void pad_calibration_store(PadCalibrations& all, GubsyRuntime& runtime, SDL_JoystickID pad,
                           const PadCalibration* calibration);
// the pad's calibration (its own, else its kind's), null when none is stored
const PadCalibration* pad_calibration_of(const PadCalibrations& all, SDL_JoystickID id);

// a raw stick axis rescaled: its rest to 0, its reach to the full +-32767
int calibrated_axis(const AxisCalibration& axis, int raw);
// a raw trigger 0..32767 rescaled: its rest to 0, its full pull to 32767
int calibrated_trigger(const PadCalibration& calibration, int trigger, int raw);
