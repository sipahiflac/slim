#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The web view is kept as big as the largest single display and never resized (see resized() in the shells), so
// its backing surface is one screen, not every monitor added together.
inline juce::Point<int> largestDisplaySize()
{
    juce::Point<int> size { 640, 480 };
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
        size = { std::max(size.x, display.totalArea.getWidth()), std::max(size.y, display.totalArea.getHeight()) };
    return size;
}

// WebView2 can keep its old size when its window is resized quickly: the window grows, the page stays small in a
// dark field. After a resize, ask the owner to set the web view's bounds again a few times (settleWebView sets them
// one pixel short first, so the value really changes). Harmless when nothing was stale.
template <typename Owner>
void scheduleWebViewSettle(Owner& owner)
{
    juce::Component::SafePointer<Owner> safe(&owner);
    for (const int ms : { 60, 250, 800 })
        juce::Timer::callAfterDelay(ms, [safe] { if (safe != nullptr) safe->settleWebView(); });
}
