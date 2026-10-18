#pragma once

#include <algorithm>

struct VoiceParams
{
    // Controls are 1..10. Floating-point values let mouse drags make fine
    // adjustments between the ten visible steps.
    float brightness = 10.0f; // Open: neutral brightness-filter bypass.
    float attack     = 2.0f;
    float release    = 2.0f;
    float detune     = 5.0f;
    float noise      = 1.0f;
    float quality    = 10.0f;
    float formant    = 5.0f;

    void clampAll()
    {
        brightness = std::clamp(brightness, 1.0f, 10.0f);
        attack     = std::clamp(attack,     1.0f, 10.0f);
        release    = std::clamp(release,    1.0f, 10.0f);
        detune     = std::clamp(detune,     1.0f, 10.0f);
        noise      = std::clamp(noise,      1.0f, 10.0f);
        quality    = std::clamp(quality,    1.0f, 10.0f);
        formant    = std::clamp(formant,    1.0f, 10.0f);
    }
};
