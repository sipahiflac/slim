#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/SynthVoice.h"
#include "DSP/SeedGenerator.h"
#include "DSP/Effects.h"
#include "UI/PresetManager.h"
#include "UI/Fonts.h"

class MonoPolySynth : public juce::Synthesiser
{
public:
    bool isMonophonic = false;
    
    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound* soundToPlay, int midiNoteNumber, int pitchWheelPosition, bool stealIfNoneAvailable) const override
    {
        if (isMonophonic && getNumVoices() > 0)
        {
            return getVoice(0);
        }
        return juce::Synthesiser::findFreeVoice(soundToPlay, midiNoteNumber, pitchWheelPosition, stealIfNoneAvailable);
    }
};

class StableAliveAudioProcessor : public juce::AudioProcessor
{
public:
    using juce::AudioProcessor::processBlock;

    StableAliveAudioProcessor();
    ~StableAliveAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    /** @param notifyHost  false only while the processor is still being constructed -
                           the host has no listeners attached yet at that point. */
    void writePatchToParameters(const SeedGenerator::PatchParameters& patch, bool notifyHost = true);

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    SeedGenerator seedGenerator;
    float uiScale = 1.0f;
    PresetManager presetManager;
    std::atomic<float> noteReactionPulse{ 0.0f };
    std::atomic<float> lfoCurrentValue{ 0.0f };
    std::atomic<float> lfoCurrentPhase{ 0.0f };

    // Keeps Poppins typefaces and grain alive in memory so opening/closing editor has 0ms font overhead
    WXFonts::KeepAlive fontKeepAlive;
    WXGrain::KeepAlive grainKeepAlive;

    // Cache procedural weather image so opening the editor doesn't re-calculate Perlin noise
    juce::Image cachedWeatherImage;
    juce::uint32 cachedWeatherSeedHash = 0;

    // Public synth reference so the editor can inject MIDI directly
    MonoPolySynth& getSynth() { return synth; }

    // Double-buffered wavetable bank shared across all voices (zero CPU buffer overrun on randomize)
    WavetableBank wavetableBanks[2];
    std::atomic<int> activeWavetableBank{ 0 };
    void prepareWavetablesForSeed(float seed);

private:
    SeedGenerator::PatchParameters cachedPatch;
    juce::uint32 lastCachedSeedHash = 0;
    int silentBlocksCount = 0;
    bool effectsIdleFlushed = false;

    float lfoPhase = 0.0f;
    float lfoLastRandVal = 0.0f;
    float lfoTargetRandVal = 0.0f;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    MonoPolySynth synth;
    SpaceEffects effects;
    
    std::vector<int> heldNotes;
    int activeMidiNote = -1;
    bool wasMonophonic = false;

    float releaseTimerSeconds = 0.0f;
    float smoothedDriftFadeIn = 0.0f;

    // Decimator buffers
    float decimatorCounters[2] = {0.0f, 0.0f};
    float decimatorHeldSamples[2] = {0.0f, 0.0f};

    // RMS Auto-gain follower
    float smoothedRMS = 0.12f;
    float currentAutoGain = 1.0f;

    std::unique_ptr<juce::dsp::Oversampling<float>> masterOversampling;
    // Block size the DSP graph was prepared for. Some hosts (and the standalone
    // app after a device change) can hand us a larger block than prepareToPlay
    // announced; processing that through the oversampler would overrun its
    // internal buffers, so we bypass it instead of crashing.
    int preparedBlockSize = 512;

    double processorSampleRate = 44100.0;
    void triggerRandomDucking()
    {
        randomDuckingGain.setCurrentAndTargetValue(0.0f);
        randomDuckingGain.setTargetValue(1.0f);
    }

private:
    juce::SmoothedValue<float> randomDuckingGain;
    double internalPpqPosition = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StableAliveAudioProcessor)
};
