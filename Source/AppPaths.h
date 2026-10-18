#pragma once
#include <juce_core/juce_core.h>

// Where 2G keeps its small per-user files (audio device choice, licence code on macOS, settings.env). Windows:
// %APPDATA%\Scrap Brain Audio\2G. macOS: ~/Library/Application Support/Scrap Brain Audio/2G (JUCE reports
// ~/Library for userApplicationDataDirectory there, and Application Support is where it belongs).
inline juce::File appDataFolder()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile("Application Support");
   #endif
    return base.getChildFile("Scrap Brain Audio").getChildFile("2G");
}
