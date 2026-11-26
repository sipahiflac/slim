#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "AtomicDSP.h"
#include "LicenseManager.h"
#include <atomic>
#include <vector>

struct CrystalPreset
{
    int seed = 0;
    float knobs[3] { 0.55f, 0.45f, 0.40f };
    float drive = 0.40f;
    float tilt  = 0.0f;
};

class CrystalAudioProcessor : public juce::AudioProcessor
{
public:
    CrystalAudioProcessor();
    ~CrystalAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Wildin'"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.15; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI Helper API
    int getCurrentSeed() const noexcept;
    void mutateChain();
    bool canUndo() const noexcept { return ! history.empty(); }
    void undoMutation();

    void applyPreset (const CrystalPreset& p, bool pushUndo = true);
    void toggleSaveCurrentPreset();
    void deletePresetAt (int index);
    bool isCurrentSeedSaved() const;
    const std::vector<CrystalPreset>& getSavedPresets() const noexcept { return savedPresets; }

    void triggerMechanicalClick (int dir = 1) noexcept;

    // License & Version API
    static constexpr const char* kPluginVersion = "1.0.0";
    bool isLicensed() const noexcept { return licensed.load (std::memory_order_acquire); }
    juce::String getLicenseKey() const { return activeLicenseKey; }
    bool tryActivateLicense (const juce::String& key);
    void deactivateLicense();

    // Lock-free oscilloscope ring buffer for UI
    static constexpr int kScopeSize = 2048;
    float scopeRing[kScopeSize] {};
    std::atomic<int> scopeWritePos { 0 };
    std::atomic<float> lastInputLevel { 0.0f };

    // Boot animasyonu uygulama/eklenti oturumu basina yalnizca 1 kez oynar!
    bool hasPlayedBootAnim = false;
    // Son pencere boyutu (Ableton Auto-Scale ac/kapa ve yeniden acilista korunur)
    int lastEditorW = 340;
    int lastEditorH = 384;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void loadPresetsFromDisk();
    void savePresetsToDisk() const;
    juce::File getPresetsFile() const;
    void pushHistory();

    double currentSr = 44100.0;
    double sK[3] { 0.55, 0.45, 0.40 };
    double sDrive = 0.40, sTilt = 0.0;
    uint32_t dspRng = 2463534242u;
    CrystalDSP::ChannelState channels[2];

    // Embedded IBM Model M click playback (RT-safe)
    std::array<juce::AudioBuffer<float>, 3> clickBuffers;
    std::atomic<int> clickReqIdx { -1 };
    int activeClickIdx = -1;
    double activeClickPos = 0.0;
    double activeClickStep = 1.0;
    std::atomic<int> clickRR { 0 };

    // Saved presets & undo history (UI thread)
    std::vector<CrystalPreset> savedPresets;
    std::vector<CrystalPreset> history;
    juce::Random uiRng;

    std::atomic<int> currentSeed { CrystalDSP::encodeChainToSeed (6, 2, 13, 1) };
    std::atomic<bool> licensed { false };
    juce::String activeLicenseKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrystalAudioProcessor)
};
