#include "PluginProcessor.h"
#include "HandsetSizeConstrainer.h"
#include "WebViewSettle.h"
#include "AppPaths.h"
#include "DevShortcutWatcher.h"
#include "WebViewFolder.h"
#include "LicenseClient.h"
#include "SampleImporter.h"
#include "PitchAnalysis.h"
#include "EngineConstants.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace
{
juce::File pluginResourcesDirectory();

juce::var errorResult(const juce::String& message)
{
    auto* value = new juce::DynamicObject();
    value->setProperty("ok", false); value->setProperty("error", message);
    return juce::var(value);
}

juce::String loadLocalSetting(const juce::String& name)
{
    auto value = juce::SystemStats::getEnvironmentVariable(name, {}).trim();
    if (value.isNotEmpty()) return value;

    juce::Array<juce::File> candidates { juce::File::getCurrentWorkingDirectory().getChildFile(".env") };
    const auto appData = appDataFolder();
    candidates.add(appData.getChildFile(".env"));
    candidates.add(appData.getChildFile("settings.env"));
    candidates.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Scrap Brain Audio").getChildFile("2G").getChildFile(".env"));
    auto folder = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (int level = 0; level < 6; ++level)
    {
        candidates.addIfNotAlreadyThere(folder.getChildFile(".env"));
        folder = folder.getParentDirectory();
    }
    // A plug-in is loaded by the DAW, so currentExecutableFile is the DAW
    // rather than the VST3. Also walk up from the bundle resources; this
    // finds the project's .env during development without putting a key in
    // the browser or serialised plug-in state.
    folder = pluginResourcesDirectory();
    for (int level = 0; level < 10 && folder.exists(); ++level)
    {
        candidates.addIfNotAlreadyThere(folder.getChildFile(".env"));
        folder = folder.getParentDirectory();
    }
    for (const auto& file : candidates)
    {
        if (!file.existsAsFile()) continue;
        for (const auto& raw : juce::StringArray::fromLines(file.loadFileAsString()))
        {
            const auto line = raw.trim();
            if (line.startsWithChar('#') || !line.startsWith(name + "=")) continue;
            value = line.fromFirstOccurrenceOf("=", false, false).trim().unquoted();
            if (value.isNotEmpty()) return value;
        }
    }
    return {};
}

float promptInfluence()
{
    const auto value = loadLocalSetting("ELEVENLABS_PROMPT_INFLUENCE");
    return value.isEmpty() ? 0.4f : std::clamp(value.getFloatValue(), 0.0f, 1.0f);
}

juce::File pluginResourcesDirectory()
{
   #if JUCE_WINDOWS
    HMODULE module{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&pluginResourcesDirectory), &module) != 0)
    {
        wchar_t path[MAX_PATH]{};
        if (GetModuleFileNameW(module, path, MAX_PATH) != 0)
        {
            const auto resources = juce::File(juce::String(path)).getParentDirectory()
                .getParentDirectory().getChildFile("Resources");
            if (resources.isDirectory()) return resources;
        }
    }
   #else
    // 2G.vst3/Contents/MacOS/2G -> 2G.vst3/Contents/Resources. The plug-in is loaded by the DAW, so the module's own
    // path is asked for, not the host's executable.
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&pluginResourcesDirectory), &info) != 0 && info.dli_fname != nullptr)
    {
        const auto resources = juce::File(juce::String::fromUTF8(info.dli_fname)).getParentDirectory()
            .getParentDirectory().getChildFile("Resources");
        if (resources.isDirectory()) return resources;
    }
   #endif
    return {};
}

std::optional<juce::WebBrowserComponent::Resource> providePluginResource(const juce::String& url)
{
    auto path = url;
    if (path.isEmpty() || path == "/") path = "/index.html";
    if (path.startsWithChar('/')) path = path.substring(1);
    if (const auto query = path.indexOfChar('?'); query >= 0) path = path.substring(0, query);
    const auto file = pluginResourcesDirectory().getChildFile(path);
    juce::MemoryBlock data;
    if (!file.existsAsFile() || !file.loadFileAsData(data)) return std::nullopt;

    juce::String mime = "application/octet-stream";
    if (file.hasFileExtension(".html")) mime = "text/html; charset=utf-8";
    else if (file.hasFileExtension(".css")) mime = "text/css; charset=utf-8";
    else if (file.hasFileExtension(".js")) mime = "application/javascript; charset=utf-8";
    else if (file.hasFileExtension(".png")) mime = "image/png";
    else if (file.hasFileExtension(".wav")) mime = "audio/wav";
    else if (file.hasFileExtension(".svg")) mime = "image/svg+xml";
    else if (file.hasFileExtension(".ttf")) mime = "font/ttf";
    const auto* begin = static_cast<const std::byte*>(data.getData());
    return juce::WebBrowserComponent::Resource{ std::vector<std::byte>(begin, begin + data.getSize()), mime };
}

class Melodragon2GEditor final : public juce::AudioProcessorEditor
{
public:
    explicit Melodragon2GEditor(Melodragon2GProcessor& p) : AudioProcessorEditor(p), processor(p)
    {
        auto options = juce::WebBrowserComponent::Options{}
           #if JUCE_WINDOWS
            .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}
                .withStatusBarDisabled().withBackgroundColour(juce::Colour(0xff1e1e1e))
                .withUserDataFolder(webViewDataFolder("Plugin")))
           #endif
            .withNativeIntegrationEnabled(true)
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withInitialisationData("isPluginHost", true)
            .withInitialisationData("viewWidth", 320)
            .withInitialisationData("viewHeight", 440)
            .withInitialisationData("showStartupConsole", processor.shouldShowStartupConsole());

        sampleImporter = std::make_unique<SampleImporter>([this](auto operation) {
            const juce::ScopedLock lock(processor.getCallbackLock());
            auto result = operation(processor.getEngine());
            if (result.isObject() && (bool)result["ok"] && result.hasProperty("index")) processor.selectSound((int)result["index"]);
            return result;
        }, [this] { sendInstrumentUpdate(); });
        options = options
            .withNativeFunction("detectRootNote", [this](const auto& args, auto done) { sampleImporter->detectPitch(args.isEmpty() ? 0 : (int)args[0], std::move(done)); })
        .withNativeFunction("importSound", [this](const auto& args, auto done) {
                if (args.size() >= 2) sampleImporter->drop(args[0].toString(), args[1].toString(), std::move(done));
                else sampleImporter->choose(std::move(done));
            })
            .withNativeFunction("finishImport", [this](const auto& args, auto done) { sampleImporter->finish(!args.isEmpty() && (bool)args[0], std::move(done)); })
            .withNativeFunction("getMasterVolume", [this](const auto&, auto done) { done(processor.getEngine().getMasterVolume()); })
            .withNativeFunction("getSpeakerSim", [this](const auto&, auto done) { done(processor.getEngine().isSpeakerSimulationEnabled()); })
            .withNativeFunction("setMasterVolume", [this](const auto& args, auto done) {
                if (!args.isEmpty()) processor.getEngine().setMasterVolume((float)args[0]);
                done(processor.getEngine().getMasterVolume());
            })
            .withNativeFunction("setUiScale", [this](const auto& args, auto done) {
                // The page offers only sizes that give whole device pixels per art pixel.
                const double percent = args.isEmpty() ? 100.0 : static_cast<double>(args[0]);
                if (!(percent >= 40.0 && percent <= 400.0)) { done(false); return; }
                // Posted, not done here: this runs inside WebView2's message handler, and a resize from there can be
                // dropped by WebView2 (window resized, page left at its old size in the corner).
                const int width = juce::roundToInt(320.0 * percent / 100.0), height = juce::roundToInt(440.0 * percent / 100.0);
                juce::Component::SafePointer<Melodragon2GEditor> safe(this);
                juce::MessageManager::callAsync([safe, width, height] { if (safe != nullptr) safe->setSize(width, height); });
                done(true);
            })
            .withNativeFunction("getViewSize", [this](const auto&, auto done) {
                // The size the page should be filling, so the page can tell when WebView2 has not caught up.
                auto* size = new juce::DynamicObject();
                size->setProperty("w", getWidth()); size->setProperty("h", getHeight());
                done(juce::var(size));
            })
            .withNativeFunction("focusWebView", [this](const auto&, auto done) {
                juce::Component::SafePointer<Melodragon2GEditor> safe(this);
                juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->focusWebView(); });
                done(true);
            })
            .withNativeFunction("repairWebView", [this](const auto&, auto done) {
                juce::Component::SafePointer<Melodragon2GEditor> safe(this);
                juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->settleWebView(true); });
                done(true);
            })
            .withNativeFunction("setResizeSteps", [this](const auto& args, auto done) {
                // The sizes the page offers, so a host-side window drag snaps to whole art pixels.
                std::vector<double> steps;
                if (!args.isEmpty() && args[0].isArray())
                    for (const auto& value : *args[0].getArray())
                        if (const double percent = (double) value; percent >= 40.0 && percent <= 400.0) steps.push_back(percent);
                sizeConstrainer.setSteps(std::move(steps));
                done(true);
            })
            .withNativeFunction("setDemoWindow", [this](const auto& args, auto done) {
                // Demo recording mode. The host decides whether it honours the new editor size.
                const bool enable = !args.isEmpty() && (bool) args[0];
                if (enable)
                {
                    if (!demoEditorActive) { editorSizeBeforeDemo = { getWidth(), getHeight() }; demoEditorActive = true; }
                    const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.reduced(60);
                    setSize(area.getWidth(), area.getHeight());
                }
                else if (demoEditorActive)
                {
                    setSize(editorSizeBeforeDemo.x, editorSizeBeforeDemo.y);
                    demoEditorActive = false;
                }
                done(true);
            })
            .withNativeFunction("getNoteGate", [this](const auto&, auto done) { done(processor.getEngine().getDisplayInputState()); })
            .withNativeFunction("getLicenseStatus", [this](const auto&, auto done) {
                juce::Component::SafePointer<Melodragon2GEditor> safeThis(this);
                if (!juce::Thread::launch([safeThis, done]() mutable {
                    auto result = LicenseClient::checkStatus();
                    juce::MessageManager::callAsync([safeThis, result = std::move(result), done = std::move(done)]() mutable {
                        if (safeThis != nullptr) done(std::move(result));
                    });
                })) done(errorResult("Service busy"));
            })
            .withNativeFunction("submitFeedback", [this](const auto& args, auto done) {
                const auto id = args.size() > 0 ? args[0].toString() : juce::String();
                const auto message = args.size() > 1 ? args[1].toString() : juce::String();
                const int choice = args.size() > 2 ? (int)args[2] : -1;
                juce::Component::SafePointer<Melodragon2GEditor> safeThis(this);
                if (!juce::Thread::launch([safeThis, id, message, choice, done]() mutable {
                    auto result = LicenseClient::submitFeedback(id, message, choice);
                    juce::MessageManager::callAsync([safeThis, result = std::move(result), done = std::move(done)]() mutable {
                        if (safeThis != nullptr) done(std::move(result));
                    });
                })) done(errorResult("Service busy"));
            })
            .withNativeFunction("checkForUpdate", [this](const auto&, auto done) {
                juce::Component::SafePointer<Melodragon2GEditor> safeThis(this);
                if (!juce::Thread::launch([safeThis, done]() mutable {
                    auto result = LicenseClient::checkForUpdate();
                    juce::MessageManager::callAsync([safeThis, result = std::move(result), done = std::move(done)]() mutable {
                        if (safeThis != nullptr) done(std::move(result));
                    });
                })) done(errorResult("Service busy"));
            })
            .withNativeFunction("openUpdatePage", [](const auto&, auto done) { done(LicenseClient::openUpdatePage()); })
            .withNativeFunction("activateLicense", [this](const auto& args, auto done) {
                const auto code = args.isEmpty() ? juce::String() : args[0].toString();
                juce::Component::SafePointer<Melodragon2GEditor> safeThis(this);
                if (!juce::Thread::launch([safeThis, code, done]() mutable {
                    auto result = LicenseClient::activate(code);
                    juce::MessageManager::callAsync([safeThis, result = std::move(result), done = std::move(done)]() mutable {
                        if (safeThis != nullptr) done(std::move(result));
                    });
                })) done(errorResult("Service busy"));
            })
            .withNativeFunction("getInstruments", [this](const auto&, auto done) { done(processor.getEngine().getInstrumentLibrary()); })
            .withNativeFunction("getStartupStatus", [this](const auto&, auto done) {
                auto* status = new juce::DynamicObject();
                status->setProperty("library", true);
                status->setProperty("audio", true);
                status->setProperty("engineRate", kEngineSampleRate);
                status->setProperty("outputRate", processor.getSampleRate());
                status->setProperty("blockSize", processor.getBlockSize());
                status->setProperty("channels", processor.getTotalNumOutputChannels());
                done(juce::var(status));
            })
            .withNativeFunction("startupConsoleShown", [this](const auto&, auto done) {
                processor.markStartupConsoleShown();done(true);
            })
            .withNativeFunction("getVoiceParams", [this](const auto& args, auto done) {
                const auto p = processor.getEngine().getVoiceParams(args.isEmpty() ? 0 : (int) args[0]);
                done(juce::var(juce::Array<juce::var>{ p.brightness, p.attack, p.release, p.detune, p.noise, p.quality, p.formant }));
            })
            .withNativeFunction("getInstrumentEditorData", [this](const auto& args, auto done) {
                done(processor.getEngine().getInstrumentEditorData(args.isEmpty() ? 0 : (int) args[0]));
            })
            .withNativeFunction("setInstrumentEditorControls", [this](const auto& args, auto done) {
                if (args.size() < 4) { done(errorResult("Invalid editor controls")); return; }
                const auto index = (int) args[0];
                auto result = processor.getEngine().setInstrumentEditorControls(index, (float) args[1], (float) args[2], (int) args[3], args.size() > 4 ? (float)args[4] : NAN, args.size() > 5 ? ((bool)args[5] ? 1 : 0) : -1);
                if ((bool) result["ok"]) saveSettings(index); done(std::move(result));
            })
            .withNativeFunction("setInstrumentLoop", [this](const auto& args, auto done) {
                if (args.size() < 2) { done(errorResult("Invalid loop selection")); return; }
                const auto index = (int) args[0]; auto result = processor.getEngine().setInstrumentLoop(index, (int) args[1]);
                if ((bool) result["ok"]) saveSettings(index); done(std::move(result));
            })
            .withNativeFunction("setGeneratedSampleControls", [this](const auto& args, auto done) {
                if (args.size() < 4) { done(errorResult("Invalid controls")); return; }
                const auto index = (int) args[0];
                auto result = processor.getEngine().setGeneratedSampleControls(index, (int) args[1], (bool) args[2], (bool) args[3]);
                if ((bool) result["ok"]) saveSettings(index); done(std::move(result));
            })
            .withNativeFunction("renameInstrument", [this](const auto& args, auto done) {
                if (args.size() < 2) { done(errorResult("Invalid name")); return; }
                const auto index = (int) args[0];
                juce::var result;
                {
                    const juce::ScopedLock lock(processor.getCallbackLock());
                    result = processor.getEngine().renameInstrument(index, args[1].toString());
                }
                if ((bool) result["ok"]) { saveSettings(index); sendInstrumentUpdate(); }
                done(std::move(result));
            })
            .withNativeFunction("deleteGeneratedSample", [this](const auto& args, auto done) {
                const juce::ScopedLock lock(processor.getCallbackLock());
                auto result = processor.getEngine().deleteGeneratedSample(args.isEmpty() ? -1 : (int) args[0]);
                if ((bool) result["ok"]) { SampleImporter::removeFiles(result); sendInstrumentUpdate(); } done(std::move(result));
            })
            .withNativeFunction("generateSoundEffect", [this](const auto& args, auto done) {
                generateSoundEffect(args.isEmpty() ? juce::String() : args[0].toString(),
                                    args.size() > 1 ? args[1] : juce::var(), std::move(done));
            })
            .withEventListener("setMidiOptions", [this](const juce::var& data) {
                processor.getEngine().setVelocityTouch(!data.hasProperty("touch") || (bool) data["touch"]);
                processor.getEngine().setBendRange(data.hasProperty("bend") ? (float) data["bend"] : 2.0f);
            })
            .withEventListener("noteOn", [this](const juce::var& data) { processor.getEngine().noteOn((int) data["note"], data.hasProperty("velocity") ? (float) data["velocity"] : 0.9f); })
            .withEventListener("noteOff", [this](const juce::var& data) { processor.getEngine().noteOff((int) data["note"]); })
            .withEventListener("allNotesOff", [this](const juce::var&) { processor.getEngine().allNotesOff(); })
            .withEventListener("selectVoice", [this](const juce::var& data) { processor.selectSound((int) data["index"]); })
            .withEventListener("previewVoice", [this](const juce::var& data) { processor.getEngine().previewVoice((int) data["index"]); })
            .withEventListener("toggleSpeaker", [this](const juce::var&) { processor.getEngine().toggleSpeakerSimulation(); })
            .withEventListener("toggleMovement", [this](const juce::var&) { processor.getEngine().toggleMovement(); })
            .withEventListener("setVoiceParam", [this](const juce::var& data) {
                const auto index = (int) data["instrument"];
                processor.getEngine().setVoiceParam(index, (int) data["parameter"], (float) data["value"]); saveSettings(index);
            })
            .withResourceProvider([](const juce::String& url) { return providePluginResource(url); });

        webView = std::make_unique<juce::WebBrowserComponent>(options);
        addAndMakeVisible(*webView); setConstrainer(&sizeConstrainer); setResizable(true, false); setSize(320, 440);
        // Release build: no native developer-shortcut watcher (DevShortcutWatcher.h is kept for development builds).
        webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    }

    void resized() override
    {
        if (webView == nullptr) return;
        // The web view stays the size of the whole desktop and the window shows its top-left part. Resizing
        // WebView2 along with the window left it stuck at an old size, so after the first layout it is never resized;
        // the page is told the visible size instead (initialisation data, then the viewSize event).
       #if JUCE_WINDOWS
        const auto desk = largestDisplaySize();
        webView->setBounds(0, 0, std::max(getWidth(), desk.x), std::max(getHeight(), desk.y));
       #else
        // A web view larger than the editor could draw over the host's own interface; WKWebView does not need
        // the fixed-size trick, so it simply fills the editor.
        webView->setBounds(getLocalBounds());
       #endif
        auto* size = new juce::DynamicObject();
        size->setProperty("w", getWidth()); size->setProperty("h", getHeight());
        webView->emitEventIfBrowserIsVisible("viewSize", juce::var(size));
    }
    void focusWebView()
    {
        if (webView != nullptr && !webView->hasKeyboardFocus(true)) webView->grabKeyboardFocus();
    }
    // A freshly opened editor takes the keyboard right away.
    void parentHierarchyChanged() override
    {
        juce::Component::SafePointer<Melodragon2GEditor> safe(this);
        juce::MessageManager::callAsync([safe] { if (safe != nullptr && safe->isShowing()) safe->focusWebView(); });
    }
    void settleWebView(bool hard = false)
    {
        if (webView == nullptr) return;
        const auto bounds = getLocalBounds();
        if (hard) webView->setVisible(false); // hiding and showing makes WebView2 lay its page out again
        webView->setBounds(bounds.withTrimmedRight(1));
        if (hard) webView->setVisible(true);
        webView->setBounds(bounds);
    }
    void syncFromProcessor() { sendInstrumentUpdate(); }

private:
    void generateSoundEffect(const juce::String& rawPrompt, const juce::var& options,
                             juce::WebBrowserComponent::NativeFunctionCompletion completion)
    {
        const auto prompt = rawPrompt.trim().substring(0, 450);
        if (prompt.isEmpty()) { completion(errorResult("Write something")); return; }
        // Checked before the request: install() would refuse it after the credit was already spent.
        if (sampleImporter->pending()) { completion(errorResult("Save or discard first")); return; }
        const bool forceUnpitched = prompt.containsWholeWordIgnoreCase("chord");
        const auto& apiPrompt = prompt;
        const auto generationOptions = GenerationOptions::fromVar(options);
        const float influence = generationOptions.influence >= 0.0f ? generationOptions.influence : promptInfluence();

        juce::Component::SafePointer<Melodragon2GEditor> safeThis(this);
        if (!juce::Thread::launch([safeThis, prompt, apiPrompt, forceUnpitched, influence, generationOptions,
                                   completion = std::move(completion)]() mutable
        {
            juce::String error;
            juce::String kind;
            auto audio = LicenseClient::generate(apiPrompt, influence, error, generationOptions, &kind);
            const bool unpitched = forceUnpitched || kind == "hit" || kind == "effect";
            auto prepared = error.isEmpty() ? ToneCard::prepareGeneratedSample(audio, prompt, unpitched, false, !unpitched, generationOptions.loop)
                                            : PreparedGeneratedSample{};
            if (prepared.error.isNotEmpty()) error = prepared.error;
            else if (error.isEmpty()) SoundEngine::calibratePrepared(prepared);
            juce::MessageManager::callAsync([safeThis, prepared = std::move(prepared), error,
                                             completion = std::move(completion)]() mutable
            {
                if (safeThis == nullptr) return;
                if (error.isNotEmpty()) { completion(errorResult(error)); return; }
                const juce::ScopedLock lock(safeThis->processor.getCallbackLock());
                auto result = safeThis->sampleImporter->install(std::move(prepared));
                if ((bool) result["ok"])
                {
                    safeThis->processor.selectSound((int) result["index"]);
                    safeThis->saveSettings((int) result["index"]);
                    safeThis->sendInstrumentUpdate();
                }
                completion(std::move(result));
            });
        })) completion(errorResult("Engine busy"));
    }

    void saveSettings(int index)
    {
        const auto path = processor.getEngine().getInstrumentSettingsPath(index);
        const auto json = processor.getEngine().getInstrumentSettingsJson(index);
        if (path.isNotEmpty() && json.isNotEmpty()) juce::Thread::launch([path, json] { juce::File(path).replaceWithText(json); });
    }
    void sendInstrumentUpdate()
    {
        if (webView != nullptr) {
            webView->emitEventIfBrowserIsVisible("instrumentsUpdated", processor.getEngine().getInstrumentLibrary());
            webView->emitEventIfBrowserIsVisible("masterVolumeChanged", processor.getEngine().getMasterVolume());
        }
    }
    Melodragon2GProcessor& processor;
    juce::Point<int> editorSizeBeforeDemo;
    HandsetSizeConstrainer sizeConstrainer;
    bool demoEditorActive = false;
    std::unique_ptr<juce::WebBrowserComponent> webView;
    std::unique_ptr<SampleImporter> sampleImporter;
    std::unique_ptr<DevShortcutWatcher> devShortcutWatcher;
};
}

Melodragon2GProcessor::Melodragon2GProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) { engine.loadDefaultToneCard(); }
void Melodragon2GProcessor::prepareToPlay(double rate, int) { engine.prepareForPlayback(rate); }
void Melodragon2GProcessor::releaseResources() { engine.allNotesOff(); }
bool Melodragon2GProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{ const auto output = layouts.getMainOutputChannelSet(); return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo(); }
void Melodragon2GProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    for (const auto item : midi) { const auto m = item.getMessage(); if (m.isNoteOn()) engine.noteOn(m.getNoteNumber(), m.getFloatVelocity()); else if (m.isNoteOff()) engine.noteOff(m.getNoteNumber()); else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff(); else if (m.isPitchWheel()) engine.setPitchWheel(m.getPitchWheelValue()); else if (m.isProgramChange()) selectSound(m.getProgramChangeNumber()); }
    buffer.clear(); engine.renderAudioBlock(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}
juce::AudioProcessorEditor* Melodragon2GProcessor::createEditor() { return new Melodragon2GEditor(*this); }
int Melodragon2GProcessor::getNumPrograms() { return std::max(1, engine.getInstrumentNames().size()); }
int Melodragon2GProcessor::getCurrentProgram() { return getSelectedSound(); }
void Melodragon2GProcessor::setCurrentProgram(int index) { selectSound(index); }
const juce::String Melodragon2GProcessor::getProgramName(int index) { const auto names = getSoundNames(); return names.isEmpty() ? "2G" : names[juce::jlimit(0, names.size() - 1, index)]; }
void Melodragon2GProcessor::getStateInformation(juce::MemoryBlock& data)
{
    const juce::ScopedLock lock(getCallbackLock());
    auto state = std::make_unique<juce::XmlElement>("Melodragon2G");
    state->setAttribute("sound", getSelectedSound());
    state->createNewChildElement("SoundState")->addTextElement(juce::JSON::toString(engine.getSessionState(), false));
    copyXmlToBinary(*state, data);
}
void Melodragon2GProcessor::setStateInformation(const void* data, int size)
{
    if (auto state = getXmlFromBinary(data, size); state != nullptr && state->hasTagName("Melodragon2G"))
    {
        const juce::ScopedLock lock(getCallbackLock());
        juce::var session;
        if (const auto* saved = state->getChildByName("SoundState")) {
            session = juce::JSON::parse(saved->getAllSubText()); engine.restoreSessionState(session);
        }
        if (!session.hasProperty("selectedSound")) selectSound(state->getIntAttribute("sound", 0));
        juce::Component::SafePointer<Melodragon2GEditor> editor(dynamic_cast<Melodragon2GEditor*>(getActiveEditor()));
        juce::MessageManager::callAsync([editor] { if (editor) editor->syncFromProcessor(); });
    }
}
juce::StringArray Melodragon2GProcessor::getSoundNames() const { return engine.getInstrumentNames(); }
void Melodragon2GProcessor::selectSound(int index) { const auto count = getNumPrograms(); const auto safe = juce::jlimit(0, count - 1, index % count); engine.selectVoice(safe); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Melodragon2GProcessor(); }
