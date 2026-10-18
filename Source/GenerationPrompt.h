#pragma once
#include <juce_core/juce_core.h>

namespace GenerationPrompt
{
inline juce::String build(const juce::String& prompt, int strength)
{
    strength = juce::jlimit(0, 2, strength);
    if (strength == 0) return prompt;
    const bool chord = prompt.containsWholeWordIgnoreCase("chord");
    const bool space = prompt.containsWholeWordIgnoreCase("reverb")
        || prompt.containsWholeWordIgnoreCase("cave") || prompt.containsWholeWordIgnoreCase("hall")
        || prompt.containsWholeWordIgnoreCase("cathedral") || prompt.containsWholeWordIgnoreCase("room")
        || prompt.containsWholeWordIgnoreCase("underwater");
    juce::StringArray terms;
    if (strength == 1)
    {
        // One tonal, held note is the point (a sampler needs a pitch); the register and character are left to the
        // model, so the same prompt still gives different sounds.
        terms.add(chord ? "one sustained chord" : "one single sustained musical note");
        if (!chord) terms.add("clear stable pitch in the natural register");
        terms.addArray({ "clear attack", "steady sustain", "natural release" });
        if (!space) terms.add("dry studio recording");
        terms.addArray({ "no melody", "no repeated notes" });
    }
    else
    {
        terms.add("one isolated playable sampler source");
        if (!chord) terms.add("single sustained monophonic note in a comfortable natural register");
        terms.addArray({ "stable fundamental pitch", "clear natural attack", "long steady sustain section",
                        "consistent tone and loudness", "complete natural release", "centered and close" });
        if (!space) terms.add("studio-dry");
        terms.add("no melody");
        if (!chord) terms.add("no chord");
        terms.addArray({ "no repeated notes", "no demonstration", "no layered background",
                        "compact timbre with a clear readable midrange" });
    }
    return prompt + ", " + terms.joinIntoString(", ");
}
}
