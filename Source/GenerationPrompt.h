#pragma once
#include <juce_core/juce_core.h>
#include <initializer_list>

// The words added after the user's prompt (Options > System: None / Medium / Strong / Auto). Resources/app.js has the
// same rules (generationSuffix) so the character counter shows the real limit; keep the two in step.
namespace GenerationPrompt
{
inline bool hasAny(const juce::String& prompt, std::initializer_list<const char*> words)
{
    for (const auto* word : words)
        if (prompt.containsWholeWordIgnoreCase(word)) return true;
    return false;
}

// Auto looks at WHAT is asked for and picks one of three endings, always starting from the user's own words:
//  - held sounds (pads, drones, leads, bowed and blown instruments, organ): one long held note
//  - struck and plucked sounds (guitar, piano, bells, harp ...): one note ringing out naturally, no forced sustain
//  - anything else: one long held tone at a steady pitch
// Synth words end in "solo synthesizer recording" so an FM bell or a square lead is not rebuilt as an acoustic one.
inline juce::String buildAuto(const juce::String& prompt, bool space)
{
    const bool synth = hasAny(prompt, { "synth", "synthesizer", "synthesiser", "fm", "square", "saw", "sawtooth", "sine",
                                        "digital", "analog", "analogue", "chiptune", "pad", "lead", "drone" });
    const bool heldWord = hasAny(prompt, { "pad", "drone", "sustained", "held", "ambient", "atmosphere", "texture", "hum" });
    const bool struck = hasAny(prompt, { "guitar", "harpsichord", "piano", "bell", "bells", "pluck", "plucked", "harp",
                                         "marimba", "vibraphone", "xylophone", "glockenspiel", "kalimba", "banjo",
                                         "mandolin", "ukulele", "koto", "sitar", "rhodes", "wurlitzer", "clavinet",
                                         "chime", "chimes", "gong", "pizzicato", "stab", "plucky", "plucks", "staccato", "short" });
    const bool heldInstrument = hasAny(prompt, { "cello", "violin", "viola", "strings", "flute", "clarinet", "oboe",
                                                 "saxophone", "sax", "trumpet", "trombone", "horn", "brass", "accordion",
                                                 "harmonium", "harmonica", "theremin", "whistle", "organ", "choir",
                                                 "vocal", "voice", "bass", "synth", "synthesizer", "sine", "saw",
                                                 "sawtooth", "square", "wave" });
    const bool chord = hasAny(prompt, { "chord", "chords", "minor", "major" });

    // The words read like a caption of the clip, not a spec sheet: text-to-sound models match descriptions they were
    // trained on. No "no ..." terms (the encoder keeps "melody" and drops "no"), and the note's own envelope is
    // described in plain words because the source should carry its natural attack and decay.
    juce::StringArray terms;
    if (heldWord || (!struck && heldInstrument))
        terms.addArray({ chord ? "one long held chord" : "one long held note", "steady and sustained" });
    else if (struck)
        terms.addArray({ chord ? "one single chord" : "one single note", "ringing out naturally" });
    else
        terms.addArray({ chord ? "one long held chord" : "one long held musical tone", "steady pitch" });
    terms.add(synth ? "solo synthesizer recording" : space ? "solo recording" : "close-miked solo recording");
    return prompt + ", " + terms.joinIntoString(", ");
}

inline juce::String build(const juce::String& prompt, int strength)
{
    strength = juce::jlimit(0, 3, strength); // 0 None, 1 Medium, 2 Strong, 3 Auto
    if (strength == 0) return prompt;
    const bool chord = prompt.containsWholeWordIgnoreCase("chord");
    const bool space = prompt.containsWholeWordIgnoreCase("reverb")
        || prompt.containsWholeWordIgnoreCase("cave") || prompt.containsWholeWordIgnoreCase("hall")
        || prompt.containsWholeWordIgnoreCase("cathedral") || prompt.containsWholeWordIgnoreCase("room")
        || prompt.containsWholeWordIgnoreCase("underwater");
    if (strength == 3) return buildAuto(prompt, space);
    juce::StringArray terms;
    if (strength == 1)
    {
        // One held, pitched note is the point (a sampler needs a pitch); register and character stay the model's.
        terms.addArray({ chord ? "one long held chord" : "one long held note", "solo" });
        if (!space) terms.add("close-miked");
    }
    else
    {
        // Strong is longer but still a caption: positive words only, the instrument alone, a natural ending.
        terms.addArray({ chord ? "a solo recording of one long sustained chord" : "a solo recording of one long sustained note",
                         "at a steady pitch", "the instrument alone" });
        if (!space) terms.add("close-miked");
        terms.add("ringing out naturally at the end");
    }
    return prompt + ", " + terms.joinIntoString(", ");
}
}
