#pragma once
#include "SoundEngine.h"
#include "PitchAnalysis.h"
#include "SoundStore.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

// One unsaved import per editor. Decode/analysis and file I/O never run in audio callbacks.
class SampleImporter final : public juce::Component
{
public:
    using Done = juce::WebBrowserComponent::NativeFunctionCompletion;
    using Mutation = std::function<juce::var(std::function<juce::var(SoundEngine&)>)>;
    void detectPitch(int index, Done done) {
        PreparedGeneratedSample sample;
        mutate([&](SoundEngine& engine) { sample = engine.copyGeneratedSampleForSaving(index); return juce::var(); });
        // Only the part that is actually played: after trimming, "analyse again" listens to what is left.
        const auto total = sample.processed.size();
        const auto from = std::min(total, (size_t) std::llround(juce::jlimit(0.0f, 1.0f, sample.trimStart) * (float) total));
        const auto to = std::max(from, std::min(total, (size_t) std::llround(juce::jlimit(0.0f, 1.0f, sample.trimEnd) * (float) total)));
        if (!juce::Thread::launch([audio = std::vector<float>(sample.processed.begin() + (ptrdiff_t) from, sample.processed.begin() + (ptrdiff_t) to), done] {
            const auto pitch = SamplePitch::analyseSelection(audio);
            auto* result = new juce::DynamicObject();
            result->setProperty("ok", !audio.empty()); result->setProperty("rootNote", pitch.note);
            result->setProperty("rootCents", pitch.cents); result->setProperty("pitched", pitch.pitched);
            result->setProperty("confidence", pitch.confidence);
            juce::MessageManager::callAsync([done, result = juce::var(result)] { done(result); });
        })) done(error("Engine busy"));
    }
    SampleImporter(Mutation edit, std::function<void()> changed) : mutate(std::move(edit)), notify(std::move(changed)) {}
    ~SampleImporter() override {
        if (draft < 0 || busy) return;
        // Closing the editor or the app (or the host dropping the plug-in) with an unsaved sound saves it: the work
        // is never lost just because "Save" was not pressed.
        PreparedGeneratedSample sample;
        mutate([&](SoundEngine& engine) { sample = engine.copyGeneratedSampleForSaving(draft); return juce::var(true); });
        if (writeSampleFiles(sample)) {
            juce::String settingsPath, settingsJson;
            mutate([&](SoundEngine& engine) {
                auto saved = engine.commitImportedSample(draft, sample.processedFile, sample.originalFile);
                settingsPath = engine.getInstrumentSettingsPath(draft); settingsJson = engine.getInstrumentSettingsJson(draft);
                return saved;
            });
            if (settingsPath.isNotEmpty()) juce::File(settingsPath).replaceWithText(settingsJson);
            return;
        }
        mutate([this](SoundEngine& engine) { auto result = engine.deleteGeneratedSample(draft); engine.selectVoice(previous); result.getDynamicObject()->setProperty("index", previous); return result; });
    }
    // Writes the processed and original WAV files of `sample` into Documents/Scrap Brain Audio/2G. False if that failed.
    static bool writeSampleFiles(PreparedGeneratedSample& sample) {
        auto directory = SoundStore::folderFor(sample.uploaded);
        auto name = sample.prompt.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_").trim().replaceCharacter(' ', '_').substring(0, 28);
        if (name.isEmpty()) name = "Sample";
        const auto file = directory.getNonexistentChildFile(juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S_") + name, SoundStore::extensionFor(sample.uploaded), false);
        sample.processedFile = file.getFullPathName();
        sample.originalFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "_original" + SoundStore::extensionFor(sample.uploaded)).getFullPathName();
        ToneCard::saveGeneratedSampleFiles(sample, sample.pitched ? sample.rootMidiNote + sample.rootCents / 100.0f : 60.0f);
        const bool written = file.getSize() > 44 && juce::File(sample.originalFile).getSize() > 44;
        if (!written) { file.deleteFile(); juce::File(sample.originalFile).deleteFile(); }
        return written;
    }
    static void removeFiles(const juce::var& result) {
        const auto processed = result["processedFile"].toString(), original = result["originalFile"].toString();
        juce::Thread::launch([processed, original] {
            for (const auto& path : { processed, original }) {
                if (path.isEmpty()) continue;
                const juce::File file(path);
                if (!SoundStore::isStoreFile(file)) continue;
                file.deleteFile(); file.getSiblingFile(file.getFileName() + ".2g.json").deleteFile();
            }
        });
    }
    static juce::var error(const juce::String& message) {
        auto* result = new juce::DynamicObject(); result->setProperty("ok", false); result->setProperty("error", message); return juce::var(result);
    }
    juce::var install(PreparedGeneratedSample prepared) {
        if (busy || draft >= 0) return error("Save or discard first");
        prepared.importDraft = true;
        auto result = mutate([&](SoundEngine& engine) {
            previous = engine.getSelectedInstrument();
            return engine.installGeneratedSample(std::move(prepared));
        });
        if ((bool)result["ok"]) { draft = (int)result["index"]; notify(); }
        return result;
    }
    void choose(Done done) {
        if (busy || draft >= 0) { done(error("Save or discard first")); return; }
        chooser = std::make_unique<juce::FileChooser>("Import sound", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        juce::Component::SafePointer<SampleImporter> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, done](const juce::FileChooser& picker) mutable {
                if (!safe) return;
                const auto file = picker.getResult();
                if (!file.existsAsFile()) { auto result = error("Cancelled"); result.getDynamicObject()->setProperty("cancelled", true); done(result); return; }
                if (file.getSize() > 20 * 1024 * 1024) { done(error("File too large (20 MB)")); return; }
                safe->start(file.getFileNameWithoutExtension(), {}, file, std::move(done));
            });
    }
    void drop(const juce::String& name, const juce::String& encoded, Done done) {
        if (busy || draft >= 0) { done(error("Save or discard first")); return; }
        if (encoded.length() > 28 * 1024 * 1024) { done(error("File too large (20 MB)")); return; }
        start(juce::File::createFileWithoutCheckingPath(name).getFileNameWithoutExtension(), encoded, {}, std::move(done));
    }
    void finish(bool save, Done done) {
        if (busy || draft < 0) { done(error("No pending sample")); return; }
        if (!save) {
            auto result = mutate([this](SoundEngine& engine) {
                auto removed = engine.deleteGeneratedSample(draft);
                engine.selectVoice(previous); removed.getDynamicObject()->setProperty("index", previous); return removed;
            });
            draft = -1; notify(); done(result); return;
        }
        PreparedGeneratedSample sample;
        mutate([&](SoundEngine& engine) { sample = engine.copyGeneratedSampleForSaving(draft); return juce::var(true); });
        busy = true;
        juce::Component::SafePointer<SampleImporter> safe(this);
        if (!juce::Thread::launch([safe, sample = std::move(sample), done]() mutable {
            auto directory = SoundStore::folderFor(sample.uploaded);
            auto name = sample.prompt.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_").trim().replaceCharacter(' ', '_').substring(0, 28);
            if (name.isEmpty()) name = "Sample";
            const auto file = directory.getNonexistentChildFile(juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S_") + name, SoundStore::extensionFor(sample.uploaded), false);
            sample.processedFile = file.getFullPathName();
            sample.originalFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "_original" + SoundStore::extensionFor(sample.uploaded)).getFullPathName();
            ToneCard::saveGeneratedSampleFiles(sample, sample.pitched ? sample.rootMidiNote + sample.rootCents / 100.0f : 60.0f);
            const bool written = file.getSize() > 44 && juce::File(sample.originalFile).getSize() > 44;
            if (!written) { file.deleteFile(); juce::File(sample.originalFile).deleteFile(); }
            juce::MessageManager::callAsync([safe, sample = std::move(sample), written, done]() mutable {
                if (!safe) return;
                safe->busy = false;
                if (!written) { done(error("Could not save WAV")); return; }
                juce::String settingsPath, settingsJson;
                auto result = safe->mutate([&](SoundEngine& engine) {
                    auto saved = engine.commitImportedSample(safe->draft, sample.processedFile, sample.originalFile);
                    settingsPath = engine.getInstrumentSettingsPath(safe->draft); settingsJson = engine.getInstrumentSettingsJson(safe->draft);
                    return saved;
                });
                juce::Thread::launch([settingsPath, settingsJson] { if (settingsPath.isNotEmpty()) juce::File(settingsPath).replaceWithText(settingsJson); });
                safe->draft = -1; safe->notify(); done(result);
            });
        })) { busy = false; done(error("Engine busy")); }
    }
private:
    void start(juce::String name, juce::String encoded, juce::File file, Done done) {
        busy = true;
        juce::Component::SafePointer<SampleImporter> safe(this);
        if (!juce::Thread::launch([safe, name, encoded, file, done]() mutable {
            juce::MemoryBlock audio;
            bool decoded = false;
            if (file.existsAsFile()) decoded = file.loadFileAsData(audio);
            else { juce::MemoryOutputStream stream; decoded = juce::Base64::convertFromBase64(stream, encoded); audio = stream.getMemoryBlock(); }
            if (audio.getSize() > 20 * 1024 * 1024) { juce::MessageManager::callAsync([safe, done] { if (safe) { safe->busy = false; done(error("File too large (20 MB)")); } }); return; }
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(audio, false)));
            if (reader && reader->lengthInSamples > reader->sampleRate * 10.0) {
                juce::MessageManager::callAsync([safe, done] { if (safe) { safe->busy = false; done(error("Max 10 seconds")); } }); return;
            }
            auto prepared = decoded ? ToneCard::prepareGeneratedSample(audio, name, false, false) : PreparedGeneratedSample{};
            if (reader && reader->metadataValues.containsKey("MidiUnityNote")) {
                const double root = reader->metadataValues["MidiUnityNote"].getIntValue()
                    + reader->metadataValues.getValue("MidiPitchFraction", "0").getLargeIntValue() / 4294967295.0;
                if (root >= 0 && root <= 127) {
                    prepared.rootMidiNote = (int)std::lround(root);
                    prepared.rootCents = (float)((root-prepared.rootMidiNote)*100);
                    prepared.pitched = true; prepared.pitchConfidence = 1.0f;
                }
            }
            if (!decoded) prepared.error = "Cannot read audio";
            // After the metadata above: the root note decides the pitches that get probed.
            else if (prepared.error.isEmpty()) SoundEngine::calibratePrepared(prepared);
            prepared.uploaded = true; prepared.importDraft = true;
            juce::MessageManager::callAsync([safe, prepared = std::move(prepared), done]() mutable {
                if (!safe) return;
                safe->busy = false;
                if (prepared.error.isNotEmpty()) { done(error(prepared.error)); return; }
                auto result = safe->install(std::move(prepared));
                done(result);
            });
        })) { busy = false; done(error("Engine busy")); }
    }
    Mutation mutate;
    std::function<void()> notify;
    std::unique_ptr<juce::FileChooser> chooser;
    bool busy = false;
    int draft = -1, previous = 0;
};
