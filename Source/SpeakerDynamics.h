#pragma once
#include <algorithm>
#include <cmath>

// Dimensionless reduced-order transducer, NOT measured Nokia parameters.
// See SPEAKER_MODEL.md for equations, sources, assumptions and validation.
class SpeakerDynamics
{
public:
    void reset() noexcept { x = v = heat = previousInput = 0.0f; }
    float process(float input, bool nonlinear = true) noexcept
    {
        // 2 substeps at 32 kHz; linear interpolation keeps excitation continuous.
        constexpr float h = 6.28318530718f * 900.0f / 32000.0f;
        constexpr float heating = 1.0f - 0.9999107183f; // 0.35 s at 32 kHz
        constexpr float cooling = 1.0f - 0.9999843751f; // 2 s at 32 kHz
        float result = 0.0f;
        for (int step = 0; step < 2; ++step)
        {
            const float u = step == 0 ? 0.5f * (previousInput + input) : input;
            const float power = std::min(u * u, 16.0f);
            heat += (power > heat ? heating : cooling) * (power - heat);
            const float resistance = nonlinear ? 1.0f + 0.7f * heat : 1.0f;
            const float forceFactor = nonlinear ? 1.0f / (1.0f + 0.18f*x*x) : 1.0f;
            const float stiffness = nonlinear ? 1.0f + 0.12f*x*x : 1.0f;
            const float damping = 0.65f + 0.45f*forceFactor*forceFactor/resistance
                                + (nonlinear ? 0.16f*v*v : 0.0f);
            const float force = u * forceFactor / resistance;
            const float oldV = v;
            // Semi-implicit position; positive damping solved implicitly.
            v = (v + h * (force - stiffness*x)) / (1.0f + h*damping);
            x += h*v;
            // Acceleration is the pressure proxy. Partial coupling retains a
            // useful low-mid range instead of pretending to fit a real enclosure.
            const float acceleration = (v-oldV)/h;
            result += 0.5f * (0.78f*u/resistance + 0.22f*acceleration);
        }
        previousInput = input;
        if (std::abs(x)<1.0e-15f) x=0;
        if (std::abs(v)<1.0e-15f) v=0;
        if (heat<1.0e-15f) heat=0;
        return result;
    }
    float getHeat() const noexcept { return heat; }
private:
    float x=0, v=0, heat=0, previousInput=0;
};
