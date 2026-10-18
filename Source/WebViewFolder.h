#pragma once
#include <juce_core/juce_core.h>

// WebView2 keeps its profile (cache, cookies, crash data) in a user data folder. Left unset it is created
// next to the host executable: read-only under Program Files, and inside the DAW's own folder for the
// plugin, either of which leaves a blank window. Standalone and plugin get separate folders because
// WebView2 only lets processes share one when their environment options match.
// Windows only: WKWebView on macOS keeps its own storage and needs no folder.
#if JUCE_WINDOWS
inline juce::File webViewDataFolder(const char* kind)
{
    auto folder = juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                      .getChildFile("Scrap Brain Audio").getChildFile("2G")
                      .getChildFile("WebView2").getChildFile(kind);
    folder.createDirectory();
    return folder;
}
#endif
