#pragma once
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include "SoundEngine.h"

class Melodragon2GProcessor final : public juce::AudioProcessor
{
public:
    Melodragon2GProcessor();
    ~Melodragon2GProcessor() override = default;

    const juce::String getName() const override { return "2G"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destinationData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    SoundEngine& getEngine() noexcept { return engine; }
    juce::StringArray getSoundNames() const;
    int getSelectedSound() const noexcept { return engine.getSelectedInstrument(); }
    void selectSound(int index);
    bool shouldShowStartupConsole() const noexcept { return !startupConsoleShown.load(); }
    void markStartupConsoleShown() noexcept { startupConsoleShown.store(true); }

private:
    SoundEngine engine;
    std::atomic<bool> startupConsoleShown { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Melodragon2GProcessor)
};
