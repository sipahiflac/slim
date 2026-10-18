#pragma once

#include "LoopFinder.h"
#include "Params.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <atomic>
#include <vector>

enum class PlaybackMode
{
    Auto,
    ForceLoop,
    OneShot
};

struct Instrument
{
    bool uploaded = false;
    bool importDraft = false;
    juce::String name;
    juce::String category = "Generated";
    std::vector<float> tick; // kEngineSampleRate mono sample (one-shot transient)
    std::vector<float> body; // kEngineSampleRate mono waveform (looped)
    std::vector<float> originalBody; // Immutable decoded source for Raw/Proc comparison.
    // Where `body` begins inside `originalBody` (the leading silence trimmed off). Loop
    // regions are in `body` coordinates, so playing the original shifts them by this.
    int originalOffset = 0;
    VoiceParams defaultParams;
    float baseNote = 60.0f; // Standard MIDI base note (C4 = 60)
    float outputGain = 1.0f; // Load-time calibration; never changes the sample/loop data.
    bool oneShotBody = false;
    PlaybackMode playbackMode = PlaybackMode::Auto;
    int loopStart = 0; // Auto-detected loop start sample
    int loopEnd = -1;  // Auto-detected loop end sample (-1 = buffer end)
    std::vector<LoopRegion> loopRegions; // Up to three distinct, ranked choices.
    std::shared_ptr<std::atomic<int>> selectedLoop; // 0 = Off, 1..N = candidate.

    // Non-destructive editor controls. Normalised bounds make the same edit
    // meaningful for processed/raw versions whose sample counts may differ.
    std::shared_ptr<std::atomic<float>> playbackStart;
    std::shared_ptr<std::atomic<float>> playbackEnd;

    // The instrument's OWN measured die-away, in dB per second, taken from
    // the sustain region of the source recording. A struck/plucked sound
    // (piano, electric piano, bell) has no steady state -- it keeps decaying
    // for as long as it rings -- so holding a key must keep decaying too,
    // instead of freezing the loop at a constant level like an organ. A
    // continuously-blown sound (flute, choir, pad) measures ~0 here and so
    // holds flat, which is equally correct for it. The loop region itself is
    // level-flattened at load time, so this envelope decay is what restores
    // the natural die-away. 0 = hold flat.

    // Generated-sample state. Shared atomics let the UI change mapping/source
    // without touching sample storage that an active audio voice is reading.
    std::shared_ptr<std::atomic<float>> liveBaseNote;
    std::shared_ptr<std::atomic<bool>> useOriginalSource;
    bool pitchDetected = false;
    int rootMidiNote = 60;
    float rootCents = 0.0f;
    float pitchConfidence = 0.0f;
    juce::String sourcePrompt;
    juce::String generatedAt;
    juce::String processedFile;
    juce::String originalFile;
    juce::String sourceFile; // WAV whose sibling .2g.json stores editor state.
};

struct PreparedGeneratedSample
{
    bool uploaded = false;
    bool importDraft = false;
    std::vector<float> original;
    std::vector<float> processed;
    int originalOffset = 0; // See Instrument::originalOffset.
    float outputGain = 1.0f; // Load-time level calibration, measured off the audio thread.
    std::vector<float> waveformMin;
    std::vector<float> waveformMax;
    bool pitched = false;
    int rootMidiNote = 60;
    float rootCents = 0.0f;
    float pitchConfidence = 0.0f;
    bool oneShot = true;
    int loopStart = 0;
    int loopEnd = 0;
    std::vector<LoopRegion> loopRegions;
    juce::String prompt;
    juce::String generatedAt;
    juce::String processedFile;
    juce::String originalFile;
    juce::String error;
};

struct ToneCard
{
    juce::String cardName;
    juce::StringArray categories;
    std::vector<Instrument> instruments;

    static void appendGeneratedSamples(ToneCard& card, const juce::File& folder, bool uploaded = false);

    // Decode an encoded audio response (MP3/WAV/etc.), mix it to mono and
    // resample it to the engine rate. Used by the on-device sample maker.
    static std::vector<float> decodeAudioMemory(const juce::MemoryBlock& audioData);
    static PreparedGeneratedSample prepareGeneratedSample(const juce::MemoryBlock& audioData,
                                                          const juce::String& prompt,
                                                          bool forceUnpitched = false, bool saveFiles = true);
    static void analyseGeneratedPlayback(PreparedGeneratedSample& sample);
    static void saveGeneratedSampleFiles(const PreparedGeneratedSample& sample, float exactRootNote);

};
