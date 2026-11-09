#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// Network and credential operations run only on a worker thread. Nothing in
// this class may be called from the audio callback.
// What the Studio plan may tune for a generation. The server only honours these for Studio licences.
struct GenerationOptions
{
    double durationSeconds = 5.0; // sound length, 1-10 s
    float influence = -1.0f;      // prompt_influence 0-1; negative means the built-in default
    bool loop = false;            // ask ElevenLabs for a smoothly looping sound
    bool enhance = true;          // let the server rewrite the prompt before it reaches the sound model
    bool photo = false;           // the sound comes from the photo the Camera screen loaded, not from words

    static GenerationOptions fromVar(const juce::var& value)
    {
        GenerationOptions result;
        if (!value.isObject()) return result;
        result.photo = value.hasProperty("photo") && (bool) value["photo"];
        if (value.hasProperty("duration")) result.durationSeconds = juce::jlimit(1.0, 10.0, (double) value["duration"]);
        if (value.hasProperty("influence")) result.influence = juce::jlimit(0.0f, 1.0f, (float) value["influence"]);
        result.loop = value.hasProperty("loop") && (bool) value["loop"];
        result.enhance = !value.hasProperty("enhance") || (bool) value["enhance"];
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
    // Asks the server's small language model to file sound names under a kind (Keys, Strings, Winds ...). Returns
    // { ok, tags } with one kind per name, in order; any failure is { status } and the page keeps its own guess.
    static juce::var categorize(const juce::StringArray& names);
    static juce::MemoryBlock generate(const juce::String& text, float influence, juce::String& error,
                                      const GenerationOptions& options = {}, juce::String* soundKind = nullptr);
    // One request, one credit, up to two sounds: the same words read two ways (plain and alternate caption). Fills
    // `audio` and `kinds` (the server's hit/effect/instrument call per sound) in the order A, B. A server that only
    // knows the single answer yields one entry; none means `error` is set.
    // With `photo` set, that image (JPEG, PNG, GIF or WebP bytes, sent untouched) is the request instead of words, and
    // `captions` gets the instrument description the server wrote for each sound.
    static void generateVariants(const juce::String& text, float influence, juce::String& error,
                                 std::vector<juce::MemoryBlock>& audio, std::vector<juce::String>& kinds,
                                 const GenerationOptions& options = {}, const juce::MemoryBlock* photo = nullptr,
                                 const juce::String& photoType = {}, std::vector<juce::String>* captions = nullptr);
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
                             const juce::String& code, juce::String* issuedCode = nullptr);
    // First launch without a saved code: the server hands this device its own Free licence.
    static juce::var registerFree(const juce::String& service);
};
