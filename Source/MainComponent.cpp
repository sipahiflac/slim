#include "WebViewSettle.h"
#include "AppPaths.h"
#include "PitchAnalysis.h"
#include "MainComponent.h"
#include "DevShortcutWatcher.h"
#include "WebViewFolder.h"
#include "GenerationPrompt.h"
#include "LicenseClient.h"
#include "EngineConstants.h"
#include <algorithm>

namespace
{
juce::var makeGenerationError(const juce::String& message)
{
    auto* result = new juce::DynamicObject();
    result->setProperty("ok", false);
    result->setProperty("error", message);
    return juce::var(result);
}

juce::String loadLocalSetting(const juce::String& name)
{
    auto value = juce::SystemStats::getEnvironmentVariable(name, {}).trim();
    if (value.isNotEmpty()) return value;

    juce::Array<juce::File> candidates;
    candidates.add(juce::File::getCurrentWorkingDirectory().getChildFile(".env"));
    auto folder = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    for (int level = 0; level < 6; ++level)
    {
        candidates.addIfNotAlreadyThere(folder.getChildFile(".env"));
        folder = folder.getParentDirectory();
    }

    for (const auto& file : candidates)
    {
        if (!file.existsAsFile()) continue;
        const auto lines = juce::StringArray::fromLines(file.loadFileAsString());
        for (const auto& rawLine : lines)
        {
            const auto line = rawLine.trim();
            if (line.startsWithChar('#') || !line.startsWith(name + "=")) continue;
            value = line.fromFirstOccurrenceOf("=", false, false).trim().unquoted();
            if (value.isNotEmpty()) return value;
        }
    }
    return {};
}

float loadPromptInfluence()
{
    const auto setting = loadLocalSetting("ELEVENLABS_PROMPT_INFLUENCE");
    return setting.isEmpty() ? 0.4f : std::clamp(setting.getFloatValue(), 0.0f, 1.0f);
}
}

juce::File MainComponent::getResourcesDirectory()
{
    // The build copies every UI asset here (see SyncPhoneArtwork in CMakeLists.txt).
   #if JUCE_MAC
    // 2G.app/Contents/MacOS/2G -> 2G.app/Contents/Resources
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory().getParentDirectory().getChildFile("Resources");
   #else
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory().getChildFile("Resources");
   #endif
}

std::optional<juce::WebBrowserComponent::Resource> MainComponent::handleResource(const juce::String& url)
{
    auto path = url;
    if (path == "/" || path.isEmpty())
        path = "/index.html";

    if (path.startsWithChar('/'))
        path = path.substring(1);

    if (int qIdx = path.indexOfChar('?'); qIdx >= 0)
        path = path.substring(0, qIdx);

    auto resDir = getResourcesDirectory();
    auto file = resDir.getChildFile(path);

    if (!file.existsAsFile())
        return std::nullopt;

    juce::MemoryBlock mb;
    if (!file.loadFileAsData(mb))
        return std::nullopt;

    juce::String mime = "application/octet-stream";
    if (file.hasFileExtension(".html"))      mime = "text/html; charset=utf-8";
    else if (file.hasFileExtension(".css"))  mime = "text/css; charset=utf-8";
    else if (file.hasFileExtension(".js"))   mime = "application/javascript; charset=utf-8";
    else if (file.hasFileExtension(".png"))  mime = "image/png";
    else if (file.hasFileExtension(".wav"))  mime = "audio/wav";
    else if (file.hasFileExtension(".svg"))  mime = "image/svg+xml";
    else if (file.hasFileExtension(".ttf"))  mime = "font/ttf";
    else if (file.hasFileExtension(".woff2")) mime = "font/woff2";

    const auto* dataPtr = reinterpret_cast<const std::byte*>(mb.getData());
    std::vector<std::byte> bytes(dataPtr, dataPtr + mb.getSize());

    return juce::WebBrowserComponent::Resource{ std::move(bytes), std::move(mime) };
}

MainComponent::MainComponent()
{
    // 1. Build the ToneCard off the message thread; it is installed when done.
    juce::Component::SafePointer<MainComponent> safeThis(this);
    toneCardLoader = std::thread([safeThis]
    {
        auto card = std::make_shared<ToneCard>(SoundEngine::buildDefaultToneCard());
        juce::MessageManager::callAsync([safeThis, card]
        {
            if (safeThis != nullptr)
                safeThis->installToneCard(std::move(*card));
        });
    });

    // 2. The output device opens once the page reports it is on screen (see
    // "uiReady"); the fallback covers a page that never gets that far.
    audioDeviceStateFile = appDataFolder().getChildFile("audio-device.xml");
    juce::Timer::callAfterDelay(5000, [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->openAudioDevice();
    });

    // 3. WebBrowser options with Native Integration
    auto options = juce::WebBrowserComponent::Options{}
       #if JUCE_WINDOWS
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(
            juce::WebBrowserComponent::Options::WinWebView2{}
                .withStatusBarDisabled()
                .withBackgroundColour(juce::Colour(0xFF1E1E1E))
                .withUserDataFolder(webViewDataFolder("Standalone"))
        )
       #endif
        .withNativeIntegrationEnabled(true)
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withInitialisationData("viewWidth", 320)
        .withInitialisationData("viewHeight", 440);

    sampleImporter = std::make_unique<SampleImporter>([this](auto operation) { return withAudioDetached([&] { return operation(soundEngine); }); }, [this] { notifyWebGuiInstruments(); });

    options = options
        .withNativeFunction("detectRootNote", [this](const auto& args, auto done) { sampleImporter->detectPitch(args.isEmpty() ? 0 : (int)args[0], std::move(done)); })
        .withNativeFunction("importSound", [this](const auto& args, auto done) {
            if (!toneCardInstalled) { done(SampleImporter::error("Still loading")); return; }
            if (args.size() >= 2) sampleImporter->drop(args[0].toString(), args[1].toString(), std::move(done));
            else sampleImporter->choose(std::move(done));
        })
        .withNativeFunction("finishImport", [this](const auto& args, auto done) { sampleImporter->finish(!args.isEmpty() && (bool)args[0], std::move(done)); })
        .withNativeFunction("getMasterVolume", [this](const auto&, auto done) { done(soundEngine.getMasterVolume()); })
        .withNativeFunction("getSpeakerSim", [this](const auto&, auto done) { done(soundEngine.isSpeakerSimulationEnabled()); })
        .withNativeFunction("setMasterVolume", [this](const auto& args, auto done) {
            if (!args.isEmpty()) soundEngine.setMasterVolume((float)args[0]);
            done(soundEngine.getMasterVolume());
        })
        .withNativeFunction("setUiScale", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            // The page offers only sizes that give whole device pixels per art pixel.
            const double percent = args.isEmpty() ? 100.0 : static_cast<double>(args[0]);
            if (!(percent >= 40.0 && percent <= 400.0)) { done(false); return; }
            const int width = juce::roundToInt(320.0 * percent / 100.0), height = juce::roundToInt(440.0 * percent / 100.0);
            // Posted, not done here: this runs inside WebView2's message handler, and a resize from there can be
            // dropped by WebView2 (window resized, page left at its old size in the corner).
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe, width, height]
            {
                if (safe == nullptr) return;
                if (auto* window = safe->findParentComponentOfClass<juce::DocumentWindow>())
                    window->setContentComponentSize(width, height);
                else safe->setSize(width, height);
            });
            done(true);
        })
        .withNativeFunction("getViewSize", [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            // The size the page should be filling, so the page can tell when WebView2 has not caught up.
            auto* size = new juce::DynamicObject();
            size->setProperty("w", getWidth()); size->setProperty("h", getHeight());
            done(juce::var(size));
        })
        .withNativeFunction("focusWebView", [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->focusWebView(); });
            done(true);
        })
        .withNativeFunction("repairWebView", [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->settleWebView(true); });
            done(true);
        })
        .withNativeFunction("setResizeSteps", [](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            done(true); // the standalone window is sized by the page through setUiScale only
        })
        .withNativeFunction("setDemoWindow", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            // Demo recording mode: nearly the whole display, then back to exactly where it was.
            const bool enable = !args.isEmpty() && (bool)args[0];
            auto* window = findParentComponentOfClass<juce::DocumentWindow>();
            if (window == nullptr) { done(false); return; }
            if (enable)
            {
                if (!demoWindowActive) { windowBoundsBeforeDemo = window->getBounds(); demoWindowActive = true; }
                const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(window->getBounds());
                const auto area = display != nullptr ? display->userArea
                                                     : juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
                window->setBounds(area.reduced(12));
            }
            else if (demoWindowActive)
            {
                window->setBounds(windowBoundsBeforeDemo);
                demoWindowActive = false;
            }
            done(true);
        })
        .withNativeFunction("getNoteGate", [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            done(soundEngine.getDisplayInputState());
        })
        .withNativeFunction("getLicenseStatus", [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            juce::Component::SafePointer<MainComponent> safeThis(this);
            if (!juce::Thread::launch([safeThis, done]() mutable {
                auto result = LicenseClient::checkStatus();
                juce::MessageManager::callAsync([safeThis, result = std::move(result), done = std::move(done)]() mutable {
                    if (safeThis != nullptr) done(std::move(result));
                });
            })) done(makeGenerationError("Service busy"));
        })
        .withNativeFunction("submitFeedback", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            const auto id = args.size() > 0 ? args[0].toString() : juce::String();
            const auto message = args.size() > 1 ? args[1].toString() : juce::String();
            const int choice = args.size() > 2 ? (int)args[2] : -1;
            juce::Component::SafePointer<MainComponent> safeThis(this);
            if (!juce::Thread::launch([safeThis, id, message, choice, done]() mutable {
                auto result = LicenseClient::submitFeedback(id, message, choice);
                juce::MessageManager::callAsync([safeThis, result = std::move(result), done = std::move(done)]() mutable {
                    if (safeThis != nullptr) done(std::move(result));
                });
            })) done(makeGenerationError("Service busy"));
        })
        .withNativeFunction("activateLicense", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done) {
            const auto code = args.isEmpty() ? juce::String() : args[0].toString();
            juce::Component::SafePointer<MainComponent> safeThis(this);
            if (!juce::Thread::launch([safeThis, code, done]() mutable {
                auto result = LicenseClient::activate(code);
                juce::MessageManager::callAsync([safeThis, result = std::move(result), done = std::move(done)]() mutable {
                    if (safeThis != nullptr) done(std::move(result));
                });
            })) done(makeGenerationError("Service busy"));
        })
        .withNativeFunction("getInstruments", [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(soundEngine.getInstrumentLibrary());
        })
        .withNativeFunction("getStartupStatus", [this](const auto&, auto done) {
            auto* status = new juce::DynamicObject();
            status->setProperty("library", toneCardInstalled);
            status->setProperty("audio", audioDeviceOpened);
            status->setProperty("engineRate", kEngineSampleRate);
            if(auto* device=audioDeviceManager.getCurrentAudioDevice()){
                status->setProperty("outputRate", device->getCurrentSampleRate());
                status->setProperty("blockSize", device->getCurrentBufferSizeSamples());
                status->setProperty("channels", device->getActiveOutputChannels().countNumberOfSetBits());
            }
            done(juce::var(status));
        })
        .withNativeFunction("getAudioOutputs", [this](const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(getAudioOutputs());
        })
        .withNativeFunction("setAudioOutput", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(selectAudioOutput(args.isEmpty() ? juce::String() : args[0].toString()));
        })
        .withNativeFunction("getVoiceParams", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            const int index = args.isEmpty() ? 0 : (int)args[0];
            const auto p = soundEngine.getVoiceParams(index);
            juce::Array<juce::var> values { p.brightness, p.attack, p.release, p.detune, p.noise, p.quality, p.formant };
            completion(juce::var(values));
        })
        .withNativeFunction("getInstrumentEditorData", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            completion(soundEngine.getInstrumentEditorData(args.isEmpty() ? 0 : (int)args[0]));
        })
        .withNativeFunction("setInstrumentEditorControls", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() < 4) { completion(makeGenerationError("Invalid editor controls")); return; }
            const int index = (int)args[0];
            auto result = soundEngine.setInstrumentEditorControls(index, (float)args[1], (float)args[2], (int)args[3], args.size() > 4 ? (float)args[4] : NAN, args.size() > 5 ? ((bool)args[5] ? 1 : 0) : -1);
            if ((bool)result["ok"]) persistInstrumentSettings(index);
            completion(std::move(result));
        })
        .withNativeFunction("setInstrumentLoop", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() < 2) { completion(makeGenerationError("Invalid loop selection")); return; }
            const int index = (int)args[0];
            auto result = soundEngine.setInstrumentLoop(index, (int)args[1]);
            if ((bool)result["ok"]) persistInstrumentSettings(index);
            completion(std::move(result));
        })
        .withNativeFunction("renameInstrument", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() < 2) { completion(makeGenerationError("Invalid name")); return; }
            const int index = (int)args[0];
            auto result = soundEngine.renameInstrument(index, args[1].toString());
            if ((bool)result["ok"])
            {
                persistInstrumentSettings(index);
                notifyWebGuiInstruments();
            }
            completion(std::move(result));
        })
        .withNativeFunction("generateSoundEffect", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            generateSoundEffect(args.isEmpty() ? juce::String() : args[0].toString(),
                                args.size() > 1 ? (int)args[1] : 1,
                                args.size() > 2 ? args[2] : juce::var(), std::move(completion));
        })
        .withNativeFunction("setGeneratedSampleControls", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.size() < 4) { completion(makeGenerationError("Invalid controls")); return; }
            const int index = (int)args[0];
            auto result = soundEngine.setGeneratedSampleControls(index, (int)args[1], (bool)args[2], (bool)args[3]);
            if ((bool)result["ok"]) persistInstrumentSettings(index);
            completion(std::move(result));
        })
        .withNativeFunction("deleteGeneratedSample", [this](const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            const int index = args.isEmpty() ? -1 : (int)args[0];
            auto result = withAudioDetached([&] { return soundEngine.deleteGeneratedSample(index); });
            if ((bool)result["ok"])
            {
                const auto processed = result["processedFile"].toString();
                const auto original = result["originalFile"].toString();
                juce::Thread::launch([processed, original] {
                    if (processed.isNotEmpty())
                    {
                        const juce::File file(processed);
                        file.deleteFile();
                        file.getSiblingFile(file.getFileName() + ".2g.json").deleteFile();
                    }
                    if (original.isNotEmpty()) juce::File(original).deleteFile();
                });
                notifyWebGuiInstruments();
            }
            completion(std::move(result));
        })
        .withEventListener("uiReady", [this](const juce::var&) { openAudioDevice(); })
        .withEventListener("setMidiOptions", [this](const juce::var& data) {
            soundEngine.setVelocityTouch(!data.hasProperty("touch") || (bool)data["touch"]);
            soundEngine.setBendRange(data.hasProperty("bend") ? (float)data["bend"] : 2.0f);
        })
        .withEventListener("noteOn", [this](const juce::var& data) {
            int note = (int)data["note"];
            float vel = data.hasProperty("velocity") ? (float)data["velocity"] : 0.9f;
            soundEngine.noteOn(note, vel);
        })
        .withEventListener("noteOff", [this](const juce::var& data) {
            int note = (int)data["note"];
            soundEngine.noteOff(note);
        })
        .withEventListener("allNotesOff", [this](const juce::var&) {soundEngine.allNotesOff();})
        .withEventListener("selectVoice", [this](const juce::var& data) {
            int index = (int)data["index"];
            soundEngine.selectVoice(index);
        })
        .withEventListener("previewVoice", [this](const juce::var& data) {
            int index = (int)data["index"];
            soundEngine.previewVoice(index);
        })
        .withEventListener("toggleSpeaker", [this](const juce::var&) {
            soundEngine.toggleSpeakerSimulation();
        })
        .withEventListener("toggleMovement", [this](const juce::var&) {
            soundEngine.toggleMovement();
        })
        .withEventListener("setVoiceParam", [this](const juce::var& data) {
            const int index = (int)data["instrument"];
            soundEngine.setVoiceParam(index, (int)data["parameter"], (float)data["value"]);
            persistInstrumentSettings(index);
        })
        .withResourceProvider([](const juce::String& url) {
            return handleResource(url);
        });

    webView = std::make_unique<juce::WebBrowserComponent>(options);
    addAndMakeVisible(*webView);
    devShortcutWatcher = std::make_unique<DevShortcutWatcher>(*webView);

    setSize(320, 440);

    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
}

MainComponent::~MainComponent()
{
    devShortcutWatcher = nullptr;
    sampleImporter = nullptr;
    // The loader only posts back through a SafePointer, so waiting for it is enough.
    if (toneCardLoader.joinable())
        toneCardLoader.join();
    audioDeviceManager.removeAudioCallback(&soundEngine);
    for (auto& device : juce::MidiInput::getAvailableDevices())
    {
        audioDeviceManager.removeMidiInputCallback(device.identifier, &soundEngine);
    }
    webView = nullptr;
}

void MainComponent::installToneCard(ToneCard card)
{
    soundEngine.installToneCard(std::move(card));
    toneCardInstalled = true;
    attachAudioWhenReady();
    notifyWebGuiInstruments();
}

void MainComponent::openAudioDevice()
{
    if (audioDeviceOpened)
        return;
    audioDeviceOpened = true;

    // Restore the standalone output choice, falling back to the system
    // default when the saved device is no longer present.
    std::unique_ptr<juce::XmlElement> savedAudioState;
    if (audioDeviceStateFile.existsAsFile())
        savedAudioState = juce::XmlDocument::parse(audioDeviceStateFile);
    audioDeviceManager.initialise(0, 2, savedAudioState.get(), true);
    attachAudioWhenReady();
}

void MainComponent::attachAudioWhenReady()
{
    if (!toneCardInstalled || !audioDeviceOpened || audioAttached)
        return;
    audioAttached = true;
    audioDeviceManager.addAudioCallback(&soundEngine);

    for (auto& device : juce::MidiInput::getAvailableDevices())
    {
        audioDeviceManager.setMidiInputDeviceEnabled(device.identifier, true);
        audioDeviceManager.addMidiInputCallback(device.identifier, &soundEngine);
    }
}

// Instrument vectors are referenced directly by active voices, so the audio
// callback must be detached while fn replaces or removes one.
template <typename Fn>
juce::var MainComponent::withAudioDetached(Fn&& fn)
{
    if (audioAttached) audioDeviceManager.removeAudioCallback(&soundEngine);
    auto result = fn();
    if (audioAttached) audioDeviceManager.addAudioCallback(&soundEngine);
    return result;
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E1E));
}

void MainComponent::resized()
{
    if (webView != nullptr)
    {
        // The web view stays the size of the whole desktop and the window shows its top-left part. Resizing
        // WebView2 along with the window left it stuck at an old size, so after the first layout it is never resized;
        // the page is told the visible size instead (initialisation data, then the viewSize event).
       #if JUCE_WINDOWS
        const auto desk = largestDisplaySize();
        webView->setBounds(0, 0, std::max(getWidth(), desk.x), std::max(getHeight(), desk.y));
       #else
        webView->setBounds(getLocalBounds()); // WKWebView resizes with its window without trouble
       #endif
        auto* size = new juce::DynamicObject();
        size->setProperty("w", getWidth()); size->setProperty("h", getHeight());
        webView->emitEventIfBrowserIsVisible("viewSize", juce::var(size));
    }
}

void MainComponent::focusWebView()
{
    if (webView != nullptr && !webView->hasKeyboardFocus(true))
        webView->grabKeyboardFocus();
}

void MainComponent::settleWebView(bool hard)
{
    if (webView == nullptr)
        return;
    const auto bounds = getLocalBounds();
    if (hard)
        webView->setVisible(false); // hiding and showing makes WebView2 lay its page out again
    webView->setBounds(bounds.withTrimmedRight(1));
    if (hard)
        webView->setVisible(true);
    webView->setBounds(bounds);
}

void MainComponent::notifyWebGuiInstruments()
{
    if (webView == nullptr)
        return;

    webView->emitEventIfBrowserIsVisible("instrumentsUpdated", soundEngine.getInstrumentLibrary());
}

juce::var MainComponent::getAudioOutputs()
{
    auto* result = new juce::DynamicObject();
    juce::Array<juce::var> outputs;
    auto* type = audioDeviceOpened ? audioDeviceManager.getCurrentDeviceTypeObject() : nullptr;
    if (type != nullptr)
    {
        type->scanForDevices();
        for (const auto& name : type->getDeviceNames(false)) outputs.add(name);
        result->setProperty("type", type->getTypeName());
    }
    const auto* current = audioDeviceManager.getCurrentAudioDevice();
    result->setProperty("ok", type != nullptr);
    result->setProperty("current", current != nullptr ? current->getName() : juce::String());
    result->setProperty("outputs", juce::var(outputs));
    return juce::var(result);
}

juce::var MainComponent::selectAudioOutput(const juce::String& deviceName)
{
    auto* result = new juce::DynamicObject();
    auto* type = audioDeviceOpened ? audioDeviceManager.getCurrentDeviceTypeObject() : nullptr;
    if (type == nullptr || deviceName.isEmpty())
    {
        result->setProperty("ok", false);
        result->setProperty("error", "No audio output");
        return juce::var(result);
    }

    type->scanForDevices();
    if (!type->getDeviceNames(false).contains(deviceName))
    {
        result->setProperty("ok", false);
        result->setProperty("error", "Output unavailable");
        return juce::var(result);
    }

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    audioDeviceManager.getAudioDeviceSetup(setup);
    setup.outputDeviceName = deviceName;
    setup.useDefaultOutputChannels = true;
    const auto error = audioDeviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
    {
        result->setProperty("ok", false);
        result->setProperty("error", error);
        return juce::var(result);
    }

    saveAudioDeviceState();
    result->setProperty("ok", true);
    result->setProperty("current", deviceName);
    return juce::var(result);
}

void MainComponent::saveAudioDeviceState()
{
    if (auto state = audioDeviceManager.createStateXml())
    {
        audioDeviceStateFile.getParentDirectory().createDirectory();
        state->writeTo(audioDeviceStateFile);
    }
}

void MainComponent::persistInstrumentSettings(int instrumentIndex)
{
    const auto path = soundEngine.getInstrumentSettingsPath(instrumentIndex);
    const auto json = soundEngine.getInstrumentSettingsJson(instrumentIndex);
    if (path.isEmpty() || json.isEmpty()) return;

    // Coalesce continuous mouse movement without touching the audio thread.
    // Older delayed callbacks drop stale snapshots; the last one writes in a
    // worker after the control has settled.
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::Timer::callAfterDelay(140, [safeThis, instrumentIndex, path, json]
    {
        if (safeThis == nullptr
            || safeThis->soundEngine.getInstrumentSettingsJson(instrumentIndex) != json)
            return;
        juce::Thread::launch([path, json] { juce::File(path).replaceWithText(json); });
    });
}

void MainComponent::generateSoundEffect(const juce::String& rawPrompt,
                                        int promptStrength,
                                        const juce::var& options,
                                        juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    const auto prompt = rawPrompt.trim().substring(0, 450);
    if (!toneCardInstalled)
    {
        // The card still loading from disk would replace the new sample.
        completion(makeGenerationError("Still loading"));
        return;
    }
    if (prompt.isEmpty())
    {
        completion(makeGenerationError("Write something"));
        return;
    }

    const bool forceUnpitched = prompt.containsWholeWordIgnoreCase("chord");
    const auto apiPrompt = GenerationPrompt::build(prompt, promptStrength);
    if (apiPrompt.length() > 450)
    {
        completion(makeGenerationError("Shorten prompt"));
        return;
    }
    const auto generationOptions = GenerationOptions::fromVar(options);
    const float promptInfluence = generationOptions.influence >= 0.0f ? generationOptions.influence : loadPromptInfluence();

    juce::Component::SafePointer<MainComponent> safeThis(this);
    const bool launched = juce::Thread::launch([safeThis, prompt, apiPrompt, forceUnpitched, promptInfluence, generationOptions,
                                                completion = std::move(completion)]() mutable
    {
        juce::String error;
        auto audio = LicenseClient::generate(apiPrompt, promptInfluence, error, generationOptions);
        PreparedGeneratedSample prepared;
        if (error.isEmpty())
        {
            prepared = ToneCard::prepareGeneratedSample(audio, prompt, forceUnpitched, false);
            if (prepared.error.isNotEmpty()) error = prepared.error;
            else SoundEngine::calibratePrepared(prepared);
        }

        juce::MessageManager::callAsync([safeThis, prepared = std::move(prepared), error,
                                         completion = std::move(completion)]() mutable
        {
            if (safeThis == nullptr) return;
            if (error.isNotEmpty())
            {
                completion(makeGenerationError(error));
                return;
            }

            auto result = safeThis->sampleImporter->install(std::move(prepared));
            if ((bool)result["ok"])
            {
                safeThis->soundEngine.selectVoice((int)result["index"]);
                safeThis->persistInstrumentSettings((int)result["index"]);
                safeThis->notifyWebGuiInstruments();
            }
            completion(std::move(result));
        });
    });

    if (!launched)
        completion(makeGenerationError("Engine busy"));
}
