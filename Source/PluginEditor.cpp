#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// The utility panel ends at 374 + 460 = 834 and the waveform selector is the
// last 25 px of it, so the window needs a little room below that - at a
// fractional scale factor a 6 px margin rounds away and clips the bar.
constexpr int editorHeight = 850;
constexpr int editorWidth = 520;

static void drawGrainTexture(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    WXGrain::draw(g, bounds, 0.04f);
}
}

StableAliveAudioProcessorEditor::StableAliveAudioProcessorEditor (StableAliveAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), presetManager(p.presetManager), presetPanel(presetManager)
{
    initKeyMap();
    addKeyListener(this);

    // Create radar display and connect callbacks
    radarDisplay = std::make_unique<WeatherRadarDisplay>(audioProcessor.apvts, audioProcessor, [this]()
    {
        if (isTrailerMode)
        {
            auto allPresets = presetManager.getPresetList();
            juce::StringArray numericPresets;
            for (auto& preset : allPresets)
            {
                if (preset.containsOnly("0123456789") && preset.isNotEmpty())
                    numericPresets.add(preset);
            }

            if (numericPresets.size() > 0)
            {
                numericPresets.sort(true);
                auto presetToLoad = numericPresets[(size_t)trailerPresetIndex % (size_t)numericPresets.size()];
                trailerPresetIndex++;

                presetManager.loadPreset(presetToLoad);
                presetPanel.refreshPresetName();

                float seedVal = audioProcessor.apvts.getRawParameterValue("RANDOM_SEED")->load();
                audioProcessor.prepareWavetablesForSeed(seedVal);
                audioProcessor.seedGenerator.setSeedFromFloat(seedVal);

                // Use false for isPresetChange to show "RANDOMIZING..." on the radar screen
                radarDisplay->regenerateWeather(audioProcessor.seedGenerator.getSeedHash(), false);
                radarDisplay->setSeedCode(audioProcessor.seedGenerator.getSeedCode());

                if (utilityPanel)
                    utilityPanel->triggerRandomizationAnimation();
                return;
            }
        }

        // Save current patch state to undo history before randomizing
        patchHistory.push_back(audioProcessor.apvts.copyState());
        if (patchHistory.size() > 50)
            patchHistory.erase(patchHistory.begin());

        audioProcessor.seedGenerator.generateNewSeed();
        auto patch = audioProcessor.seedGenerator.generatePatch();
        if (utilityPanel)
            utilityPanel->triggerRandomizationAnimation();
        audioProcessor.writePatchToParameters(patch);
        
        radarDisplay->regenerateWeather(audioProcessor.seedGenerator.getSeedHash(), false);
        radarDisplay->setSeedCode(audioProcessor.seedGenerator.getSeedCode());
    });
    radarDisplay->setNoteReactionSource(&audioProcessor.noteReactionPulse);

    utilityPanel = std::make_unique<UtilityPanel>(audioProcessor.apvts, audioProcessor);
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this);
    tooltipWindow->setLookAndFeel(&tooltipLaf);
    tooltipWindow->setMillisecondsBeforeTipAppears(50);

    // Setup minimalist Master Volume Slider
    masterGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    masterGainSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterGainSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xFF151515));
    masterGainSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xFF2E2E2E));
    masterGainSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xFFD9D9D9));
    addAndMakeVisible(masterGainSlider);
    masterGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "GAIN", masterGainSlider);

    addAndMakeVisible(*radarDisplay);
    addAndMakeVisible(*utilityPanel);
    addAndMakeVisible(logoComponent);
    addAndMakeVisible(presetPanel);

    // Initial weather seed setup
    radarDisplay->regenerateWeather(audioProcessor.seedGenerator.getSeedHash());
    radarDisplay->setSeedCode(audioProcessor.seedGenerator.getSeedCode());

    // Persistent window scaling via processor cached uiScale (zero disk I/O on UI thread!)
    double savedScale = (double) audioProcessor.uiScale;
    if (savedScale < 0.5 || savedScale > 2.0)
        savedScale = 1.0;
    
    logoComponent.currentScale = savedScale;
    setScaleFactor(savedScale);

    logoComponent.onScaleChange = [this](double newScale)
    {
        audioProcessor.uiScale = (float)newScale;
        setScaleFactor((float)newScale);

        // Nudge the size so the host re-queries it, then settle on the real one.
        setSize(editorWidth + 1, editorHeight);
        setSize(editorWidth, editorHeight);

        // NOTE: do not touch the peer here. setScaleFactor already sizes the
        // native window as logicalSize * scale * the display's own scale. Forcing
        // it to editorWidth * scale ignores that display scale, so on a 125% or
        // 150% Windows display the window came out too small and the bottom of
        // the panel - the waveform selector - was cut off.
        
        juce::PropertiesFile::Options opts;
        opts.applicationName = "WX";
        opts.filenameSuffix = "settings";
        opts.folderName = "Scrap Brain Audio";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        
        juce::ApplicationProperties appProps;
        appProps.setStorageParameters(opts);
        if (auto* props = appProps.getUserSettings())
        {
            props->setValue("uiScale", newScale);
            props->saveIfNeeded();
        }
    };

    // Preset loaded callback to trigger radar display update with "RECONFIGURING..."
    presetPanel.onPresetLoaded = [this]()
    {
        float seedVal = audioProcessor.apvts.getRawParameterValue("RANDOM_SEED")->load();
        audioProcessor.prepareWavetablesForSeed(seedVal);
        audioProcessor.seedGenerator.setSeedFromFloat(seedVal);
        radarDisplay->regenerateWeather(audioProcessor.seedGenerator.getSeedHash(), true);
        radarDisplay->setSeedCode(audioProcessor.seedGenerator.getSeedCode());
        if (utilityPanel)
            utilityPanel->triggerRandomizationAnimation();
    };

    setWantsKeyboardFocus(true);
    setSize(editorWidth, editorHeight);

    // DRM: Check license and show activation screen if needed.
    // Refresh here (message thread) so an activation done in another instance or
    // in the standalone app is picked up when this editor opens.
    LicenseManager::refreshCache();
    if (!LicenseManager::isLicensed())
    {
        // Hide all main UI components
        radarDisplay->setVisible(false);
        utilityPanel->setVisible(false);
        logoComponent.setVisible(false);
        presetPanel.setVisible(false);
        masterGainSlider.setVisible(false);
        licenseScreen = std::make_unique<LicenseScreen>();
        licenseScreen->onActivated = [this]() { showMainUI(); };
        addAndMakeVisible(*licenseScreen);
        licenseScreen->setBounds(getLocalBounds());
    }
    else
    {
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<StableAliveAudioProcessorEditor>(this)]()
        {
            if (safeThis != nullptr)
                safeThis->grabKeyboardFocus();
        });
    }
}

void StableAliveAudioProcessorEditor::showMainUI()
{
    if (licenseScreen)
    {
        removeChildComponent(licenseScreen.get());
        licenseScreen.reset();
    }

    radarDisplay->setVisible(true);
    utilityPanel->setVisible(true);
    logoComponent.setVisible(true);
    presetPanel.setVisible(true);
    masterGainSlider.setVisible(true);
    repaint();

    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<StableAliveAudioProcessorEditor>(this)]()
    {
        if (safeThis != nullptr)
            safeThis->grabKeyboardFocus();
    });
}

StableAliveAudioProcessorEditor::~StableAliveAudioProcessorEditor()
{
    removeKeyListener(this);
    tooltipWindow->setLookAndFeel(nullptr);
}

void StableAliveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF0E1116)); // Cool dark background matching utility panel
    drawGrainTexture(g, getLocalBounds());

    // Draw minimalist Main Volume label using matching cool slate text color
    g.setFont(WXFonts::get(13.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xFFB8C8DB));
    g.drawText("MAIN VOL", 255, 14, 100, 20, juce::Justification::centredRight);
}

void StableAliveAudioProcessorEditor::resized()
{
    radarDisplay->setBounds(27, 48, 466, 318);
    utilityPanel->setBounds(27, 374, 466, 460);
    masterGainSlider.setBounds(365, 14, 128, 20);
    logoComponent.setBounds(27, 8, 70, 36);
    presetPanel.setBounds(105, 13, 175, 24);
}

void StableAliveAudioProcessorEditor::showLicenseScreen()
{
    if (licenseScreen)
        return;

    radarDisplay->setVisible(false);
    utilityPanel->setVisible(false);
    logoComponent.setVisible(false);
    presetPanel.setVisible(false);
    masterGainSlider.setVisible(false);

    licenseScreen = std::make_unique<LicenseScreen>();
    licenseScreen->onActivated = [this]() { showMainUI(); };
    addAndMakeVisible(*licenseScreen);
    licenseScreen->setBounds(getLocalBounds());
}

void StableAliveAudioProcessorEditor::initKeyMap()
{
    keyMap.push_back({'A', 60});
    keyMap.push_back({'S', 62});
    keyMap.push_back({'D', 64});
    keyMap.push_back({'F', 65});
    keyMap.push_back({'G', 67});
    keyMap.push_back({'H', 69});
    keyMap.push_back({'J', 71});
    keyMap.push_back({'K', 72});
    keyMap.push_back({'L', 74});

    keyMap.push_back({'W', 61});
    keyMap.push_back({'E', 63});
    keyMap.push_back({'T', 66});
    keyMap.push_back({'Y', 68});
    keyMap.push_back({'U', 70});
    keyMap.push_back({'O', 73});
    keyMap.push_back({'P', 75});
}

bool StableAliveAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    int keyCode = key.getKeyCode();

    // Ctrl + Shift + T opens License Activation Screen to enter/change license key!
    if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown() && (keyCode == 'T' || keyCode == 't'))
    {
        showLicenseScreen();
        return true;
    }

    if (keyCode == juce::KeyPress::F11Key)
    {
        float scale = 3.0f; // 3x scaling for ultra-crisp vector export
        int radarW = radarDisplay->getWidth();
        int radarH = radarDisplay->getHeight();
        
        juce::Image img(juce::Image::ARGB, 
                        (int)(radarW * scale), 
                        (int)(radarH * scale), 
                        true);
        
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(scale));
        radarDisplay->paintEntireComponent(g, true);
        
        auto desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
        auto filename = "StableAlive_Radar_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S") + ".png";
        auto file = desktop.getChildFile(filename);
        
        juce::PNGImageFormat png;
        if (auto out = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
        {
            png.writeImageToStream(img, *out);
        }
        return true;
    }

    if (keyCode == juce::KeyPress::F12Key)
    {
        float scale = 3.0f; // 3x scaling for ultra-crisp vector export
        juce::Image img(juce::Image::ARGB, 
                        (int)(getWidth() * scale), 
                        (int)(getHeight() * scale), 
                        true);
        
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(scale));
        paintEntireComponent(g, true);
        
        auto desktop = juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
        auto filename = "StableAlive_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S") + ".png";
        auto file = desktop.getChildFile(filename);
        
        juce::PNGImageFormat png;
        if (auto out = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
        {
            png.writeImageToStream(img, *out);
        }
        return true;
    }

    // Ctrl + Z / Cmd + Z: Undo last random patch
    if (key.getModifiers().isCommandDown() && (keyCode == 'Z' || keyCode == 'z'))
    {
        undoLastRandomPatch();
        return true;
    }

    if (keyCode == 'Z' || keyCode == 'z')
    {
        octaveOffset = juce::jlimit(-36, 36, octaveOffset - 12);
        return true;
    }
    if (keyCode == 'X' || keyCode == 'x')
    {
        octaveOffset = juce::jlimit(-36, 36, octaveOffset + 12);
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        octaveOffset = juce::jlimit(-36, 36, octaveOffset + 12);
        return true;
    }
    if (key == juce::KeyPress::downKey)
    {
        octaveOffset = juce::jlimit(-36, 36, octaveOffset - 12);
        return true;
    }

    return false;
}

bool StableAliveAudioProcessorEditor::keyStateChanged(bool, juce::Component*)
{
    for (auto& kn : keyMap)
    {
        bool nowDown = juce::KeyPress::isKeyCurrentlyDown(kn.keyCode);

        if (nowDown && !kn.isDown)
        {
            kn.isDown = true;
            kn.activeMidiNote = kn.midiNote + octaveOffset;
            audioProcessor.getSynth().noteOn(1, kn.activeMidiNote, 0.8f);
            audioProcessor.noteReactionPulse.store(0.85f);
        }
        else if (!nowDown && kn.isDown)
        {
            kn.isDown = false;
            if (kn.activeMidiNote != -1)
            {
                audioProcessor.getSynth().noteOff(1, kn.activeMidiNote, 0.0f, true);
                kn.activeMidiNote = -1;
            }
        }
    }
    return true;
}

void StableAliveAudioProcessorEditor::undoLastRandomPatch()
{
    if (patchHistory.empty())
        return;

    auto prevState = patchHistory.back();
    patchHistory.pop_back();

    audioProcessor.apvts.replaceState(prevState);

    float seedVal = 0.5f;
    if (auto* seedParam = audioProcessor.apvts.getRawParameterValue("RANDOM_SEED"))
        seedVal = seedParam->load();
    audioProcessor.prepareWavetablesForSeed(seedVal);
    audioProcessor.seedGenerator.setSeedFromFloat(seedVal);

    radarDisplay->regenerateWeather(audioProcessor.seedGenerator.getSeedHash(), true);
    radarDisplay->setSeedCode(audioProcessor.seedGenerator.getSeedCode());

    if (utilityPanel)
        utilityPanel->triggerRandomizationAnimation();
}
