#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

// WebView2 can consume browser accelerator chords before a DOM keydown reaches
// the page. Observe this development shortcut on the native message thread too.
class DevShortcutWatcher final : private juce::Timer
{
public:
    explicit DevShortcutWatcher(juce::WebBrowserComponent& view) : browser(view)
    {
        startTimerHz(60);
    }
    ~DevShortcutWatcher() override { stopTimer(); }

private:
    void timerCallback() override
    {
        const auto modifiers = juce::ModifierKeys::getCurrentModifiersRealtime();
        const bool held = modifiers.isCtrlDown() && modifiers.isShiftDown()
            && !modifiers.isAltDown() && juce::KeyPress::isKeyCurrentlyDown('E');
        if (held && !wasHeld && browser.isShowing() && juce::Process::isForegroundProcess())
            browser.emitEventIfBrowserIsVisible("previewMascotGlitch", juce::var());
        wasHeld = held;
        const bool messageHeld = modifiers.isCtrlDown() && modifiers.isShiftDown()
            && !modifiers.isAltDown() && juce::KeyPress::isKeyCurrentlyDown('M');
        if (messageHeld && !wasMessageHeld && browser.isShowing() && juce::Process::isForegroundProcess())
            browser.emitEventIfBrowserIsVisible("previewInboxMessage", juce::var());
        wasMessageHeld = messageHeld;
        const bool demoHeld = modifiers.isCtrlDown() && modifiers.isShiftDown()
            && !modifiers.isAltDown() && juce::KeyPress::isKeyCurrentlyDown('D');
        if (demoHeld && !wasDemoHeld && browser.isShowing() && juce::Process::isForegroundProcess())
            browser.emitEventIfBrowserIsVisible("toggleDemoMode", juce::var());
        wasDemoHeld = demoHeld;
    }
    juce::WebBrowserComponent& browser;
    bool wasHeld = false;
    bool wasMessageHeld = false;
    bool wasDemoHeld = false;
};
