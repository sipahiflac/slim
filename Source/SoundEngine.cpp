#include "SoundEngine.h"
#include "SoundStore.h"
#include "ParallelFor.h"
#include <cmath>
#include <algorithm>

SoundEngine::SoundEngine()
{
    initSpeakerFilters();
}

SoundEngine::~SoundEngine()
{
}

void SoundEngine::initSpeakerFilters()
{
    constexpr float Fs = kEngineSampleRateF;
    // 1) 4th-order HPF at 300 Hz (cascaded Butterworth Q1=0.5412, Q2=1.3066)
    hpf1 = Biquad::makeHPF(300.0f, 0.5412f, Fs);
    hpf2 = Biquad::makeHPF(300.0f, 1.3066f, Fs);

    // 2) 4th-order LPF at 4000 Hz (cascaded Butterworth Q1=0.5412, Q2=1.3066)
    lpf1 = Biquad::makeLPF(4000.0f, 0.5412f, Fs);
    lpf2 = Biquad::makeLPF(4000.0f, 1.3066f, Fs);

    // 3) Resonances: gentle presence bumps, not a formant filter of their
    // own. Each Voice already runs its own adjustable Formant coloring
    // (Voice::Resonator, ~800-1700 Hz by default); stacking a second, fixed
    // +7 dB / Q3.5 peak from here on top of that pushed the same 1.2-2 kHz
    // region well past +10 dB combined, which reads as a honky, narrowed
    // tone rather than an authentic small-speaker character. These softer
    // values still give the "tinny handset" presence lift without drowning
    // out the rest of the spectrum.
    peak2k  = Biquad::makePeak(2000.0f, 2.5f, 2.5f, Fs);
    peak1k2 = Biquad::makePeak(1200.0f, 1.5f, 1.5f, Fs);
}

float SoundEngine::processSpeakerChain(float monoIn, bool saturate) noexcept
{
    // 1) 4th-order HPF (300 Hz)
    float x = hpf1.process(monoIn);
    x = hpf2.process(x);

    // Excursion/velocity-dependent mechanics + slow thermal memory.
    x = speakerDynamics.process(x * speakerDrive, saturate) / speakerDrive;

    // 3) Resonant peaks: gentle 2 kHz + 1.2 kHz presence lift, kept subtle so
    // it doesn't stack into a honky boost on top of the per-voice Formant EQ
    x = peak2k.process(x);
    x = peak1k2.process(x);

    // Acoustic roll-off follows the nonlinear source, suppressing generated
    // upper harmonics. The two substeps reduce, but do not eliminate aliasing.
    x = lpf1.process(x);
    x = lpf2.process(x);
    x *= speakerOutputTrim;

    if (!saturate)
        return x;

    // Bounded emergency output protection, separate from the transducer model.
    return x / std::sqrt(1.0f + x*x);
}

void SoundEngine::resetSpeakerChain() noexcept
{
    hpf1.reset();
    hpf2.reset();
    lpf1.reset();
    lpf2.reset();
    peak2k.reset();
    peak1k2.reset();
    speakerDynamics.reset();
}

void SoundEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    prepareForPlayback(device != nullptr ? device->getCurrentSampleRate() : 44100.0);
}

void SoundEngine::prepareForPlayback(double sampleRate)
{
    hostSampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    masterGain.reset(hostSampleRate, 0.015);
    masterGain.setCurrentAndTargetValue(getMasterVolume());

    sourcePhase = 0.0;
    prevEngineSample = 0.0f;
    currEngineSample = 0.0f;
    previewCountdown = 0;
    previewVoiceIndex = -1;

    resetSpeakerChain();
    heldDisplayNotes.fill(false);heldDisplayCount=0;displayGate.store(false,std::memory_order_relaxed);

    for (auto& v : voices)
        v.stop();
    clearStealTails();
}

void SoundEngine::audioDeviceStopped()
{
    for (auto& v : voices)
        v.stop();
    clearStealTails();
}

void SoundEngine::clearStealTails() noexcept
{
    stealFadeRemaining.fill(0);
    for (auto& tail : stealTails) tail.stop();
}

void SoundEngine::startVoiceInSlot(int slot, int note, float velocity,
                                   const Instrument* instrument, const VoiceParams& params) noexcept
{
    const auto index = (size_t)slot;
    if (voices[index].isActive())
    {
        stealTails[index] = voices[index];
        stealFadeRemaining[index] = kStealFadeSamples;
    }
    else
    {
        stealTails[index].stop();
        stealFadeRemaining[index] = 0;
    }
    voices[index].noteOn(note, velocity, instrument, params);
}

float SoundEngine::renderEngineSample()
{
    // Check preview note timer
    if (previewCountdown > 0)
    {
        if (--previewCountdown == 0 && previewVoiceIndex >= 0)
        {
            voices[(size_t)previewVoiceIndex].fadeOut(0.06f);
            previewVoiceIndex = -1;
        }
    }

    // Mix 4 voices into mono with movement layer status
    const bool movEnabled = movementEnabled.load(std::memory_order_relaxed);
    const float bend = getPitchBendSemitones();
    float rawMono = 0.0f;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        voices[i].setPitchBend(bend);
        stealTails[i].setPitchBend(bend);
        const float fresh = voices[i].renderSample(movEnabled);
        int& remaining = stealFadeRemaining[i];
        if (remaining > 0)
        {
            const float old = stealTails[i].renderSample(movEnabled);
            const float t = 1.0f - (float)remaining / (float)kStealFadeSamples;
            const float newGain = t * t * (3.0f - 2.0f * t); // zero slope at both ends
            rawMono += old * (1.0f - newGain) + fresh * newGain;
            if (--remaining == 0) stealTails[i].stop();
        }
        else rawMono += fresh;
    }

    // Bypass the speaker colour, retaining a bounded soft overload safeguard
    // for four calibrated voices summed together.
    if (!speakerSimEnabled.load(std::memory_order_relaxed))
        return rawMono / std::sqrt(1.0f + rawMono * rawMono);

    // One shared physical state for all four voices (chords share excursion/heat).
    const float output = processSpeakerChain(rawMono);
    if (!std::isfinite(output))
    {
        // The speaker model and filters feed back on themselves, so one bad sample would silence
        // the engine until restart. Drop it and start the chain clean.
        resetSpeakerChain();
        return 0.0f;
    }
    return output;
}

void SoundEngine::loadDefaultToneCard()
{
    installToneCard(buildDefaultToneCard());
}

float SoundEngine::measureOutputGain(Instrument& instrument)
{
    // Fixed, conservative load-time trim. Measure the LINEAR speaker path:
    // saturated peaks cannot predict safe gain before that saturation.
    // Check a two-octave range and short transients as well as sustained energy.
    if (instrument.liveBaseNote == nullptr)
        instrument.liveBaseNote = std::make_shared<std::atomic<float>>(instrument.baseNote);
    if (instrument.playbackStart == nullptr)
        instrument.playbackStart = std::make_shared<std::atomic<float>>(0.0f);
    if (instrument.playbackEnd == nullptr)
        instrument.playbackEnd = std::make_shared<std::atomic<float>>(1.0f);
    if (instrument.selectedLoop == nullptr)
        instrument.selectedLoop = std::make_shared<std::atomic<int>>(instrument.loopRegions.empty() ? 0 : 1);
    instrument.outputGain = 1.0f;

    const int window = (int)(kEngineSampleRate * 0.2);
    const int shortWindow = (int)(kEngineSampleRate * 0.02);
    const int ringOut = (int)(kEngineSampleRate * 0.05);
    double maxEnergy = 0.0, maxShortEnergy = 0.0;
    float rawPeak = 0.0f, speakerPeak = 0.0f;
    // One speaker model for the whole instrument (on the heap, it is large), reset per note.
    auto speakerProbe = std::make_unique<SoundEngine>();
    std::vector<double> energies((size_t)window), shortEnergies((size_t)shortWindow);
    for (int note : {48, 60, 72})
    {
        Voice probe;
        speakerProbe->resetSpeakerChain();
        probe.noteOn(note, 1.0f, &instrument, instrument.defaultParams);
        std::fill(energies.begin(), energies.end(), 0.0);
        std::fill(shortEnergies.begin(), shortEnergies.end(), 0.0);
        double rollingEnergy = 0.0, shortEnergy = 0.0;
        int silent = 0;
        for (int i = 0; i < (int)(kEngineSampleRate * 6.0); ++i)
        {
            const float raw = probe.renderSample(false);
            const float sample = speakerProbe->processSpeakerChain(raw, false);
            rawPeak = std::max(rawPeak, std::abs(raw));
            speakerPeak = std::max(speakerPeak, std::abs(sample));
            const size_t slot = (size_t)(i % window);
            const size_t shortSlot = (size_t)(i % shortWindow);
            const double energy = (double)sample * sample;
            rollingEnergy += energy - energies[slot];
            shortEnergy += energy - shortEnergies[shortSlot];
            energies[slot] = shortEnergies[shortSlot] = energy;
            maxEnergy = std::max(maxEnergy, rollingEnergy);
            maxShortEnergy = std::max(maxShortEnergy, shortEnergy);
            // A one-shot that has finished can only ring out quieter than it played.
            if (!probe.isActive() && ++silent > ringOut) break;
        }
    }
    const float rms = (float)std::sqrt(maxEnergy / window);
    const float shortRms = (float)std::sqrt(maxShortEnergy / shortWindow);
    if (rms > 0.0001f && rawPeak > 0.0001f)
        return std::min({ 0.1f / rms,
                          0.126f / std::max(shortRms, 0.0001f),
                          0.22f / std::max(speakerPeak, 0.0001f),
                          0.5f / rawPeak, 2.0f });
    return 1.0f;
}

void SoundEngine::calibratePrepared(PreparedGeneratedSample& prepared)
{
    // Mirror what installGeneratedSample() will build, with the default sound-maker parameters.
    const float baseNote = (float)prepared.rootMidiNote + (prepared.pitched ? prepared.rootCents / 100.0f : 0.0f);
    Instrument probe;
    probe.body = prepared.processed;
    probe.baseNote = baseNote;
    probe.liveBaseNote = std::make_shared<std::atomic<float>>(baseNote);
    probe.useOriginalSource = std::make_shared<std::atomic<bool>>(false);
    probe.playbackStart = std::make_shared<std::atomic<float>>(0.0f);
    probe.playbackEnd = std::make_shared<std::atomic<float>>(1.0f);
    probe.loopRegions = prepared.loopRegions;
    probe.selectedLoop = std::make_shared<std::atomic<int>>(prepared.oneShot || prepared.loopRegions.empty() ? 0 : 1);
    probe.defaultParams = { 10, 1, 3, 5, 1, 10, 5 };
    prepared.outputGain = measureOutputGain(probe);
}

ToneCard SoundEngine::buildDefaultToneCard()
{
    SoundStore::migrateLegacyGenerated(); // older installs: Documents/.../Generated/*.wav -> hidden .2gs
    ToneCard toneCard;
    toneCard.cardName = "User sounds";
    toneCard.categories.add("Generated");
    toneCard.categories.add("Uploaded");
    ToneCard::appendGeneratedSamples(toneCard, SoundStore::generatedFolder());
    ToneCard::appendGeneratedSamples(toneCard, SoundStore::uploadedFolder(), true);
    // Fixed, conservative load-time trim. Measure the LINEAR speaker path:
    // saturated peaks cannot predict safe gain before that saturation.
    // Check a two-octave range and short transients as well as sustained energy.
    // Every instrument is probed with its own voice and speaker, so they run in parallel.
    parallelFor((int)toneCard.instruments.size(), [&toneCard](int index)
    {
        auto& instrument = toneCard.instruments[(size_t)index];
        if (instrument.liveBaseNote == nullptr)
            instrument.liveBaseNote = std::make_shared<std::atomic<float>>(instrument.baseNote);
        if (instrument.playbackStart == nullptr)
            instrument.playbackStart = std::make_shared<std::atomic<float>>(0.0f);
        if (instrument.playbackEnd == nullptr)
            instrument.playbackEnd = std::make_shared<std::atomic<float>>(1.0f);
        if (instrument.selectedLoop == nullptr)
            instrument.selectedLoop = std::make_shared<std::atomic<int>>(instrument.loopRegions.empty() ? 0 : 1);
        instrument.outputGain = measureOutputGain(instrument);
    });
    return toneCard;
}

void SoundEngine::installToneCard(ToneCard card)
{
    for (auto& voice : voices) voice.stop();
    clearStealTails();
    toneCard = std::move(card);
    editableParams.clear();
    for (const auto& instrument : toneCard.instruments)
        editableParams.push_back(instrument.defaultParams);

    // Notes pressed while the card was loading must not all fire at once when
    // audio starts; keep only the voice the UI has selected by now.
    {
        const juce::SpinLock::ScopedLockType guard(producerLock);
        readIndex.store(writeIndex.load(std::memory_order_relaxed), std::memory_order_release);
    }
    const int selected = std::clamp(selectedInstrumentIndex.load(), 0,
                                    std::max(0, (int)toneCard.instruments.size() - 1));
    selectedInstrumentIndex.store(selected);
    activeInstrumentInAudioThread = selected;
}

juce::var SoundEngine::getInstrumentLibrary() const
{
    juce::Array<juce::var> instruments, categories;
    for (const auto& category : toneCard.categories) categories.add(category);
    for (const auto& instrument : toneCard.instruments)
    {
        auto* item = new juce::DynamicObject();
        item->setProperty("name", instrument.name);
        item->setProperty("category", instrument.category);
        item->setProperty("generated", !instrument.uploaded && instrument.processedFile.isNotEmpty());
        item->setProperty("uploaded", instrument.uploaded);
        item->setProperty("draft", instrument.importDraft);
        item->setProperty("editable", instrument.processedFile.isNotEmpty() || instrument.importDraft);
        instruments.add(juce::var(item));
    }
    auto* library = new juce::DynamicObject();
    library->setProperty("instruments", juce::var(instruments));
    library->setProperty("categories", juce::var(categories));
    library->setProperty("selectedIndex", selectedInstrumentIndex.load());
    return juce::var(library);
}

juce::StringArray SoundEngine::getInstrumentNames() const
{
    juce::StringArray names;
    for (const auto& inst : toneCard.instruments)
        names.add(inst.name);

    if (names.isEmpty())
        names.add("Pulse Lead");

    return names;
}

juce::var SoundEngine::getInstrumentEditorData(int instrumentIndex) const
{
    auto* result = new juce::DynamicObject();
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size())
    {
        result->setProperty("ok", false);
        return juce::var(result);
    }

    const auto& instrument = toneCard.instruments[(size_t)instrumentIndex];
    const auto& samples = !instrument.body.empty() ? instrument.body : instrument.tick;
    juce::Array<juce::var> waveformMin, waveformMax;
    constexpr int bins = 82;
    for (int bin = 0; bin < bins; ++bin)
    {
        const size_t first = samples.empty() ? 0 : (size_t)bin * samples.size() / bins;
        const size_t last = samples.empty() ? 0 : std::max(first + 1, (size_t)(bin + 1) * samples.size() / bins);
        float low = 0.0f, high = 0.0f;
        for (size_t i = first; i < std::min(last, samples.size()); ++i)
        {
            low = std::min(low, samples[i]);
            high = std::max(high, samples[i]);
        }
        waveformMin.add(low);
        waveformMax.add(high);
    }
    const float start = instrument.playbackStart != nullptr ? instrument.playbackStart->load() : 0.0f;
    const float end = instrument.playbackEnd != nullptr ? instrument.playbackEnd->load() : 1.0f;
    const float root = instrument.liveBaseNote != nullptr ? instrument.liveBaseNote->load() : instrument.baseNote;
    result->setProperty("ok", true);
    result->setProperty("waveformMin", juce::var(waveformMin));
    result->setProperty("waveformMax", juce::var(waveformMax));
    result->setProperty("start", start);
    result->setProperty("end", end);
    result->setProperty("rootNote", instrument.rootMidiNote);
    result->setProperty("rootCents", instrument.rootCents);
    result->setProperty("pitched", instrument.pitchDetected);
    result->setProperty("useOriginal", instrument.useOriginalSource != nullptr
        && instrument.useOriginalSource->load(std::memory_order_relaxed));
    result->setProperty("confidence", instrument.pitchConfidence);
    result->setProperty("loop", instrument.selectedLoop != nullptr ? instrument.selectedLoop->load() : 0);
    juce::Array<juce::var> loops;
    const double denominator = std::max<size_t>(1, samples.size());
    for (const auto& region : instrument.loopRegions)
    {
        auto* loop = new juce::DynamicObject();
        loop->setProperty("start", region.start / denominator);
        loop->setProperty("end", region.end / denominator);
        loop->setProperty("full", region.full);
        loops.add(juce::var(loop));
    }
    result->setProperty("loops", juce::var(loops));
    return juce::var(result);
}

juce::var SoundEngine::setInstrumentEditorControls(int instrumentIndex, float start, float end, int rootMidiNote, float rootCents, int pitched)
{
    auto* result = new juce::DynamicObject();
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size())
    {
        result->setProperty("ok", false);
        return juce::var(result);
    }
    auto& instrument = toneCard.instruments[(size_t)instrumentIndex];
    const float safeStart = std::clamp(start, 0.0f, 0.98f);
    const float safeEnd = std::clamp(end, safeStart + 0.02f, 1.0f);
    if (instrument.playbackStart == nullptr) instrument.playbackStart = std::make_shared<std::atomic<float>>(safeStart);
    if (instrument.playbackEnd == nullptr) instrument.playbackEnd = std::make_shared<std::atomic<float>>(safeEnd);
    if (instrument.liveBaseNote == nullptr) instrument.liveBaseNote = std::make_shared<std::atomic<float>>(instrument.baseNote);
    instrument.playbackStart->store(safeStart, std::memory_order_relaxed);
    instrument.playbackEnd->store(safeEnd, std::memory_order_relaxed);
    instrument.rootMidiNote = std::clamp(rootMidiNote, 0, 127);
    if (std::isfinite(rootCents)) instrument.rootCents = std::clamp(rootCents, -100.0f, 100.0f);
    if (pitched >= 0) instrument.pitchDetected = pitched != 0;
    instrument.liveBaseNote->store(instrument.rootMidiNote + (instrument.pitchDetected ? instrument.rootCents / 100.0f : 0.0f), std::memory_order_relaxed);
    for (auto& voice : voices)
        if (voice.usesInstrument(&instrument)) voice.refreshPitch();
    result->setProperty("ok", true);
    result->setProperty("start", safeStart);
    result->setProperty("end", safeEnd);
    result->setProperty("rootNote", instrument.rootMidiNote);
    return juce::var(result);
}

juce::var SoundEngine::setInstrumentLoop(int instrumentIndex, int selection)
{
    auto* result = new juce::DynamicObject();
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size())
    {
        result->setProperty("ok", false); return juce::var(result);
    }
    auto& instrument = toneCard.instruments[(size_t)instrumentIndex];
    const int safe = std::clamp(selection, 0, (int)instrument.loopRegions.size());
    if (instrument.selectedLoop == nullptr)
        instrument.selectedLoop = std::make_shared<std::atomic<int>>(safe);
    instrument.selectedLoop->store(safe, std::memory_order_relaxed);
    for (auto& voice : voices) if (voice.usesInstrument(&instrument)) voice.stop();
    clearStealTails();
    result->setProperty("ok", true);
    result->setProperty("loop", safe);
    return juce::var(result);
}

juce::String SoundEngine::getInstrumentSettingsPath(int instrumentIndex) const
{
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size()) return {};
    const auto& source = toneCard.instruments[(size_t)instrumentIndex].sourceFile;
    if (source.isEmpty()) return {};
    const juce::File file(source);
    return file.getSiblingFile(file.getFileName() + ".2g.json").getFullPathName();
}

juce::String SoundEngine::getInstrumentSettingsJson(int instrumentIndex) const
{
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size()
        || instrumentIndex >= (int)editableParams.size()) return {};
    const auto& instrument = toneCard.instruments[(size_t)instrumentIndex];
    const auto& params = editableParams[(size_t)instrumentIndex];
    auto* object = new juce::DynamicObject();
    object->setProperty("version", 2);
    object->setProperty("name", instrument.name);
    object->setProperty("brightness", params.brightness);
    object->setProperty("attack", params.attack);
    object->setProperty("release", params.release);
    object->setProperty("noise", params.noise);
    object->setProperty("quality", params.quality);
    object->setProperty("trimStart", instrument.playbackStart != nullptr ? instrument.playbackStart->load() : 0.0f);
    object->setProperty("trimEnd", instrument.playbackEnd != nullptr ? instrument.playbackEnd->load() : 1.0f);
    object->setProperty("rootNote", instrument.rootMidiNote);
    object->setProperty("loop", instrument.selectedLoop != nullptr ? instrument.selectedLoop->load() : 0);
    {
        object->setProperty("pitched", instrument.pitchDetected);
        object->setProperty("rootCents", instrument.rootCents);
        object->setProperty("useOriginal", instrument.useOriginalSource != nullptr
            && instrument.useOriginalSource->load(std::memory_order_relaxed));
    }
    return juce::JSON::toString(juce::var(object), true);
}

juce::var SoundEngine::getSessionState() const
{
    auto* state = new juce::DynamicObject();
    if (!toneCard.instruments.empty()) {
        const auto& selected = toneCard.instruments[(size_t)std::clamp(selectedInstrumentIndex.load(), 0, (int)toneCard.instruments.size()-1)];
        auto* identity = new juce::DynamicObject();
        identity->setProperty("source", selected.sourceFile);
        identity->setProperty("name", selected.name); identity->setProperty("category", selected.category);
        state->setProperty("selectedSound", juce::var(identity));
    }
    state->setProperty("speaker", speakerSimEnabled.load(std::memory_order_relaxed));
    state->setProperty("masterVolume", getMasterVolume());
    state->setProperty("movement", movementEnabled.load(std::memory_order_relaxed));
    juce::Array<juce::var> entries;
    for (size_t i = 0; i < toneCard.instruments.size() && i < editableParams.size(); ++i)
    {
        const auto& instrument = toneCard.instruments[i]; const auto& p = editableParams[i];
        auto* entry = new juce::DynamicObject();
        entry->setProperty("source", instrument.sourceFile);
        entry->setProperty("name", instrument.name); entry->setProperty("category", instrument.category);
        entry->setProperty("brightness", p.brightness); entry->setProperty("attack", p.attack);
        entry->setProperty("release", p.release); entry->setProperty("detune", p.detune);
        entry->setProperty("noise", p.noise); entry->setProperty("quality", p.quality); entry->setProperty("formant", p.formant);
        entry->setProperty("trimStart", instrument.playbackStart != nullptr ? instrument.playbackStart->load() : 0.0f);
        entry->setProperty("trimEnd", instrument.playbackEnd != nullptr ? instrument.playbackEnd->load() : 1.0f);
        entry->setProperty("rootNote", instrument.rootMidiNote);
        entry->setProperty("rootCents", instrument.rootCents);
        entry->setProperty("loop", instrument.selectedLoop != nullptr ? instrument.selectedLoop->load() : 0);
        entry->setProperty("pitched", instrument.pitchDetected);
        entry->setProperty("useOriginal", instrument.useOriginalSource != nullptr && instrument.useOriginalSource->load());
        entries.add(juce::var(entry));
    }
    state->setProperty("instruments", juce::var(entries));
    return juce::var(state);
}

void SoundEngine::restoreSessionState(const juce::var& state)
{
    if (!state.isObject()) return;
    setMasterVolume(state.hasProperty("masterVolume") ? (float) state["masterVolume"] : 1.0f);
    if (state.hasProperty("speaker")) speakerSimEnabled.store((bool) state["speaker"], std::memory_order_relaxed);
    if (state.hasProperty("movement")) movementEnabled.store((bool) state["movement"], std::memory_order_relaxed);
    const auto* entries = state["instruments"].getArray(); if (entries == nullptr) return;
    for (const auto& entry : *entries)
    {
        const auto source = entry["source"].toString(); const auto name = entry["name"].toString(); const auto category = entry["category"].toString();
        int target = -1;
        for (int i = 0; i < (int) toneCard.instruments.size(); ++i)
        {
            const auto& candidate = toneCard.instruments[(size_t) i];
            if ((source.isNotEmpty() && candidate.sourceFile == source) || (source.isEmpty() && candidate.name == name && candidate.category == category)) { target = i; break; }
        }
        if (target < 0 || target >= (int) editableParams.size()) continue;
        auto& instrument = toneCard.instruments[(size_t) target]; auto& p = editableParams[(size_t) target];
        const auto get = [&entry](const char* key, float fallback) { return entry.hasProperty(key) ? (float) entry[key] : fallback; };
        p.brightness=get("brightness",p.brightness); p.attack=get("attack",p.attack); p.release=get("release",p.release);
        p.detune=get("detune",p.detune); p.noise=get("noise",p.noise); p.quality=get("quality",p.quality); p.formant=get("formant",p.formant); p.clampAll();
        instrument.defaultParams=p;
        if (instrument.playbackStart != nullptr) instrument.playbackStart->store(std::clamp(get("trimStart",instrument.playbackStart->load()),0.0f,0.98f));
        if (instrument.playbackEnd != nullptr) instrument.playbackEnd->store(std::clamp(get("trimEnd",instrument.playbackEnd->load()),0.02f,1.0f));
        instrument.rootMidiNote=std::clamp(entry.hasProperty("rootNote")?(int)entry["rootNote"]:instrument.rootMidiNote,0,127);
        if (instrument.selectedLoop != nullptr) instrument.selectedLoop->store(std::clamp(entry.hasProperty("loop")?(int)entry["loop"]:0,0,(int)instrument.loopRegions.size()));
        {
            if (entry.hasProperty("rootCents")) instrument.rootCents = std::clamp((float)entry["rootCents"], -100.0f, 100.0f);
            if (entry.hasProperty("pitched")) instrument.pitchDetected=(bool)entry["pitched"];
            if (instrument.useOriginalSource != nullptr && entry.hasProperty("useOriginal")) instrument.useOriginalSource->store((bool)entry["useOriginal"]);
        }
        if (instrument.liveBaseNote != nullptr) instrument.liveBaseNote->store(instrument.rootMidiNote+(instrument.pitchDetected?instrument.rootCents/100.0f:0.0f));
    }
    for (auto& voice : voices) voice.stop();
    clearStealTails();
    const auto selected = state["selectedSound"];
    if (selected.isObject()) {
        const auto source = selected["source"].toString();
        for (int i=0;i<(int)toneCard.instruments.size();++i) {
            const auto& candidate = toneCard.instruments[(size_t)i];
            if ((source.isNotEmpty() && candidate.sourceFile == source)
                || (source.isEmpty() && candidate.name == selected["name"].toString() && candidate.category == selected["category"].toString())) {
                selectVoice(i); break;
            }
        }
    }
}

juce::var SoundEngine::installGeneratedSample(PreparedGeneratedSample prepared)
{
    if (prepared.processed.size() < 64 || prepared.error.isNotEmpty())
    {
        auto* error = new juce::DynamicObject();
        error->setProperty("ok", false);
        error->setProperty("error", prepared.error.isNotEmpty() ? prepared.error : "Decode failed");
        return juce::var(error);
    }

    for (auto& voice : voices) voice.stop();
    clearStealTails();
    {
        const juce::SpinLock::ScopedLockType guard(producerLock);
        readIndex.store(writeIndex.load(std::memory_order_relaxed), std::memory_order_release);
    }

    Instrument generated;
    const auto cleanPrompt = prepared.prompt.trim().replaceCharacters("\r\n\t", "   ");
    generated.name = cleanPrompt.isEmpty() ? "AI Sample" : cleanPrompt.substring(0, 22);
    generated.category = prepared.uploaded ? "Uploaded" : "Generated";
    generated.uploaded = prepared.uploaded;
    generated.importDraft = prepared.importDraft;
    generated.body = std::move(prepared.processed);
    generated.originalBody = std::move(prepared.original);
    generated.pitchDetected = prepared.pitched;
    generated.rootMidiNote = prepared.rootMidiNote;
    generated.rootCents = prepared.rootCents;
    generated.pitchConfidence = prepared.pitchConfidence;
    generated.baseNote = prepared.rootMidiNote + (prepared.pitched ? prepared.rootCents / 100.0f : 0.0f);
    generated.liveBaseNote = std::make_shared<std::atomic<float>>(generated.baseNote);
    generated.useOriginalSource = std::make_shared<std::atomic<bool>>(false);
    generated.playbackStart = std::make_shared<std::atomic<float>>(0.0f);
    generated.playbackEnd = std::make_shared<std::atomic<float>>(1.0f);
    generated.sourcePrompt = prepared.prompt;
    generated.generatedAt = prepared.generatedAt;
    generated.processedFile = prepared.processedFile;
    generated.originalFile = prepared.originalFile;
    generated.sourceFile = prepared.processedFile;
    generated.outputGain = prepared.outputGain > 0.0f ? prepared.outputGain : 1.0f;
    generated.originalOffset = prepared.originalOffset;
    generated.oneShotBody = prepared.oneShot;
    generated.playbackMode = prepared.oneShot ? PlaybackMode::OneShot : PlaybackMode::ForceLoop;
    generated.loopRegions = prepared.loopRegions;
    // A loop that passed the strict checks is on from the start; one that is only good enough stays offered but Off.
    generated.selectedLoop = std::make_shared<std::atomic<int>>(
        prepared.preferFull && !prepared.loopRegions.empty() && prepared.loopRegions.back().full ? (int)prepared.loopRegions.size()
        : prepared.oneShot || prepared.loopRegions.empty() || !prepared.loopRegions.front().confident ? 0 : 1);
    generated.loopStart = prepared.loopStart;
    generated.loopEnd = prepared.oneShot ? 0 : prepared.loopEnd;
    generated.defaultParams = { 10, 1, 3, 5, 1, 10, 5 };

    const int instrumentIndex = (int)toneCard.instruments.size();
    toneCard.instruments.push_back(std::move(generated));
    editableParams.push_back(toneCard.instruments.back().defaultParams);
    if (!prepared.importDraft) toneCard.categories.addIfNotAlreadyThere(prepared.uploaded ? "Uploaded" : "Generated");

    selectedInstrumentIndex.store(instrumentIndex, std::memory_order_release);
    activeInstrumentInAudioThread = instrumentIndex;

    const auto& body = toneCard.instruments[(size_t)instrumentIndex].body;
    juce::Array<juce::var> waveformMin, waveformMax;
    for (const auto value : prepared.waveformMin) waveformMin.add(value);
    for (const auto value : prepared.waveformMax) waveformMax.add(value);

    auto* result = new juce::DynamicObject();
    result->setProperty("ok", true);
    result->setProperty("index", instrumentIndex);
    result->setProperty("name", toneCard.instruments[(size_t)instrumentIndex].name);
    result->setProperty("duration", (double)body.size() / kEngineSampleRate);
    result->setProperty("waveformMin", juce::var(waveformMin));
    result->setProperty("waveformMax", juce::var(waveformMax));
    result->setProperty("pitched", prepared.pitched);
    result->setProperty("rootNote", prepared.rootMidiNote);
    result->setProperty("cents", prepared.rootCents);
    result->setProperty("confidence", prepared.pitchConfidence);
    result->setProperty("useOriginal", false);
    result->setProperty("processedFile", prepared.processedFile);
    result->setProperty("originalFile", prepared.originalFile);
    return juce::var(result);
}

juce::var SoundEngine::commitImportedSample(int index, const juce::String& processed, const juce::String& original)
{
    auto* result = new juce::DynamicObject();
    if (index < 0 || index >= (int)toneCard.instruments.size() || !toneCard.instruments[(size_t)index].importDraft)
    { result->setProperty("ok", false); return juce::var(result); }
    auto& instrument = toneCard.instruments[(size_t)index];
    instrument.sourceFile = instrument.processedFile = processed;
    instrument.originalFile = original; instrument.importDraft = false;
    toneCard.categories.addIfNotAlreadyThere(instrument.uploaded ? "Uploaded" : "Generated");
    result->setProperty("ok", true); result->setProperty("index", index);
    return juce::var(result);
}

juce::var SoundEngine::setGeneratedSampleControls(int instrumentIndex, int rootMidiNote,
                                                  bool pitched, bool useOriginal)
{
    auto* result = new juce::DynamicObject();
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size())
    {
        result->setProperty("ok", false); return juce::var(result);
    }
    auto& instrument = toneCard.instruments[(size_t)instrumentIndex];
    if ((instrument.processedFile.isEmpty() && !instrument.importDraft) || instrument.liveBaseNote == nullptr || instrument.useOriginalSource == nullptr)
    {
        result->setProperty("ok", false); return juce::var(result);
    }
    instrument.rootMidiNote = std::clamp(rootMidiNote, 0, 127);
    instrument.pitchDetected = pitched;
    instrument.liveBaseNote->store(instrument.rootMidiNote + (pitched ? instrument.rootCents / 100.0f : 0.0f),
                                   std::memory_order_relaxed);
    instrument.useOriginalSource->store(useOriginal, std::memory_order_relaxed);
    result->setProperty("ok", true);
    result->setProperty("rootNote", instrument.rootMidiNote);
    result->setProperty("cents", instrument.rootCents);
    result->setProperty("pitched", instrument.pitchDetected);
    result->setProperty("useOriginal", useOriginal);
    return juce::var(result);
}

PreparedGeneratedSample SoundEngine::copyGeneratedSampleForSaving(int instrumentIndex) const
{
    PreparedGeneratedSample copy;
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size()) return copy;
    const auto& instrument = toneCard.instruments[(size_t)instrumentIndex];
    copy.original = instrument.originalBody;
    copy.processed = instrument.body;
    copy.pitched = instrument.pitchDetected;
    copy.rootMidiNote = instrument.rootMidiNote;
    copy.rootCents = instrument.rootCents;
    copy.pitchConfidence = instrument.pitchConfidence;
    copy.trimStart = instrument.playbackStart != nullptr ? instrument.playbackStart->load() : 0.0f;
    copy.trimEnd = instrument.playbackEnd != nullptr ? instrument.playbackEnd->load() : 1.0f;
    copy.oneShot = instrument.selectedLoop == nullptr || instrument.selectedLoop->load() == 0;
    copy.loopStart = instrument.loopStart;
    copy.loopEnd = instrument.loopEnd;
    copy.loopRegions = instrument.loopRegions;
    copy.prompt = instrument.sourcePrompt;
    copy.generatedAt = instrument.generatedAt;
    copy.processedFile = instrument.processedFile;
    copy.originalFile = instrument.originalFile;
    copy.uploaded = instrument.uploaded;
    return copy;
}

juce::var SoundEngine::renameInstrument(int instrumentIndex, const juce::String& newName)
{
    auto* result = new juce::DynamicObject();
    result->setProperty("ok", false);
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size())
        return juce::var(result);
    auto& instrument = toneCard.instruments[(size_t)instrumentIndex];
    if (instrument.processedFile.isEmpty() && !instrument.importDraft)
    {
        result->setProperty("error", "This sound cannot be renamed");
        return juce::var(result);
    }
    // One line, no control characters, single spaces, at most 28 characters (what the LCD lists hold).
    juce::String clean;
    bool lastSpace = true;
    for (const auto character : newName)
    {
        if (character < 32 || character == 127) continue;
        const bool space = character == ' ';
        if (space && lastSpace) continue;
        clean += character;
        lastSpace = space;
    }
    clean = clean.trim().substring(0, 28).trim();
    if (clean.isEmpty())
    {
        result->setProperty("error", "Name is empty");
        return juce::var(result);
    }
    instrument.name = clean;
    result->setProperty("ok", true);
    result->setProperty("name", clean);
    return juce::var(result);
}

juce::var SoundEngine::deleteGeneratedSample(int instrumentIndex)
{
    auto* result = new juce::DynamicObject();
    if (instrumentIndex < 0 || instrumentIndex >= (int)toneCard.instruments.size()
        || (toneCard.instruments[(size_t)instrumentIndex].processedFile.isEmpty() && !toneCard.instruments[(size_t)instrumentIndex].importDraft))
    { result->setProperty("ok", false); return juce::var(result); }

    for (auto& voice : voices) voice.stop();
    clearStealTails();
    const auto processed = toneCard.instruments[(size_t)instrumentIndex].processedFile;
    const auto original = toneCard.instruments[(size_t)instrumentIndex].originalFile;
    toneCard.instruments.erase(toneCard.instruments.begin() + instrumentIndex);
    editableParams.erase(editableParams.begin() + instrumentIndex);
    const int next = std::clamp(instrumentIndex, 0, std::max(0, (int)toneCard.instruments.size() - 1));
    selectedInstrumentIndex.store(next, std::memory_order_release);
    activeInstrumentInAudioThread = next;
    result->setProperty("ok", true);
    result->setProperty("index", next);
    result->setProperty("processedFile", processed);
    result->setProperty("originalFile", original);
    return juce::var(result);
}

void SoundEngine::pushEvent(const AudioEvent& ev)
{
    const juce::SpinLock::ScopedLockType guard(producerLock);
    uint32_t currentWrite = writeIndex.load(std::memory_order_relaxed);
    uint32_t nextWrite = (currentWrite + 1) & kQueueMask;

    if (nextWrite != readIndex.load(std::memory_order_acquire))
    {
        eventQueue[currentWrite] = ev;
        writeIndex.store(nextWrite, std::memory_order_release);
    }
}

void SoundEngine::noteOn(int midiNote, float velocity)
{
    AudioEvent ev;
    ev.type = AudioEvent::Type::NoteOn;
    ev.note = midiNote;
    ev.velocity = velocityTouch.load(std::memory_order_relaxed) ? velocity : 0.9f;
    pushEvent(ev);
    displayNoteOnSerial.fetch_add(1, std::memory_order_relaxed);
}

void SoundEngine::noteOff(int midiNote)
{
    AudioEvent ev;
    ev.type = AudioEvent::Type::NoteOff;
    ev.note = midiNote;
    pushEvent(ev);
}

void SoundEngine::allNotesOff()
{
    AudioEvent ev;
    ev.type = AudioEvent::Type::AllNotesOff;
    pushEvent(ev);
}

void SoundEngine::selectVoice(int instrumentIndex)
{
    selectedInstrumentIndex.store(instrumentIndex);

    AudioEvent ev;
    ev.type = AudioEvent::Type::SelectVoice;
    ev.instrumentIndex = instrumentIndex;
    pushEvent(ev);
}

void SoundEngine::previewVoice(int instrumentIndex)
{
    selectedInstrumentIndex.store(instrumentIndex);

    AudioEvent ev;
    ev.type = AudioEvent::Type::PreviewVoice;
    ev.instrumentIndex = instrumentIndex;
    pushEvent(ev);
}

VoiceParams SoundEngine::getVoiceParams(int instrumentIndex) const
{
    if (editableParams.empty())
        return {};
    const int safeIndex = std::clamp(instrumentIndex, 0, (int)editableParams.size() - 1);
    return editableParams[(size_t)safeIndex];
}

void SoundEngine::setVoiceParam(int instrumentIndex, int paramIndex, float value)
{
    if (editableParams.empty())
        return;
    const int safeInstrument = std::clamp(instrumentIndex, 0, (int)editableParams.size() - 1);
    const int safeParam = std::clamp(paramIndex, 0, 6);
    const float maximum = 10.0f;
    const float safeValue = std::clamp(value, 1.0f, maximum);
    auto& p = editableParams[(size_t)safeInstrument];
    float* values[] = { &p.brightness, &p.attack, &p.release, &p.detune, &p.noise, &p.quality, &p.formant };
    *values[safeParam] = safeValue;

    AudioEvent ev;
    ev.type = AudioEvent::Type::SetParam;
    ev.instrumentIndex = safeInstrument;
    ev.paramIndex = safeParam;
    ev.paramValue = safeValue;
    pushEvent(ev);
}

void SoundEngine::processEventInAudioThread(const AudioEvent& ev)
{
    if (toneCard.instruments.empty())
        return;

    const int numInstruments = (int)toneCard.instruments.size();

    const auto findQuietestVoice = [this]()
    {
        int quietest = 0;
        for (int i = 1; i < (int)voices.size(); ++i)
        {
            const float candidate = voices[(size_t)i].getAudibility();
            const float current = voices[(size_t)quietest].getAudibility();
            if (candidate < current - 1.0e-6f
                || (std::abs(candidate - current) <= 1.0e-6f
                    && voices[(size_t)i].getAge() > voices[(size_t)quietest].getAge()))
                quietest = i;
        }
        return quietest;
    };

    switch (ev.type)
    {
        case AudioEvent::Type::SetParam:
        {
            const int instrument = std::clamp(ev.instrumentIndex, 0, numInstruments - 1);
            auto& p = toneCard.instruments[(size_t)instrument].defaultParams;
            float* values[] = { &p.brightness, &p.attack, &p.release, &p.detune, &p.noise, &p.quality, &p.formant };
            const int parameter = std::clamp(ev.paramIndex, 0, 6);
            *values[parameter] = std::clamp(ev.paramValue, 1.0f, 10.0f);
            const auto* changedInstrument = &toneCard.instruments[(size_t)instrument];
            for (auto& voice : voices)
                if (voice.isActive() && voice.usesInstrument(changedInstrument))
                    voice.applyParamsLive(p);
            break;
        }
        case AudioEvent::Type::SelectVoice:
        {
            activeInstrumentInAudioThread = std::clamp(ev.instrumentIndex, 0, numInstruments - 1);
            break;
        }

        case AudioEvent::Type::PreviewVoice:
        {
            activeInstrumentInAudioThread = std::clamp(ev.instrumentIndex, 0, numInstruments - 1);
            const auto* inst = &toneCard.instruments[(size_t)activeInstrumentInAudioThread];

            const int previousPreview = previewVoiceIndex;
            if (previousPreview >= 0 && voices[(size_t)previousPreview].isActive())
                voices[(size_t)previousPreview].fadeOut(0.045f);

            // Re-use or find voice for preview note (Middle C = 60)
            int targetIdx = -1;
            if (targetIdx < 0)
            {
                for (int i = 0; i < 4; ++i)
                {
                    if (i != previousPreview && !voices[i].isActive())
                    {
                        targetIdx = i;
                        break;
                    }
                }
            }
            if (targetIdx < 0)
            {
                targetIdx = findQuietestVoice();
            }

            startVoiceInSlot(targetIdx, 60, 0.9f, inst, inst->defaultParams);
            voices[targetIdx].setMenuPreview(true);
            previewVoiceIndex = targetIdx;
            previewCountdown = (int)(kEngineSampleRate * 0.70);
            break;
        }

        case AudioEvent::Type::NoteOn:
        {
            if(ev.note>=0&&ev.note<128&&!heldDisplayNotes[(size_t)ev.note]){
                heldDisplayNotes[(size_t)ev.note]=true;++heldDisplayCount;
                displayGate.store(true,std::memory_order_relaxed);
            }
            const auto* inst = &toneCard.instruments[(size_t)activeInstrumentInAudioThread];

            const int fadingPreview = previewVoiceIndex;
            for (auto& voice : voices)
                if (voice.isActive() && voice.isMenuPreview())
                    voice.fadeOut(0.08f);
            previewCountdown = 0;
            previewVoiceIndex = -1;

            // 1. Check if same note is already playing (retrigger own voice)
            int targetIdx = -1;
            for (int i = 0; i < 4; ++i)
            {
                if (!voices[i].isMenuPreview() && voices[i].isActive() && voices[i].getMidiNote() == ev.note)
                {
                    targetIdx = i;
                    break;
                }
            }

            // 2. Check for free / idle voice
            if (targetIdx < 0)
            {
                for (int i = 0; i < 4; ++i)
                {
                    if (i != fadingPreview && !voices[i].isActive())
                    {
                        targetIdx = i;
                        break;
                    }
                }
            }

            // 3. 4-voice limit full: replace the least audible voice. The
            // level follower avoids choosing a loud note at a zero crossing.
            if (targetIdx < 0)
            {
                targetIdx = findQuietestVoice();
            }

            startVoiceInSlot(targetIdx, ev.note, ev.velocity, inst, inst->defaultParams);
            break;
        }

        case AudioEvent::Type::NoteOff:
        {
            if(ev.note>=0&&ev.note<128&&heldDisplayNotes[(size_t)ev.note]){
                heldDisplayNotes[(size_t)ev.note]=false;--heldDisplayCount;
                displayGate.store(heldDisplayCount>0,std::memory_order_relaxed);
            }
            for (int i = 0; i < 4; ++i)
            {
                if (voices[i].isActive() && voices[i].getMidiNote() == ev.note)
                {
                    voices[i].noteOff(ev.note);
                }
            }
            break;
        }

        case AudioEvent::Type::AllNotesOff:
        {
            heldDisplayNotes.fill(false);heldDisplayCount=0;displayGate.store(false,std::memory_order_relaxed);
            for (int i = 0; i < 4; ++i)
            {
                voices[i].stop();
            }
            previewCountdown = 0;
            previewVoiceIndex = -1;
            clearStealTails();
            break;
        }
    }
}


void SoundEngine::audioDeviceIOCallbackWithContext(const float* const* /*inputChannelData*/,
                                                 int /*numInputChannels*/,
                                                 float* const* outputChannelData,
                                                 int numOutputChannels,
                                                 int numSamples,
                                                 const juce::AudioIODeviceCallbackContext& /*context*/)
{
    renderAudioBlock(outputChannelData, numOutputChannels, numSamples);
}

void SoundEngine::renderAudioBlock(float* const* outputChannelData,
                                   int numOutputChannels,
                                   int numSamples)
{
    // Real-time audio thread: No memory allocation, no mutex locks, no disk I/O, no logging!
    juce::ScopedNoDenormals noDenormals;

    // 1. Drain pending events
    uint32_t currentRead = readIndex.load(std::memory_order_relaxed);
    while (currentRead != writeIndex.load(std::memory_order_acquire))
    {
        processEventInAudioThread(eventQueue[currentRead]);
        currentRead = (currentRead + 1) & kQueueMask;
    }
    readIndex.store(currentRead, std::memory_order_release);

    if (numOutputChannels <= 0 || outputChannelData == nullptr)
        return;

    const double sampleRateRatio = kEngineSampleRate / std::max(kEngineSampleRate * 0.5, hostSampleRate);
    masterGain.setTargetValue(getMasterVolume());

    for (int i = 0; i < numSamples; ++i)
    {
        sourcePhase += sampleRateRatio;
        while (sourcePhase >= 1.0)
        {
            sourcePhase -= 1.0;
            prevEngineSample = currEngineSample;
            currEngineSample = renderEngineSample();
        }

        // Fractional linear interpolation between consecutive engine samples
        float frac = (float)sourcePhase;
        float outSample = (1.0f - frac) * prevEngineSample + frac * currEngineSample;
        outSample *= masterGain.getNextValue();

        // Copy mono output to all active channels (e.g. Left & Right stereo)
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            if (float* chData = outputChannelData[ch])
                chData[i] = outSample;
        }
    }
}

void SoundEngine::handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        noteOn(message.getNoteNumber(),message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        noteOff(message.getNoteNumber());
    }
    else if (message.isAllNotesOff())
    {
        allNotesOff();
    }
    else if (message.isPitchWheel())
    {
        setPitchWheel(message.getPitchWheelValue());
    }
}
