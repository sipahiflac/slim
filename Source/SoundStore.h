#pragma once
#include "AppPaths.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

// Where sounds live on disk.
//  - Uploaded: the user's own files, kept as ordinary WAVs in Documents/Scrap Brain Audio/2G/Uploaded.
//  - Generated: not left lying around as .wav files. They are kept in the app's own data folder
//    (Windows %APPDATA%\Scrap Brain Audio\2G\Sounds, macOS ~/Library/Application Support/Scrap Brain Audio/2G/Sounds)
//    with the extension .2gs, so Explorer, Finder and DAWs do not treat them as audio files. The content is still a
//    plain WAV stream: this is tidiness, not protection. Only 2G reads them (openReader looks at the content, not
//    the extension).
namespace SoundStore
{
inline juce::File documentsFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Scrap Brain Audio").getChildFile("2G");
}
inline juce::File uploadedFolder() { return documentsFolder().getChildFile("Uploaded"); }
inline juce::File generatedFolder() { return appDataFolder().getChildFile("Sounds"); }
inline juce::File folderFor(bool uploaded) { return uploaded ? uploadedFolder() : generatedFolder(); }
inline const char* extensionFor(bool uploaded) { return uploaded ? ".wav" : ".2gs"; }
inline bool isStoreFile(const juce::File& file) { return file.isAChildOf(uploadedFolder()) || file.isAChildOf(generatedFolder()); }

// Reads audio from a file whatever its extension.
inline std::unique_ptr<juce::AudioFormatReader> openReader(juce::AudioFormatManager& formats, const juce::File& file)
{
    auto stream = file.createInputStream();
    if (stream == nullptr) return nullptr;
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(std::move(stream)));
}

// Older versions kept generated sounds as .wav in Documents/.../Generated. Moves them (and their editor settings)
// into the generated folder under the new names, then removes the empty old folder. Safe to run on every start.
inline void migrateLegacyGenerated()
{
    const auto legacy = documentsFolder().getChildFile("Generated");
    if (!legacy.isDirectory()) return;
    const auto target = generatedFolder();
    target.createDirectory();
    for (const auto& file : legacy.findChildFiles(juce::File::findFiles, false, "*"))
    {
        const auto name = file.getFileName();
        juce::String newName;
        if (name.endsWithIgnoreCase(".wav.2g.json")) newName = name.dropLastCharacters(12) + ".2gs.2g.json";
        else if (file.hasFileExtension(".wav")) newName = file.getFileNameWithoutExtension() + ".2gs";
        else continue;
        const auto destination = target.getChildFile(newName);
        if (!destination.exists()) file.moveFileTo(destination);
    }
    if (legacy.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) legacy.deleteFile();
}
}
