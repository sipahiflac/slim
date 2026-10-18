#pragma once
#include <cmath>

#include "Voice.h"
#include "ToneCard.h"
#include "EngineConstants.h"
#include "SpeakerDynamics.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// Direct Form II Transposed Biquad Filter (numerically stable, real-time safe)
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    inline float process(float in) noexcept
    {
        float out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return out;
    }

    static Biquad makeHPF(float f0, float Q, float Fs) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        double w0 = 2.0 * kPi * (double)f0 / (double)Fs;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * (double)Q);

        double b0 =  (1.0 + cosw0) / 2.0;
        double b1 = -(1.0 + cosw0);
        double b2 =  (1.0 + cosw0) / 2.0;
        double a0 =   1.0 + alpha;
        double a1 =  -2.0 * cosw0;
        double a2 =   1.0 - alpha;

        Biquad bq;
        bq.b0 = (float)(b0 / a0);
        bq.b1 = (float)(b1 / a0);
        bq.b2 = (float)(b2 / a0);
        bq.a1 = (float)(a1 / a0);
        bq.a2 = (float)(a2 / a0);
        return bq;
    }

    static Biquad makeLPF(float f0, float Q, float Fs) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        double w0 = 2.0 * kPi * (double)f0 / (double)Fs;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * (double)Q);

        double b0 =  (1.0 - cosw0) / 2.0;
        double b1 =   1.0 - cosw0;
        double b2 =  (1.0 - cosw0) / 2.0;
        double a0 =   1.0 + alpha;
        double a1 =  -2.0 * cosw0;
        double a2 =   1.0 - alpha;

        Biquad bq;
        bq.b0 = (float)(b0 / a0);
        bq.b1 = (float)(b1 / a0);
        bq.b2 = (float)(b2 / a0);
        bq.a1 = (float)(a1 / a0);
        bq.a2 = (float)(a2 / a0);
        return bq;
    }

    static Biquad makePeak(float f0, float Q, float gainDB, float Fs) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        double A = std::pow(10.0, (double)gainDB / 40.0);
        double w0 = 2.0 * kPi * (double)f0 / (double)Fs;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * (double)Q);

        double b0 =   1.0 + alpha * A;
        double b1 =  -2.0 * cosw0;
        double b2 =   1.0 - alpha * A;
        double a0 =   1.0 + alpha / A;
        double a1 =  -2.0 * cosw0;
        double a2 =   1.0 - alpha / A;

        Biquad bq;
        bq.b0 = (float)(b0 / a0);
        bq.b1 = (float)(b1 / a0);
        bq.b2 = (float)(b2 / a0);
        bq.a1 = (float)(a1 / a0);
        bq.a2 = (float)(a2 / a0);
        return bq;
    }
};

class SoundEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    SoundEngine();
    ~SoundEngine() override;

    // Load and compact the user's WAV instruments on startup.
    void loadDefaultToneCard();

    // The slow half of loadDefaultToneCard: decodes, analyses and calibrates
    // every instrument. Touches no engine state, so it may run on any thread.
    static ToneCard buildDefaultToneCard();

    // Loudness trim for one instrument: probes it through its own voice and the speaker chain.
    // Touches no engine state, so it may run on any thread. Every sound needs this, whether it is
    // loaded at startup or generated/imported later, or the same sound plays louder until relaunch.
    static float measureOutputGain(Instrument& instrument);
    // Same, for a freshly prepared sample; call on the worker thread before installGeneratedSample().
    static void calibratePrepared(PreparedGeneratedSample& prepared);

    // Swaps in a built card. Call on the message thread while the audio
    // callback is detached.
    void installToneCard(ToneCard card);

    // UI / External Control APIs (thread-safe, lock-free)
    void noteOn(int midiNote, float velocity = 0.9f);
    void noteOff(int midiNote);
    void allNotesOff();
    void selectVoice(int instrumentIndex);
    void previewVoice(int instrumentIndex);
    void setMasterVolume(float gain) noexcept
    {
        if (std::isfinite(gain)) masterVolume.store(juce::jlimit(0.0f, 2.0f, gain), std::memory_order_relaxed);
    }
    float getMasterVolume() const noexcept { return masterVolume.load(std::memory_order_relaxed); }
    VoiceParams getVoiceParams(int instrumentIndex) const;
    void setVoiceParam(int instrumentIndex, int paramIndex, float value);

    // Speaker Simulation A/B Comparison Toggle
    void toggleSpeakerSimulation() noexcept
    {
        speakerSimEnabled.store(!speakerSimEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    bool isSpeakerSimulationEnabled() const noexcept
    {
        return speakerSimEnabled.load(std::memory_order_relaxed);
    }

    // Movement Layer A/B Comparison Toggle (Pitch and brightness drift)
    void toggleMovement() noexcept
    {
        movementEnabled.store(!movementEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    bool isMovementEnabled() const noexcept
    {
        return movementEnabled.load(std::memory_order_relaxed);
    }

    // Get current ToneCard instrument names for WebGUI
    juce::StringArray getInstrumentNames() const;
    juce::var getInstrumentLibrary() const;
    juce::var getInstrumentEditorData(int instrumentIndex) const;
    juce::var setInstrumentEditorControls(int instrumentIndex, float start, float end, int rootMidiNote, float rootCents = NAN, int pitched = -1);
    juce::var setInstrumentLoop(int instrumentIndex, int selection);
    juce::String getInstrumentSettingsPath(int instrumentIndex) const;
    juce::String getInstrumentSettingsJson(int instrumentIndex) const;
    juce::var getSessionState() const;
    void restoreSessionState(const juce::var& state);
    int getCurrentInstrumentIndex() const { return selectedInstrumentIndex.load(); }

    // The caller must temporarily detach the audio callback while this
    // replaces the generated instrument and its sample storage.
    int getSelectedInstrument() const { return selectedInstrumentIndex.load(); }
    juce::var commitImportedSample(int index, const juce::String& processed, const juce::String& original);
    juce::var installGeneratedSample(PreparedGeneratedSample prepared);
    juce::var setGeneratedSampleControls(int instrumentIndex, int rootMidiNote,
                                         bool pitched, bool useOriginal);
    PreparedGeneratedSample copyGeneratedSampleForSaving(int instrumentIndex) const;
    juce::var deleteGeneratedSample(int instrumentIndex);
    // Generated and uploaded sounds only. Message thread; the audio thread never reads the name.
    juce::var renameInstrument(int instrumentIndex, const juce::String& newName);

    // juce::AudioIODeviceCallback overrides
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                         int numInputChannels,
                                         float* const* outputChannelData,
                                         int numOutputChannels,
                                         int numSamples,
                                         const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // Host-facing path used by the VST3. The standalone callback delegates to
    // the same code, so both products run the exact same engine and resampler.
    void prepareForPlayback(double sampleRate);
    bool getNoteGate() const noexcept { return displayGate.load(std::memory_order_relaxed); }
    juce::var getDisplayInputState() const {
        auto* state = new juce::DynamicObject();
        state->setProperty("gate", getNoteGate());
        state->setProperty("noteOns", static_cast<int>(displayNoteOnSerial.load(std::memory_order_relaxed)));
        return juce::var(state);
    }
    void renderAudioBlock(float* const* outputChannelData,
                          int numOutputChannels,
                          int numSamples);

    // juce::MidiInputCallback override
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

private:
    std::atomic<bool> displayGate{false};
    std::atomic<unsigned int> displayNoteOnSerial{0};
    std::array<bool,128> heldDisplayNotes{};
    int heldDisplayCount=0;
    struct AudioEvent
    {
        enum class Type { NoteOn, NoteOff, AllNotesOff, SelectVoice, PreviewVoice, SetParam };
        Type type = Type::NoteOn;
        int note = 60;
        float velocity = 0.9f;
        int instrumentIndex = 0;
        int paramIndex = 0;
        float paramValue = 1.0f;
    };

    static constexpr uint32_t kQueueSize = 256;
    static constexpr uint32_t kQueueMask = 255;
    AudioEvent eventQueue[kQueueSize];
    std::atomic<uint32_t> writeIndex{ 0 };
    std::atomic<uint32_t> readIndex{ 0 };
    juce::SpinLock producerLock; // Producers only; audio callback never locks.
    void pushEvent(const AudioEvent& ev);
    void processEventInAudioThread(const AudioEvent& ev);

    // Audio Engine State
    ToneCard toneCard;
    std::vector<VoiceParams> editableParams;
    std::atomic<int> selectedInstrumentIndex{ 0 };
    int activeInstrumentInAudioThread = 0;

    // 4 Voices (polyphony limit)
    std::array<Voice, 4> voices;
    // Fixed-size old-wave tails for a short, real waveform crossfade when a
    // four-voice slot is retriggered or stolen. No audio-thread allocations.
    static constexpr int kStealFadeSamples = 80; // 5 ms at 16 kHz
    std::array<Voice, 4> stealTails;
    std::array<int, 4> stealFadeRemaining{};
    void startVoiceInSlot(int slot, int note, float velocity, const Instrument* instrument,
                          const VoiceParams& params) noexcept;
    void clearStealTails() noexcept;

    double hostSampleRate = 44100.0;
    double sourcePhase = 0.0;
    float prevEngineSample = 0.0f;
    float currEngineSample = 0.0f;
    std::atomic<float> masterVolume{ 1.0f };
    juce::SmoothedValue<float> masterGain{ 1.0f };

    // Speaker DSP chain at kEngineSampleRate:
    // 1) 4th-order HPF at 300 Hz (two 2nd-order Butterworth biquads)
    Biquad hpf1, hpf2;
    // 2) 4th-order LPF at 4000 Hz (two 2nd-order Butterworth biquads)
    Biquad lpf1, lpf2;
    // 3) Resonances: 2 kHz primary peak (+2.5 dB) + 1.2 kHz secondary peak (+1.5 dB) --
    // gentle presence lift only; Voice's own Formant knob provides the main coloring
    Biquad peak2k;
    Biquad peak1k2;
    SpeakerDynamics speakerDynamics;
    // Independent excitation and listening level; output trim cannot heat the model.
    static constexpr float speakerDrive = 1.6f;
    static constexpr float speakerOutputTrim = 0.8f;

    // A/B Comparison Bypass Toggle
    std::atomic<bool> speakerSimEnabled{ true };
    std::atomic<bool> movementEnabled{ false };

    void initSpeakerFilters();
    void resetSpeakerChain() noexcept;
    float processSpeakerChain(float monoIn, bool saturate = true) noexcept;

    // Preview timer countdown in engine samples
    int previewCountdown = 0;
    int previewVoiceIndex = -1;

    float renderEngineSample();
};
