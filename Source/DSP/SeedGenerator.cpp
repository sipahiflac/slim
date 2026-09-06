#include "SeedGenerator.h"

SeedGenerator::SeedGenerator()
{
    generateNewSeed();
}

void SeedGenerator::generateNewSeed()
{
    juce::uint32 newSeed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    setSeedCode(formatSeedCode(newSeed));
}

juce::String SeedGenerator::formatSeedCode(juce::uint32 seedValue)
{
    juce::uint16 hexPart = static_cast<juce::uint16>(seedValue & 0xFFFF);
    char letterPart = 'A' + (char) ((seedValue >> 16) % 26);
    return juce::String::formatted("RPL-%04X-%c", hexPart, letterPart);
}

SeedGenerator::PatchParameters SeedGenerator::generatePatch()
{
    PatchParameters p;

    // Local PRNG seeded from the atomic hash: no shared mutable state, so this
    // is safe to call from the audio thread.
    juce::Random prng ((juce::int64) currentSeedHash.load(std::memory_order_relaxed));

    // 25 entries: r[24] is read further down (unisonMode). The array used to be
    // sized 24, so that read ran off the end of the stack buffer and unisonMode
    // was decided by whatever happened to sit next to it.
    float r[25];
    for (int i = 0; i < 25; ++i)
        r[i] = prng.nextFloat();

    p.oscMorph = r[0];
    p.detuneAmount = juce::jmap(r[1] * r[1], 0.0f, 0.67f);
    p.subOscLevel = juce::jmap(r[2], 0.0f, 0.6f);
    p.oscMode1 = (SeedGenerator::OscMode)((int)(prng.nextFloat() * 4.0f) % 4);
    p.oscMode2 = (SeedGenerator::OscMode)((int)(prng.nextFloat() * 4.0f) % 4);

    // 1/3 chance -1 octave (MONE), 1/3 chance 0 (NORM), 1/3 chance +1 octave (PONE)
    float octRand = prng.nextFloat();
    if (octRand < 0.3333f)      p.octaveShift = -1;
    else if (octRand < 0.6666f) p.octaveShift = 0;
    else                        p.octaveShift = 1;

    float envType = r[3];
    if (envType < 0.3f)
    {
        p.envAttack = juce::jmap(r[4], 0.001f, 0.02f);
        p.envDecay = juce::jmap(r[5], 0.1f, 0.8f);
        p.envSustain = juce::jmap(r[6], 0.0f, 0.4f);
        p.envRelease = juce::jmap(r[7], 0.05f, 0.5f);
    }
    else if (envType < 0.65f)
    {
        p.envAttack = juce::jmap(r[4], 0.01f, 0.15f);
        p.envDecay = juce::jmap(r[5], 0.2f, 1.5f);
        p.envSustain = juce::jmap(r[6], 0.5f, 1.0f);
        p.envRelease = juce::jmap(r[7], 0.3f, 2.0f);
    }
    else
    {
        p.envAttack = juce::jmap(r[4], 0.3f, 3.0f);
        p.envDecay = juce::jmap(r[5], 0.5f, 2.0f);
        p.envSustain = juce::jmap(r[6], 0.6f, 1.0f);
        p.envRelease = juce::jmap(r[7], 1.0f, 5.0f);
    }

    // Restrict random filter values to the middle range (min Cutoff 450 Hz / 15% knob minimum)
    p.filterCutoff = juce::jmap(juce::jmap(r[8], 0.15f, 0.875f), 450.0f, 14000.0f);
    p.filterReso = juce::jmap(juce::jmap(r[9], 0.125f, 0.875f), 0.3f, 3.5f); // Tamed to 3.5 max to avoid extreme feedback whistles
    // Musical, controlled Filter Envelope randomization (eliminates harsh screeching, mud, or clicks)
    if (envType < 0.35f) // Plucky / percussive
    {
        p.filterEnvAttack = juce::jmap(r[11], 0.001f, 0.04f); // Ultra snappy
        p.filterEnvDecay = juce::jmap(r[12], 0.08f, 0.65f);  // Crisp plucky decay
        p.filterEnvSustain = 0.0f;                             // Pure pluck: closes cleanly
        p.filterEnvAmount = juce::jmap(r[10], 1200.0f, 4800.0f); // Bold snappy opening
    }
    else if (envType < 0.65f) // Keys / brass / synth leads
    {
        p.filterEnvAttack = juce::jmap(r[11], 0.01f, 0.15f);
        p.filterEnvDecay = juce::jmap(r[12], 0.25f, 1.4f);
        p.filterEnvSustain = juce::jmap(r[13], 0.15f, 0.55f); // Musical sustain: lets decay breathe
        p.filterEnvAmount = juce::jmap(r[10], 800.0f, 3800.0f);
    }
    else // Ambient / pads / slow sweeps
    {
        p.filterEnvAttack = juce::jmap(r[11], 0.15f, 1.2f);   // Smooth graceful bloom
        p.filterEnvDecay = juce::jmap(r[12], 0.5f, 2.5f);
        p.filterEnvSustain = juce::jmap(r[13], 0.25f, 0.65f);
        p.filterEnvAmount = juce::jmap(r[10], 600.0f, 3200.0f);
    }
    p.filterEnvRelease = p.filterEnvDecay * 0.85f;

    p.driftAmount = juce::jmap(r[15], 0.0f, 1.0f);
    p.driftAmount = juce::jlimit(0.0f, 1.0f, p.driftAmount);
    p.driftAlive = r[16] > 0.2f;
    p.stereoWidth = (envType > 0.5f) ? juce::jmap(r[0], 0.5f, 1.0f) : juce::jmap(r[0], 0.1f, 0.5f);
    p.isMonophonic = (r[20] < 0.45f);
    p.envLoop = (r[7] < 0.2f);
    p.glideRate = juce::jmap(r[21], 0.0f, 0.30f);

    // Signal Loss is NEVER added to randomized patches (always pristine 0.0f unless user manually turns it on)
    p.reverbSize = 0.0f;

    p.reverbMix = juce::jmap(r[18], 0.05f, 0.40f);  // Tamed to 0.40 max to avoid wet wash saturation
    p.chorusDepth = juce::jmap(r[2], 0.0f, 0.7f);

    // 1 in 5 chance (20% probability) for Decimator to take a random value, 4 in 5 (80%) it stays OFF (0.0f)
    if (r[17] < 0.20f)
        p.decimatorMix = juce::jmap(r[19], 0.08f, 0.32f);
    else
        p.decimatorMix = 0.0f;

    // 25% chance for delay effect to be active
    const bool delayActive = (r[4] < 0.25f);
    if (delayActive)
    {
        p.delayFeedback = juce::jmap(r[3], 0.15f, 0.45f); // Tamed to 0.45 max to avoid runaway feedback
        p.delayMix = juce::jmap(r[1], 0.15f, 0.30f);      // Tamed to 0.30 max to maintain direct signal focus
    }
    else
    {
        p.delayFeedback = 0.0f;
        p.delayMix = 0.0f;
    }

    // 25% chance for autopan effect to be active
    const bool autopanActive = (r[5] < 0.25f);
    if (autopanActive)
    {
        p.autopanAmount = juce::jmap(r[20], 0.25f, 0.85f);
    }
    else
    {
        p.autopanAmount = 0.0f;
    }

    p.autopanRate16th = r[21] > 0.5f;
    p.autopanShapeSaw = r[22] > 0.5f;
    p.delayTone = juce::jmap(r[23], -100.0f, 0.0f);
    p.unisonMode = (r[24] < 0.5f) ? 0 : 1; // 50% 2VU, 50% 5VU

    return p;
}
