#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SeedGenerator.h"

class ReplicantDrift
{
public:
    ReplicantDrift() = default;
    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate;
        prng.setSeedRandomly();
        smoothPitch.reset(sampleRate, 0.8);
        smoothCutoff.reset(sampleRate, 0.8);
    }
    
    void process(float amount)
    {
        if (amount < 0.01f) return;
        
        counter++;
        if (counter > 800)
        {
            counter = 0;
            float targetPitchDrift = (prng.nextFloat() * 2.0f - 1.0f) * 0.003f * amount;
            float targetCutoffDrift = (prng.nextFloat() * 2.0f - 1.0f) * 400.0f * amount;
            
            smoothPitch.setTargetValue(targetPitchDrift);
            smoothCutoff.setTargetValue(targetCutoffDrift);
        }
    }
    
    float getPitchDrift() { return smoothPitch.getNextValue(); }
    float getCutoffDrift() { return smoothCutoff.getNextValue(); }

private:
    juce::Random prng;
    juce::SmoothedValue<float> smoothPitch;
    juce::SmoothedValue<float> smoothCutoff;
    int counter = 0;
    double currentSampleRate = 44100.0;
};

struct WavetableData
{
    static constexpr int NUM_MIPS = 8;
    static constexpr int TABLE_SIZE = 512;
    float mips[NUM_MIPS][TABLE_SIZE] = {};
};

struct WavetableBank;

class DPWOscillator
{
public:
    static constexpr int NUM_MIPS = 8;
    static constexpr int TABLE_SIZE = 512;

    DPWOscillator() = default;
    
    void prepare(double sampleRate)
    {
        currentSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
        reset();
    }

    void setWavetable(const WavetableData* table)
    {
        wavetable = table;
    }

    void reset()
    {
        phase = 0.0f;
        lastValue = 1.0f;
        lastValueSquare = 1.0f;
    }

    void setPhase(float newPhase)
    {
        phase = std::fmod(newPhase, juce::MathConstants<float>::twoPi);
        if (phase < 0.0f) phase += juce::MathConstants<float>::twoPi;
        float rawSaw = (phase / juce::MathConstants<float>::pi) - 1.0f;
        lastValue = rawSaw * rawSaw;
        float rawSquare = rawSaw >= 0.0f ? 1.0f : -1.0f;
        lastValueSquare = rawSquare * rawSaw;
    }
    
    void setFrequency(float freq)
    {
        frequency = freq;
        double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;
        phaseIncrement = (freq / (float)sr) * 2.0f * juce::MathConstants<float>::pi;
        if (std::isnan(phaseIncrement) || std::isinf(phaseIncrement))
            phaseIncrement = 0.0f;
    }
    
    float getSaw(float currentPhase)
    {
        float rawSaw = (currentPhase / juce::MathConstants<float>::pi) - 1.0f;
        float sq = rawSaw * rawSaw;
        float diff = sq - lastValue;
        lastValue = sq;
        
        float scale = 4.0f * (phaseIncrement / (2.0f * juce::MathConstants<float>::pi));
        return (scale > 0.0001f) ? (diff / scale) : rawSaw;
    }

    static inline float polyBLEP(float t, float dt)
    {
        if (t < dt)
        {
            float r = t / dt;
            return r + r - r * r - 1.0f;
        }
        else if (t > 1.0f - dt)
        {
            float r = (t - 1.0f) / dt;
            return r * r + r + r + 1.0f;
        }
        return 0.0f;
    }

    static inline float polyBLAMP(float t, float dt)
    {
        if (t < dt)
        {
            float r = t / dt;
            return (dt / 3.0f) * (1.0f - r) * (1.0f - r) * (1.0f - r);
        }
        else if (t > 1.0f - dt)
        {
            float r = (1.0f - t) / dt;
            return (dt / 3.0f) * (1.0f - r) * (1.0f - r) * (1.0f - r);
        }
        return 0.0f;
    }

    float processSaw()
    {
        phase += phaseIncrement;
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
        return getSaw(phase);
    }

    float processPulse(float duty = 0.5f)
    {
        phase += phaseIncrement;
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        float t = phase / juce::MathConstants<float>::twoPi;
        double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;
        float dt = std::max(0.00001f, frequency / (float)sr);
        float d = juce::jlimit(0.05f, 0.95f, duty);

        float val = (t < d) ? 1.0f : -1.0f;
        val += polyBLEP(t, dt);
        val -= polyBLEP(std::fmod(t - d + 1.0f, 1.0f), dt);
        return val;
    }

    float processRandom()
    {
        if (wavetable == nullptr)
            return 0.0f;

        phase += phaseIncrement;
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;
        float maxHarmonics = (float)(sr / (2.0 * std::max(20.0f, frequency)));
        float mipFloat = juce::jlimit(0.0f, (float)(NUM_MIPS - 1), (float) std::log2(std::max(1.0f, 256.0f / maxHarmonics)));
        int mip0 = (int) mipFloat;
        int mip1 = std::min(NUM_MIPS - 1, mip0 + 1);
        float mipFrac = mipFloat - (float) mip0;

        float tablePos = (phase / juce::MathConstants<float>::twoPi) * (float)TABLE_SIZE;

        auto sampleHermite = [](const float* table, float pos) -> float
        {
            int idx1 = (int) pos;
            float frac = pos - (float) idx1;
            int idx0 = (idx1 - 1 + TABLE_SIZE) % TABLE_SIZE;
            int idx2 = (idx1 + 1) % TABLE_SIZE;
            int idx3 = (idx1 + 2) % TABLE_SIZE;

            float y0 = table[idx0];
            float y1 = table[idx1];
            float y2 = table[idx2];
            float y3 = table[idx3];

            float c0 = y1;
            float c1 = 0.5f * (y2 - y0);
            float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        };

        float val0 = sampleHermite(wavetable->mips[mip0], tablePos);
        float val1 = sampleHermite(wavetable->mips[mip1], tablePos);
        return val0 + (val1 - val0) * mipFrac;
    }

    float processSine()
    {
        phase += phaseIncrement;
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
        return std::sin(phase);
    }

    float processTriangle()
    {
        phase += phaseIncrement;
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        float t = phase / juce::MathConstants<float>::twoPi;
        double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;
        float dt = std::max(0.00001f, frequency / (float)sr);

        float val = 2.0f * std::abs(2.0f * (t - std::floor(t + 0.5f))) - 1.0f;
        float t2 = std::fmod(t + 0.5f, 1.0f);
        val += (4.0f / dt) * (polyBLAMP(t, dt) - polyBLAMP(t2, dt));
        return juce::jlimit(-1.0f, 1.0f, val);
    }

    static void generateWavetable(float seedValue, WavetableData& outTable)
    {
        juce::uint32 seedInt = (juce::uint32)(seedValue * 987654.0f) + 13579U;
        juce::Random r((juce::int64)seedInt);

        int mode = r.nextInt(6);
        const int maxH = 256;
        float amps[maxH + 1];
        float phases[maxH + 1];
        std::fill_n(amps, maxH + 1, 0.0f);
        std::fill_n(phases, maxH + 1, 0.0f);

        if (mode == 0)
        {
            float pulseWidth = 0.15f + r.nextFloat() * 0.70f;
            for (int h = 1; h <= maxH; ++h)
            {
                amps[h] = (4.0f / (juce::MathConstants<float>::pi * (float)h)) * std::sin(juce::MathConstants<float>::pi * (float)h * pulseWidth);
                phases[h] = 0.0f;
            }
        }
        else if (mode == 1)
        {
            float centerHarmonic = 3.0f + r.nextFloat() * 12.0f;
            float qWidth = 1.5f + r.nextFloat() * 4.0f;
            for (int h = 1; h <= 64; ++h)
            {
                float dist = std::abs((float)h - centerHarmonic);
                amps[h] = std::exp(-dist * dist / (2.0f * qWidth * qWidth));
                phases[h] = 0.0f;
            }
        }
        else if (mode == 2)
        {
            for (int h = 1; h <= 24; ++h)
            {
                amps[h] = 1.0f / (float)h;
                phases[h] = (r.nextFloat() - 0.5f) * 0.4f * juce::MathConstants<float>::twoPi;
            }
        }
        else if (mode == 3)
        {
            static const int ratios[9] = { 1, 2, 3, 4, 5, 6, 8, 10, 12 };
            for (int idx = 0; idx < 9; ++idx)
            {
                int h = ratios[idx];
                amps[h] = r.nextFloat();
                phases[h] = 0.0f;
            }
        }
        else if (mode == 4)
        {
            for (int h = 1; h <= 64; ++h)
            {
                amps[h] = 1.0f / std::pow((float)h, 0.7f);
                phases[h] = (h % 2 == 0) ? 0.0f : juce::MathConstants<float>::pi * 0.5f;
            }
        }
        else
        {
            for (int h = 1; h <= 64; ++h)
            {
                amps[h] = r.nextFloat() / std::sqrt((float)h);
                phases[h] = r.nextFloat() * juce::MathConstants<float>::twoPi;
            }
        }

        // Additive build of the mip pyramid.
        float partialSum[TABLE_SIZE];
        std::fill_n (partialSum, TABLE_SIZE, 0.0f);
        int harmonicsAlreadySummed = 0;

        for (int m = NUM_MIPS - 1; m >= 0; --m)
        {
            const int maxHarmonicsForMip = std::max(2, maxH >> m);

            for (int h = harmonicsAlreadySummed + 1; h <= maxHarmonicsForMip; ++h)
            {
                const float amp = amps[h];
                if (amp == 0.0f)
                    continue;

                const double theta   = juce::MathConstants<double>::twoPi * (double) h / (double) TABLE_SIZE;
                const double cosStep = std::cos (theta);
                const double sinStep = std::sin (theta);

                double c = std::cos ((double) phases[h]);
                double s = std::sin ((double) phases[h]);

                for (int i = 0; i < TABLE_SIZE; ++i)
                {
                    partialSum[i] += amp * (float) s;

                    const double nextC = c * cosStep - s * sinStep;
                    const double nextS = s * cosStep + c * sinStep;
                    c = nextC;
                    s = nextS;
                }
            }

            harmonicsAlreadySummed = std::max (harmonicsAlreadySummed, maxHarmonicsForMip);
            std::copy_n (partialSum, TABLE_SIZE, outTable.mips[m]);

            float sumSq = 0.0f;
            for (int i = 0; i < TABLE_SIZE; ++i)
                sumSq += outTable.mips[m][i] * outTable.mips[m][i];
            float rms = std::sqrt(sumSq / (float)TABLE_SIZE);
            if (rms > 0.0001f)
            {
                float targetRMS = 0.32f;
                float scale = targetRMS / rms;
                for (int i = 0; i < TABLE_SIZE; ++i)
                    outTable.mips[m][i] *= scale;
            }

            float peak = 0.0f;
            for (int i = 0; i < TABLE_SIZE; ++i)
                peak = std::max(peak, std::abs(outTable.mips[m][i]));
            if (peak > 0.95f)
            {
                float peakScale = 0.95f / peak;
                for (int i = 0; i < TABLE_SIZE; ++i)
                    outTable.mips[m][i] *= peakScale;
            }
        }
    }
    
private:
    double currentSampleRate = 44100.0;
    float phase = 0.0f;
    float phaseIncrement = 0.0f;
    float lastValue = 0.0f;
    float lastValueSquare = 0.0f;
    float frequency = 440.0f;

    const WavetableData* wavetable = nullptr;
};

struct WavetableBank
{
    static constexpr int NUM_TABLES = 5;
    WavetableData tables[NUM_TABLES];
    float seedValue = -999999.0f;

    void build(float seed)
    {
        seedValue = seed;
        DPWOscillator::generateWavetable(seed, tables[0]);
        DPWOscillator::generateWavetable(seed + 0.123f, tables[1]);
        DPWOscillator::generateWavetable(seed + 0.456f, tables[2]);
        DPWOscillator::generateWavetable(seed + 0.789f, tables[3]);
        DPWOscillator::generateWavetable(seed + 0.987f, tables[4]);
    }
};

class LoopingEnvelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, WaitingForLoop, Release };
    
    void setSampleRate(double sr)
    {
        sampleRate = sr;
    }
    
    void setParameters(float a, float d, float s, float r, bool loop)
    {
        attackTime = a;
        decayTime = d;
        sustainLevel = s;
        releaseTime = r;
        isLooping = loop;
    }

    void setLoopControls(float durationMs)
    {
        loopDurationSeconds = juce::jlimit(0.03f, 60.0f, durationMs * 0.001f);
    }

    std::atomic<float>* loopPulseTarget = nullptr;
    void setLoopPulseTarget(std::atomic<float>* target) { loopPulseTarget = target; }
    void setGridSyncEnabled(bool shouldSync) { gridSyncEnabled = shouldSync; }
    
    void noteOn()
    {
        gateHeld = true;
        stage = Stage::Attack;
        forcedFastRelease = false; // Reset on note-on
        // Start attack from current level to prevent instant step clicks
        if (currentLevel < 0.001f)
            currentLevel = 0.0f;
        if (loopPulseTarget)
            loopPulseTarget->store(0.85f);
    }
    
    void noteOff()
    {
        gateHeld = false;
        if (stage == Stage::Idle)
            return;
        stage = Stage::Release;
        releaseStartLevel = currentLevel;
        currentLevel = releaseStartLevel;
    }

    void forceFastStop()
    {
        if (stage == Stage::Idle)
            return;
        gateHeld = false;
        stage = Stage::Release;
        releaseStartLevel = currentLevel;
        forcedFastRelease = true; // Force 5ms release time
    }
    
    float getNextSample(bool gridBoundary = false)
    {
        if (stage == Stage::Idle) return 0.0f;

        if (gridBoundary && isLooping && gridSyncEnabled && gateHeld)
        {
            // Keep the current level at the first lock point to avoid an amplitude step.
            // Following cycles naturally reach zero before their grid boundary.
            if (currentLevel < 0.001f)
                currentLevel = 0.0f;
            stage = Stage::Attack;
            if (loopPulseTarget)
                loopPulseTarget->store(0.75f);
        }
        
        switch (stage)
        {
            case Stage::Attack:
            {
                const float activeAttackTime = isLooping
                    ? std::min(attackTime, std::max(0.001f, loopDurationSeconds - 0.001f))
                    : attackTime;
                float attackRate = 1.0f / (sampleRate * std::max(0.001f, activeAttackTime));
                currentLevel += attackRate;
                if (currentLevel >= 1.0f)
                {
                    currentLevel = 1.0f;
                    stage = Stage::Decay;
                }
                break;
            }
            case Stage::Decay:
            {
                const float remainingLoopTime = std::max(0.001f, loopDurationSeconds - std::max(0.001f, attackTime));
                float activeDecayTime = isLooping ? remainingLoopTime : decayTime;
                float decayRate = 1.0f / (sampleRate * std::max(0.001f, activeDecayTime));
                currentLevel -= isLooping ? decayRate : decayRate * (1.0f - sustainLevel);
                
                float target = isLooping ? 0.0f : sustainLevel;
                if (currentLevel <= target)
                {
                    currentLevel = target;
                    if (isLooping && gateHeld)
                    {
                        stage = gridSyncEnabled ? Stage::WaitingForLoop : Stage::Attack;
                        if (!gridSyncEnabled && loopPulseTarget)
                            loopPulseTarget->store(0.75f);
                    }
                    else
                    {
                        stage = Stage::Sustain;
                    }
                }
                break;
            }
            case Stage::Sustain:
            {
                currentLevel = sustainLevel;
                break;
            }
            case Stage::WaitingForLoop:
            {
                currentLevel = 0.0f;
                break;
            }
            case Stage::Release:
            {
                float effectiveReleaseTime = forcedFastRelease ? 0.005f : releaseTime;
                float releaseRate = 1.0f / (sampleRate * std::max(0.001f, effectiveReleaseTime));
                currentLevel -= releaseRate * releaseStartLevel;
                if (currentLevel <= 0.0f)
                {
                    currentLevel = 0.0f;
                    stage = Stage::Idle;
                }
                break;
            }
        }
        
        return currentLevel;
    }
    
    bool isActive() const { return stage != Stage::Idle; }
    float getCurrentLevel() const { return currentLevel; }
    void reset() { stage = Stage::Idle; currentLevel = 0.0f; gateHeld = false; forcedFastRelease = false; }
    
private:
    double sampleRate = 44100.0;
    Stage stage = Stage::Idle;
    float currentLevel = 0.0f;
    float releaseStartLevel = 0.0f;
    
    float attackTime = 0.1f;
    float decayTime = 0.5f;
    float sustainLevel = 0.8f;
    float releaseTime = 1.0f;
    bool isLooping = false;
    bool gateHeld = false;
    bool gridSyncEnabled = false;
    float loopDurationSeconds = 0.5f;
    bool forcedFastRelease = false;
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    SynthVoice(int voiceIndex);
    
    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int controllerNumber, int newControllerValue) override;
    void prepareToPlay(double sampleRate, int samplesPerBlock, int outputChannels);
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;
    bool isVoiceActive() const override;
    bool isKeyDown() const { return !isKeyReleased && env.isActive(); }

    void updateParameters(const SeedGenerator::PatchParameters& params, float globalCutoff, float globalReso,
                          float globalAttack, float globalDecay, float globalSustain, float globalRelease,
                          bool envLoop, float envLoopMs, bool driftAlive, int oscModeIdx, float randomSeed,
                          int filterTypeIdx, int unisonModeIdx, int octaveShiftIdx,
                          float filterAttack, float filterDecay, float filterSustain, float filterRelease, float filterEnvAmount);
    void setLoopSyncTiming(bool enabled, double firstBoundarySample, double intervalSamples);
    void setNotePulseTarget(std::atomic<float>* target) { env.setLoopPulseTarget(target); }
    void setWavetableBank(const WavetableBank* bank)
    {
        if (bank != nullptr && bank != currentWavetableBank)
        {
            currentWavetableBank = bank;
            osc1.setWavetable(&bank->tables[0]);
            osc2.setWavetable(&bank->tables[1]);
            oscUnison1.setWavetable(&bank->tables[2]);
            oscUnison2.setWavetable(&bank->tables[3]);
            oscUnison3.setWavetable(&bank->tables[4]);
        }
    }

private:
    const WavetableBank* currentWavetableBank = nullptr;
    int voiceIdx;
    int unisonMode = 1; // 0 = 2VU, 1 = 5VU
    
    DPWOscillator osc1;        // Main Osc (Center)
    DPWOscillator osc2;        // Detune Osc 1 (Mid Right)
    DPWOscillator oscUnison1; // Detune Osc 2 (Mid Left)
    DPWOscillator oscUnison2; // Detune Osc 3 (Outer Right)
    DPWOscillator oscUnison3; // Detune Osc 4 (Outer Left)
    DPWOscillator subOsc;     // Sub (triangle, -1 oct)
    
    juce::dsp::LadderFilter<float> filter;
    LoopingEnvelope env;
    LoopingEnvelope filterEnv;
    bool loopGridSyncEnabled = false;
    int64_t nextLoopBoundarySample = 0;
    int64_t loopBoundaryIntervalSamples = 1;
    ReplicantDrift drift;
    
    juce::dsp::Gain<float> gain;

    float driftAmount = 0.5f;
    float currentBaseFrequency = 440.0f;
    float currentDetune = 0.05f;
    float oscMorph = 0.5f;
    float subLevel = 0.0f;
    float voicePan = 0.5f;       // 0=left, 1=right
    float stereoWidth = 0.5f;
    SeedGenerator::OscMode oscMode1 = SeedGenerator::OscMode::Morph;
    SeedGenerator::OscMode oscMode2 = SeedGenerator::OscMode::Morph;

    float baseCutoff = 1000.0f;
    float filterEnvAmount = 1000.0f;
    float currentVelocity = 1.0f;
    juce::SmoothedValue<float> smoothedVelocity;
    juce::SmoothedValue<float> smoothedVoiceCutoff;

    bool isMonophonic = false;
    bool isKeyReleased = true;
    float targetFrequency = 440.0f;
    float glideRate = 0.95f;

    double currentSampleRate = 44100.0;
    bool isPrepared = false;

    float currentSeed = -1.0f;
    int filterType = 0;
    int octaveShift = 0;
};

class SynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int /*midiNoteNumber*/) override { return true; }
    bool appliesToChannel (int /*midiChannel*/) override { return true; }
};
