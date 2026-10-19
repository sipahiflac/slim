#pragma once

#include "Params.h"
#include "EngineConstants.h"
#include <vector>
#include <cstdint>
#include <cmath>

struct Instrument;

class Voice
{
public:
    Voice();

    void setSampleRate(float rate); // Always kEngineSampleRateF

    void noteOn(int midiNote, float velocity, const Instrument* instrument, const VoiceParams& params);
    void refreshPitch();
    void noteOff(int midiNote);
    void fadeOut(float seconds);
    void stop();
    void applyParamsLive(const VoiceParams& params);

    // Render one sample at kEngineSampleRateF. Returns mono sample in [-1.0, 1.0].
    float renderSample(bool movementEnabled = true);

    // Pitch wheel position in semitones (already scaled by the bend range); glides to the new value.
    void setPitchBend(float semitones) noexcept { bendTarget = semitones; }

    bool isActive() const { return active; }
    bool isMenuPreview() const { return menuPreview; }
    void setMenuPreview(bool shouldBePreview) { menuPreview = shouldBePreview; }
    int getMidiNote() const { return currentNote; }
    uint64_t getAge() const { return age; }
    float getAudibility() const { return audibility; }
    bool usesInstrument(const Instrument* instrument) const { return currentInstrument == instrument; }

    // Movement parameters (internal tuning)
    float pitchDriftDepthCents = 7.0f;     // ~±7 cents default
    float brightnessDriftRatio = 0.15f;    // ~±15% filter cutoff ratio

private:
    // Allocation-free topology-preserving low-pass stage. Its integrators stay
    // stable while the cutoff changes, unlike interpolating direct-form poles.
    struct LowpassStage
    {
        float s1 = 0.0f, s2 = 0.0f;
        float process(float input, float g, float inverseQ) noexcept
        {
            const float high = (input - s1 * (g + inverseQ) - s2)
                             / (1.0f + inverseQ * g + g * g);
            const float band = high * g + s1;
            s1 = high * g + band;
            const float low = band * g + s2;
            s2 = band * g + low;
            return low;
        }
        void reset() noexcept { s1 = s2 = 0.0f; }
    };
    struct Resonator
    {
        float b0 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;
        void configure(float frequency, float q) noexcept;
        float process(float input) noexcept
        {
            const float output = b0 * input + z1;
            z1 = -a1 * output + z2;
            z2 = b2 * input - a2 * output;
            return output;
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
    };
    enum class EnvStage { OFF, ATTACK, SUSTAIN_DECAY, RELEASE };

    int endFadeRemaining = 0;
    float endFadeOutput = 0.0f;
    float renderEndFade();
    bool active = false;
    bool menuPreview = false;
    int currentNote = -1;
    uint64_t age = 0; // Incremented every sample to identify the oldest voice

    const Instrument* currentInstrument = nullptr;
    VoiceParams currentParams;

    // Playback state
    double tickPos = 0.0;
    double bodyPos = 0.0;
    double pitchRatio = 1.0;
    float bendTarget = 0.0f, bendSmoothed = 0.0f;
    double bendRatio = 1.0;

    // Movement Layer State (per-voice independent, non-periodic smooth noise)
    uint32_t prngState = 123456789;
    inline float nextRandomSigned() noexcept
    {
        prngState = prngState * 1664525u + 1013904223u;
        return ((float)(prngState >> 16) / 32768.0f) - 1.0f; // [-1.0, 1.0]
    }

    // Dual-rate filtered noise for pitch drift (~0.8 Hz & ~2.2 Hz)
    float pitchNoiseState1 = 0.0f;
    float pitchNoiseState2 = 0.0f;
    // Dual-rate filtered noise for brightness drift (~0.6 Hz & ~1.8 Hz)
    float brightNoiseState1 = 0.0f;
    float brightNoiseState2 = 0.0f;

    // Envelope state
    EnvStage envStage = EnvStage::OFF;
    float envLevel = 0.0f;
    float attackCoefficient = 0.01f;
    float decayFactor = 0.9999f;
    float releaseFactor = 0.92f; // Fast, click-free release when key released
    bool releaseFull = false;
    // After release no new loop seam starts, so the note plays on into the
    // recording's own tail; a seam already under way still completes.
    bool loopReleased = false;
    bool loopCrossfading = false;
    // Per-sample factor reproducing the instrument's own measured die-away
    // while the key is held. 1.0 = hold flat (a blown/bowed sound).

    // Four-pole Butterworth brightness filter, logarithmic Hz smoothing.
    LowpassStage brightnessLow, brightnessHigh;
    float targetCutoffLog = 0.0f, smoothedCutoffLog = 0.0f, cutoffG = 1.0f;
    float targetFilterMix = 1.0f, smoothedFilterMix = 1.0f;

    // Levels and smoothing
    float targetVolume = 0.0f;
    float smoothedVolume = 0.0f;
    float noiseGain = 0.0f;
    float qualityStep = 0.0f;
    int qualityHoldSamples = 1;
    int qualityCounter = 0;
    float heldQualitySample = 0.0f;
    float lastOutput = 0.0f;
    float audibility = 0.0f;
    double detuneRatio = 1.0;
    float formantMix = 0.0f;
    Resonator formantLow, formantHigh;

    void updateParams(const VoiceParams& p);
};
