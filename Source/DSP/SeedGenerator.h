#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

class SeedGenerator
{
public:
    SeedGenerator();
    ~SeedGenerator() = default;

    void generateNewSeed();

    enum class OscMode
    {
        Saw,
        Pulse,
        Morph,
        Sine
    };

    struct PatchParameters {
        // Oscillator
        float oscMorph;         // used only when an oscillator is in Morph mode
        float detuneAmount;     // semitone fraction
        float subOscLevel;      // sub oscillator level
        OscMode oscMode1;
        OscMode oscMode2;
        int octaveShift;        // -1 (MONE), 0 (NORM), +1 (PONE)
        
        // Envelope
        float envAttack;
        float envDecay;
        float envSustain;
        float envRelease;
        
        // Filter
        float filterCutoff;
        float filterReso;
        float filterEnvAmount;
        float filterEnvAttack;
        float filterEnvDecay;
        float filterEnvSustain;
        float filterEnvRelease;
        
        // Character
        float driftAmount;
        float stereoWidth;      // 0=mono, 1=full stereo
        bool isMonophonic;      // true=mono with legato, false=poly
        bool envLoop;           // true=looping volume/filter envelopes
        float glideRate;        // glide coefficient
        
        // FX
        float reverbSize;
        float reverbMix;
        float chorusDepth;
        float delayFeedback;
        float decimatorMix;
        bool driftAlive;

        // Autopan and delay tone
        float autopanAmount;
        int unisonMode = 1; // 0 = 2VU, 1 = 5VU
        bool autopanRate16th;
        bool autopanShapeSaw;
        float delayTone;
        float delayMix;
    };

    PatchParameters generatePatch();

    /** Message thread only - returns a reference into the shared juce::String. */
    const juce::String& getSeedCode() const { return currentSeedCode; }

    /** Safe from any thread. */
    juce::uint32 getSeedHash() const { return currentSeedHash.load(std::memory_order_relaxed); }

    void setSeedFromFloat(float seedValue)
    {
        juce::uint32 seedInt = (juce::uint32)(seedValue * 123456.0f);
        setSeedCode(formatSeedCode(seedInt));
    }

private:
    // generatePatch() runs on the AUDIO thread while the editor can be changing
    // the seed on the MESSAGE thread. juce::String is copy-on-write, so reading
    // currentSeedCode from the audio thread while it is reassigned is a data
    // race that can free the string data out from under the reader. The hash the
    // audio thread actually needs is therefore mirrored into an atomic, and
    // generatePatch() never touches the string (nor any shared juce::Random).
    juce::String currentSeedCode;
    std::atomic<juce::uint32> currentSeedHash { 0 };

    void setSeedCode(juce::String newCode)
    {
        currentSeedHash.store((juce::uint32) newCode.hashCode(), std::memory_order_relaxed);
        currentSeedCode = std::move(newCode);
    }

    juce::String formatSeedCode(juce::uint32 seedValue);
};
