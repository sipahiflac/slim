#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "SoundEngine.h"
#include "SampleImporter.h"
#include <thread>
class DevShortcutWatcher;

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void settleWebView(bool hard = false); // see WebViewSettle.h
    void focusWebView();                   // keyboard focus into the page without a click

    static juce::File getResourcesDirectory();

private:
    void installToneCard(ToneCard card);
    void openAudioDevice();
    void attachAudioWhenReady();
    template <typename Fn> juce::var withAudioDetached(Fn&& fn);
    void notifyWebGuiInstruments();
    void persistInstrumentSettings(int instrumentIndex);
    juce::var getAudioOutputs();
    juce::var selectAudioOutput(const juce::String& deviceName);
    void saveAudioDeviceState();
    void generateSoundEffect(const juce::String& prompt,
                             const juce::var& options,
                             juce::WebBrowserComponent::NativeFunctionCompletion completion);
    static std::optional<juce::WebBrowserComponent::Resource> handleResource(const juce::String& url);

    juce::AudioDeviceManager audioDeviceManager;
    juce::File audioDeviceStateFile;
    SoundEngine soundEngine;

    std::unique_ptr<juce::WebBrowserComponent> webView;
    std::unique_ptr<SampleImporter> sampleImporter;
    std::unique_ptr<DevShortcutWatcher> devShortcutWatcher;

    // Startup runs in pieces so the window appears at once: the tone card is
    // built on toneCardLoader while the page loads, and the output device
    // (a Bluetooth headset takes seconds to wake) opens once the page is up.
    // The engine joins the audio callback only after both have finished.
    std::thread toneCardLoader;
    bool toneCardInstalled = false;
    // Demo recording mode: the window's bounds before it was enlarged.
    juce::Rectangle<int> windowBoundsBeforeDemo;
    bool demoWindowActive = false;
    bool audioDeviceOpened = false;
    bool audioAttached = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
