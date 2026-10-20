#pragma once

#include <juce_core/juce_core.h>

// Network and credential operations run only on a worker thread. Nothing in
// this class may be called from the audio callback.
// What the Studio plan may tune for a generation. The server only honours these for Studio licences.
struct GenerationOptions
{
    double durationSeconds = 5.0; // sound length, 1-10 s
    float influence = -1.0f;      // prompt_influence 0-1; negative means the built-in default
    bool loop = false;            // ask ElevenLabs for a smoothly looping sound

    static GenerationOptions fromVar(const juce::var& value)
    {
        GenerationOptions result;
        if (!value.isObject()) return result;
        if (value.hasProperty("duration")) result.durationSeconds = juce::jlimit(1.0, 10.0, (double) value["duration"]);
        if (value.hasProperty("influence")) result.influence = juce::jlimit(0.0f, 1.0f, (float) value["influence"]);
        result.loop = value.hasProperty("loop") && (bool) value["loop"];
        return result;
    }
};

class LicenseClient final
{
public:
    static juce::var activate(const juce::String& enteredCode);
    static juce::var checkStatus();
    static juce::var submitFeedback(const juce::String& installationId,
                                    const juce::String& messageId, int choice);
    static juce::MemoryBlock generate(const juce::String& text, float influence, juce::String& error,
                                      const GenerationOptions& options = {});
    // Asks the server whether a newer build is published: { ok, newer, current, latest, notes }. The download page is
    // kept here (never handed to the page), so openUpdatePage() can only open a checked scrapbrainaudio.com link.
    static juce::var checkForUpdate();
    static bool openUpdatePage();

private:
    static juce::String deviceId();
    static juce::String endpoint();
    static juce::String readStoredCode(const juce::String& service);
    static bool storeCode(const juce::String& service, const juce::String& code);
    static juce::var request(const juce::String& service, const juce::String& action,
                             const juce::String& code);
};
