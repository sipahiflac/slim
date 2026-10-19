#include "ToneCard.h"
#include "SoundStore.h"
#include "PitchAnalysis.h"
#include "LoopFinder.h"
#include "ParallelFor.h"
#include <optional>
#include "EngineConstants.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <numeric>

static std::vector<float> resampleReaderToEngineRate(juce::AudioFormatReader* reader)
{
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};

    const int numChannels = (int)reader->numChannels;
    const int numSrcSamples = (int)reader->lengthInSamples;
    const double srcRate = reader->sampleRate;

    juce::AudioBuffer<float> tempBuffer(numChannels, numSrcSamples);
    reader->read(&tempBuffer, 0, numSrcSamples, 0, true, true);

    // Mixdown to mono
    std::vector<float> monoSrc(numSrcSamples, 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* chData = tempBuffer.getReadPointer(ch);
        for (int i = 0; i < numSrcSamples; ++i)
            // A float WAV may hold NaN/inf; one such sample would otherwise poison every
            // downstream filter state for as long as the engine runs.
            monoSrc[i] += std::isfinite(chData[i]) ? chData[i] : 0.0f;
    }
    const float chScale = (numChannels > 0) ? (1.0f / (float)numChannels) : 1.0f;
    for (int i = 0; i < numSrcSamples; ++i)
        monoSrc[i] *= chScale;

    if (srcRate > kEngineSampleRate)
    {
        constexpr int taps = 63;
        constexpr double pi = 3.14159265358979323846;
        double kernel[taps], sum = 0.0;
        // Anti-alias cutoff at ~7.5 kHz (93.75% of the 16 kHz engine's Nyquist).
        const double fc = (0.9375 * kEngineSampleRate * 0.5) / srcRate;
        for (int k = 0; k < taps; ++k)
        {
            const int n = k - taps / 2;
            const double sinc = n == 0 ? 2.0 * fc : std::sin(2.0 * pi * fc * n) / (pi * n);
            kernel[k] = sinc * (0.54 - 0.46 * std::cos(2.0 * pi * k / (taps - 1)));
            sum += kernel[k];
        }
        std::vector<float> filtered(monoSrc.size());
        for (int i = 0; i < numSrcSamples; ++i)
        {
            double value = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                const int index = i + k - taps / 2;
                if (index >= 0 && index < numSrcSamples)
                    value += monoSrc[(size_t)index] * kernel[k] / sum;
            }
            filtered[(size_t)i] = (float)value;
        }
        monoSrc = std::move(filtered);
    }

    // Resample to the engine's working rate
    const double ratio = srcRate / kEngineSampleRate;
    const int numDstSamples = (int)std::max(1.0, std::floor((double)numSrcSamples / ratio));

    std::vector<float> dst(numDstSamples, 0.0f);
    double srcPos = 0.0;
    for (int i = 0; i < numDstSamples; ++i)
    {
        int i0 = (int)srcPos;
        int i1 = std::min(i0 + 1, numSrcSamples - 1);
        float frac = (float)(srcPos - (double)i0);
        dst[i] = (1.0f - frac) * monoSrc[i0] + frac * monoSrc[i1];
        srcPos += ratio;
    }

    return dst;
}

std::vector<float> ToneCard::decodeAudioMemory(const juce::MemoryBlock& audioData)
{
    if (audioData.getSize() == 0)
        return {};

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto stream = std::make_unique<juce::MemoryInputStream>(audioData, false);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(std::move(stream)));
    if (!reader || reader->sampleRate <= 0 || reader->lengthInSamples > reader->sampleRate * 120.0) return {};
    return resampleReaderToEngineRate(reader.get());
}

namespace
{
constexpr float kStartThreshold = 0.00316227766f; // -50 dBFS

// Where the processed copy of `audio` begins: the first sample above -50 dBFS once the DC offset is
// removed, less 5 ms of pre-roll. Returns audio.size() for silence. prepareGeneratedSample() trims
// with this, and a sound reloaded from disk re-runs it on its _original file to recover the offset.
size_t trimStart(const std::vector<float>& audio)
{
    if (audio.empty()) return 0;
    const double mean = std::accumulate(audio.begin(), audio.end(), 0.0) / (double)audio.size();
    size_t first = 0;
    while (first < audio.size() && std::abs(audio[first] - (float)mean) < kStartThreshold) ++first;
    if (first == audio.size()) return audio.size();
    const size_t preRoll = (size_t)std::lround(0.005 * kEngineSampleRate);
    return first > preRoll ? first - preRoll : 0;
}

void writeGeneratedWav(const juce::File& file, const std::vector<float>& samples,
                       const juce::String& prompt, const juce::Time& created,
                       float exactRootNote, bool processed)
{
    if (samples.empty()) return;
    file.getParentDirectory().createDirectory();
    auto metadata = juce::WavAudioFormat::createBWAVMetadata(
        "Prompt: " + prompt, "Scrap Brain Audio 2G", {}, created, 0,
        processed ? "2G preparation chain" : "Original decoded source");

    const int unityNote = std::clamp((int)std::floor(exactRootNote), 0, 127);
    const double fraction = std::clamp((double)exactRootNote - unityNote, 0.0, 0.999999999);
    metadata.set("MidiUnityNote", juce::String(unityNote));
    metadata.set("MidiPitchFraction", juce::String((juce::int64)std::llround(fraction * 4294967295.0)));
    metadata.set("NumSampleLoops", "0");
    metadata.set(juce::WavAudioFormat::riffInfoComment, "Prompt: " + prompt + " | Generated: " + created.toISO8601(true));
    metadata.set(juce::WavAudioFormat::riffInfoSoftware, "Scrap Brain Audio 2G");

    juce::WavAudioFormat format;
    auto output = file.createOutputStream();
    if (output == nullptr) return;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(output.release(), kEngineSampleRate, 1, 24, metadata, 0));
    if (writer == nullptr) return;
    juce::AudioBuffer<float> buffer(1, (int)samples.size());
    buffer.copyFrom(0, 0, samples.data(), (int)samples.size());
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}
}

PreparedGeneratedSample ToneCard::prepareGeneratedSample(const juce::MemoryBlock& audioData,
                                                         const juce::String& prompt,
                                                         bool forceUnpitched, bool saveFiles)
{
    PreparedGeneratedSample result;
    result.prompt = prompt.trim();
    const auto created = juce::Time::getCurrentTime();
    result.generatedAt = created.toISO8601(true);
    result.original = decodeAudioMemory(audioData);
    if (result.original.size() < 64) { result.error = "Decode failed"; return result; }

    auto cleaned = result.original;
    const double mean = std::accumulate(cleaned.begin(), cleaned.end(), 0.0) / (double)cleaned.size();
    for (auto& sample : cleaned) sample -= (float)mean;

    constexpr float tailThreshold = 0.001f;          // -60 dBFS
    const size_t begin = trimStart(result.original);
    if (begin >= cleaned.size()) { result.error = "Silent audio"; return result; }
    result.originalOffset = (int)begin;

    size_t last = cleaned.size() - 1;
    while (last > begin && std::abs(cleaned[last]) < tailThreshold) --last;
    const size_t end = std::min(cleaned.size(), last + 2);
    if (end <= begin + 63) { result.error = "Audio too short"; return result; }
    result.processed.assign(cleaned.begin() + (ptrdiff_t)begin, cleaned.begin() + (ptrdiff_t)end);

    const size_t fadeIn = std::min((size_t)std::lround(0.002 * kEngineSampleRate), result.processed.size() / 2);
    for (size_t i = 0; i < fadeIn; ++i)
        result.processed[i] *= (float)i / (float)std::max((size_t)1, fadeIn);

    const size_t fadeOut = std::min((size_t)std::lround(0.075 * kEngineSampleRate), result.processed.size() / 2);
    constexpr double floorGain = 0.0001;
    for (size_t i = 0; i < fadeOut; ++i)
    {
        const double t = (double)i / (double)std::max((size_t)1, fadeOut - 1);
        const float gain = (float)((std::pow(10.0, -4.0 * t) - floorGain) / (1.0 - floorGain));
        result.processed[result.processed.size() - fadeOut + i] *= std::max(0.0f, gain);
    }

    float peak = 0.0f;
    for (const auto sample : result.processed) peak = std::max(peak, std::abs(sample));
    if (peak < 1.0e-6f) { result.error = "Silent audio"; return result; }
    constexpr float targetPeak = 0.891250938f; // -1 dBFS
    const float normalise = targetPeak / peak;
    for (auto& sample : result.processed) sample *= normalise;

    // YIN is measured after the onset, over several frames. Confidence mixes
    const auto pitch = SamplePitch::analyse(result.processed, kEngineSampleRate);
    result.pitched = !forceUnpitched && pitch.pitched;
    // The sample keeps its own register: a bass is not declared a C2 and then played two octaves up from the middle
    // key. The root is folded into F#3..F#4 (same note name, nearest octave), so the middle of the keyboard plays
    // the sound at roughly its natural pitch, thick if it is thick.
    int root = pitch.note;
    if (result.pitched) { while (root < 54) root += 12; while (root > 66) root -= 12; }
    result.rootMidiNote = root;
    result.rootCents = pitch.cents;
    result.pitchConfidence = pitch.confidence;

    analyseGeneratedPlayback(result);

    constexpr int bins = 82;
    result.waveformMin.resize(bins); result.waveformMax.resize(bins);
    for (int bin = 0; bin < bins; ++bin)
    {
        const size_t from = (size_t)bin * result.processed.size() / bins;
        const size_t to = std::max(from + 1, (size_t)(bin + 1) * result.processed.size() / bins);
        float lo = 1.0f, hi = -1.0f;
        for (size_t i = from; i < std::min(to, result.processed.size()); ++i)
        { lo = std::min(lo, result.processed[i]); hi = std::max(hi, result.processed[i]); }
        result.waveformMin[(size_t)bin] = lo; result.waveformMax[(size_t)bin] = hi;
    }

    if (!saveFiles) return result;

    auto generatedDir = SoundStore::generatedFolder();
    auto safeName = result.prompt.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_").trim().replaceCharacter(' ', '_').substring(0, 28);
    if (safeName.isEmpty()) safeName = "AI_sample";
    const auto stem = created.formatted("%Y%m%d-%H%M%S_") + safeName;
    const auto processedFile = generatedDir.getNonexistentChildFile(stem, SoundStore::extensionFor(false), false);
    const auto originalFile = processedFile.getSiblingFile(processedFile.getFileNameWithoutExtension() + "_original" + SoundStore::extensionFor(false));
    result.processedFile = processedFile.getFullPathName();
    result.originalFile = originalFile.getFullPathName();
    const float exactRoot = result.pitched ? result.rootMidiNote + result.rootCents / 100.0f : 60.0f;
    saveGeneratedSampleFiles(result, exactRoot);
    return result;
}

void ToneCard::saveGeneratedSampleFiles(const PreparedGeneratedSample& sample, float exactRootNote)
{
    auto created = juce::Time::fromISO8601(sample.generatedAt);
    if (created.toMilliseconds() <= 0) created = juce::Time::getCurrentTime();
    if (sample.processedFile.isNotEmpty())
        writeGeneratedWav(juce::File(sample.processedFile), sample.processed, sample.prompt, created, exactRootNote, true);
    if (sample.originalFile.isNotEmpty())
        writeGeneratedWav(juce::File(sample.originalFile), sample.original, sample.prompt, created, exactRootNote, false);
}

void ToneCard::analyseGeneratedPlayback(PreparedGeneratedSample& sample)
{
    sample.oneShot = true;
    sample.loopStart = 0;
    sample.loopEnd = 0;
    if (sample.processed.empty()) return;

    sample.loopRegions = LoopFinder::findLoops(sample.processed);
    if (!sample.loopRegions.empty())
    {
        sample.oneShot = false;
        sample.loopStart = sample.loopRegions.front().start;
        sample.loopEnd = sample.loopRegions.front().end;
    }

    // Analysis never writes into or truncates the waveform. Peak calibration
    // remains a whole-file operation and loop seams are rendered at playback.
    float peak = 0.0f;
    for (const auto value : sample.processed) peak = std::max(peak, std::abs(value));
    if (peak > 1.0e-6f)
    {
        constexpr float targetPeak = 0.891250938f;
        const float gain = targetPeak / peak;
        for (auto& value : sample.processed) value *= gain;
    }
}

void ToneCard::appendGeneratedSamples(ToneCard& card, const juce::File& folder, bool uploaded)
{
    if (!folder.isDirectory()) return;
    auto files = folder.findChildFiles(juce::File::findFiles, false, uploaded ? "*.wav" : "*.2gs");
    files.removeIf([](const juce::File& file)
                   { return file.getFileNameWithoutExtension().endsWithIgnoreCase("_original"); });

    // Each sample is decoded and analysed on its own, so spread them over the cores.
    std::vector<std::optional<Instrument>> slots((size_t)files.size());
    parallelFor(files.size(), [&files, &slots, uploaded](int index)
    {
        const auto& file = files.getReference(index);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto reader = SoundStore::openReader(formats, file);
        if (reader == nullptr) return;

        PreparedGeneratedSample prepared;
        prepared.processed = resampleReaderToEngineRate(reader.get());
        if (prepared.processed.empty()) return;
        const auto originalFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "_original" + file.getFileExtension());
        if (originalFile.existsAsFile())
        {
            auto originalReader = SoundStore::openReader(formats, originalFile);
            prepared.original = resampleReaderToEngineRate(originalReader.get());
        }
        if (prepared.original.empty()) prepared.original = prepared.processed;
        else
        {
            // The processed file is the original with its lead-in trimmed; recover where it starts.
            // An _original that is not actually the source of this file keeps the safe offset 0.
            const size_t offset = trimStart(prepared.original);
            if (offset + prepared.processed.size() <= prepared.original.size() + 8)
                prepared.originalOffset = (int)offset;
        }

        auto description = reader->metadataValues[juce::WavAudioFormat::bwavDescription];
        prepared.prompt = description.startsWithIgnoreCase("Prompt:") ? description.substring(7).trim()
                                                                       : file.getFileNameWithoutExtension();
        prepared.processedFile = file.getFullPathName();
        prepared.originalFile = originalFile.existsAsFile() ? originalFile.getFullPathName() : juce::String();
        prepared.generatedAt = reader->metadataValues[juce::WavAudioFormat::bwavOriginationDate];
        const int unity = reader->metadataValues.getValue("MidiUnityNote", "60").getIntValue();
        const double fraction = (double)reader->metadataValues.getValue("MidiPitchFraction", "0").getLargeIntValue() / 4294967295.0;
        prepared.rootMidiNote = std::clamp((int)std::lround(unity + fraction), 0, 127);
        prepared.rootCents = (float)((unity + fraction - prepared.rootMidiNote) * 100.0);
        prepared.pitched = reader->metadataValues.containsKey("MidiUnityNote");
        if (!prepared.pitched) {
            const auto pitch = SamplePitch::analyse(prepared.processed, kEngineSampleRate);
            prepared.pitched = pitch.pitched; prepared.rootMidiNote = pitch.note;
            prepared.rootCents = pitch.cents; prepared.pitchConfidence = pitch.confidence;
        }
        analyseGeneratedPlayback(prepared);

        int selectedLoop = prepared.loopRegions.empty() ? 0 : 1;
        VoiceParams savedParams { 10, 1, 3, 5, 1, 10, 5 };
        float trimStart = 0.0f, trimEnd = 1.0f;
        juce::String savedName; // a name the user typed in the editor wins over the prompt
        int savedRoot = prepared.rootMidiNote;
        bool savedPitched = prepared.pitched, savedUseOriginal = false;
        const auto settingsFile = file.getSiblingFile(file.getFileName() + ".2g.json");
        if (settingsFile.existsAsFile())
        {
            const auto settings = juce::JSON::parse(settingsFile.loadFileAsString());
            if (settings.isObject())
            {
                const auto readFloat = [&settings](const char* key, float fallback)
                { return settings.hasProperty(key) ? (float)settings[key] : fallback; };
                savedParams.brightness = readFloat("brightness", savedParams.brightness);
                savedParams.attack = readFloat("attack", savedParams.attack);
                savedParams.release = readFloat("release", savedParams.release);
                savedParams.noise = readFloat("noise", savedParams.noise);
                savedParams.quality = readFloat("quality", savedParams.quality);
                trimStart = readFloat("trimStart", trimStart);
                trimEnd = readFloat("trimEnd", trimEnd);
                if (settings.hasProperty("name")) savedName = settings["name"].toString().trim().substring(0, 28);
                savedRoot = settings.hasProperty("rootNote") ? (int)settings["rootNote"] : savedRoot;
                savedPitched = settings.hasProperty("pitched") ? (bool)settings["pitched"] : savedPitched;
                savedUseOriginal = settings.hasProperty("useOriginal") ? (bool)settings["useOriginal"] : false;
                if (settings.hasProperty("rootCents")) prepared.rootCents = (float)settings["rootCents"];
                if (settings.hasProperty("loop")) selectedLoop = (int)settings["loop"];
                if (settings.hasProperty("playback") && settings["playback"].toString() == "oneshot")
                {
                    selectedLoop = 0;
                    savedParams.release = 10.0f;
                }
            }
        }

        Instrument instrument;
        instrument.name = savedName.isNotEmpty() ? savedName : prepared.prompt.substring(0, 28);
        instrument.category = uploaded ? "Uploaded" : "Generated";
        instrument.uploaded = uploaded;
        instrument.body = std::move(prepared.processed);
        instrument.originalBody = std::move(prepared.original);
        instrument.originalOffset = prepared.originalOffset;
        instrument.rootMidiNote = std::clamp(savedRoot, 0, 127);
        instrument.rootCents = prepared.rootCents;
        instrument.pitchDetected = savedPitched;
        instrument.baseNote = (float)std::clamp(savedRoot, 0, 127)
            + (savedPitched ? prepared.rootCents / 100.0f : 0.0f);
        instrument.liveBaseNote = std::make_shared<std::atomic<float>>(instrument.baseNote);
        instrument.useOriginalSource = std::make_shared<std::atomic<bool>>(savedUseOriginal);
        instrument.playbackStart = std::make_shared<std::atomic<float>>(std::clamp(trimStart, 0.0f, 0.98f));
        instrument.playbackEnd = std::make_shared<std::atomic<float>>(std::clamp(trimEnd, trimStart + 0.02f, 1.0f));
        instrument.loopRegions = prepared.loopRegions;
        selectedLoop = std::clamp(selectedLoop, 0, (int)instrument.loopRegions.size());
        instrument.selectedLoop = std::make_shared<std::atomic<int>>(selectedLoop);
        instrument.loopStart = prepared.loopStart;
        instrument.loopEnd = prepared.loopEnd;
        savedParams.clampAll();
        instrument.defaultParams = savedParams;
        instrument.sourcePrompt = prepared.prompt;
        instrument.generatedAt = prepared.generatedAt;
        instrument.processedFile = prepared.processedFile;
        instrument.originalFile = prepared.originalFile;
        instrument.sourceFile = prepared.processedFile;
        slots[(size_t)index] = std::move(instrument);
    });

    bool added = false;
    for (auto& slot : slots)
    {
        if (!slot) continue;
        card.instruments.push_back(std::move(*slot));
        added = true;
    }
    if (added) card.categories.addIfNotAlreadyThere(uploaded ? "Uploaded" : "Generated");
}
