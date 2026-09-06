#include "PluginProcessor.h"
#include "PluginEditor.h"

StableAliveAudioProcessor::StableAliveAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), apvts(*this, nullptr, "Parameters", createParameterLayout()), presetManager(apvts)
#endif
{
    // Read license.key once, here on the message thread, so that processBlock()
    // and paint() can query the licence state without touching the file system.
    LicenseManager::refreshCache();

    // Load persistent window scaling once here on the processor, eliminating disk I/O on editor open
    juce::PropertiesFile::Options options;
    options.applicationName = "WX";
    options.filenameSuffix = "settings";
    options.folderName = "Scrap Brain Audio";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(options);
    if (auto* props = appProperties.getUserSettings())
        uiScale = (float) props->getDoubleValue("uiScale", 1.0);

    wavetableBanks[0].build(0.5f);
    wavetableBanks[1].build(0.5f);
    activeWavetableBank.store(0);

    synth.addSound(new SynthSound());
    for (int i = 0; i < 8; ++i)
    {
        auto* voice = new SynthVoice(i);
        voice->setWavetableBank(&wavetableBanks[0]);
        synth.addVoice(voice);
    }
    
    // Seed the initial patch. notifyHost = false: the host has not attached its
    // listeners yet during AudioProcessor construction, and pushing parameter
    // changes at it from inside the constructor is what upsets strict hosts and
    // auval. The parameters and the APVTS tree are still updated.
    auto patch = seedGenerator.generatePatch();
    cachedPatch = patch;
    lastCachedSeedHash = seedGenerator.getSeedHash();
    writePatchToParameters(patch, false);
}

void StableAliveAudioProcessor::prepareWavetablesForSeed(float seed)
{
    int currentActive = activeWavetableBank.load(std::memory_order_relaxed);
    if (std::abs(wavetableBanks[currentActive].seedValue - seed) < 0.0001f)
        return;

    int inactive = 1 - currentActive;
    wavetableBanks[inactive].build(seed);
    activeWavetableBank.store(inactive, std::memory_order_release);
}


void StableAliveAudioProcessor::writePatchToParameters(const SeedGenerator::PatchParameters& patch, bool notifyHost)
{
    // Normalised values must be inside [0, 1]; a patch value outside the
    // parameter's range would otherwise produce an out-of-range normalised
    // value (a debug assertion, and nonsense in release).
    auto applyNormalised = [notifyHost](juce::RangedAudioParameter* param, float normalised) {
        if (param == nullptr)
            return;

        normalised = juce::jlimit(0.0f, 1.0f, normalised);

        if (notifyHost)
        {
            param->setValueNotifyingHost(normalised);
        }
        else
        {
            // Same effect minus the host callback: the parameter and every
            // internal listener (APVTS tree, attachments) are still updated.
            param->setValue(normalised);
            param->sendValueChangedMessageToListeners(normalised);
        }
    };

    auto setFloatParam = [&](const juce::String& paramID, float value) {
        if (auto* param = apvts.getParameter(paramID))
            applyNormalised(param, param->convertTo0to1(value));
    };

    auto setBoolParam = [&](const juce::String& paramID, bool value) {
        applyNormalised(apvts.getParameter(paramID), value ? 1.0f : 0.0f);
    };
    
    setFloatParam("ATTACK", patch.envAttack);
    setFloatParam("DECAY", patch.envDecay);
    setFloatParam("SUSTAIN", patch.envSustain);
    setFloatParam("RELEASE", patch.envRelease);
    setFloatParam("CUTOFF", patch.filterCutoff);
    setFloatParam("RESONANCE", patch.filterReso);
    setFloatParam("DETUNE", patch.detuneAmount);
    setFloatParam("REVERB_SIZE", patch.reverbSize);
    setFloatParam("REVERB_MIX", patch.reverbMix);
    setFloatParam("DELAY_MIX", patch.delayMix);
    setFloatParam("GLIDE", patch.glideRate);
    setFloatParam("CHORUS", patch.chorusDepth);
    setFloatParam("PAN_MIX", patch.autopanAmount);
    setFloatParam("DECIMATOR", patch.decimatorMix);
    setBoolParam("LEGATO", patch.isMonophonic);
    setBoolParam("ENV_LOOP", patch.envLoop);
    setBoolParam("DRIFT_ALIVE", patch.driftAlive);

    // Dedicated Filter Envelope
    setFloatParam("FILTER_ATTACK", patch.filterEnvAttack);
    setFloatParam("FILTER_DECAY", patch.filterEnvDecay);
    setFloatParam("FILTER_SUSTAIN", patch.filterEnvSustain);
    setFloatParam("FILTER_ENV_AMOUNT", patch.filterEnvAmount);

    juce::Random r (juce::Time::getMillisecondCounter());

    // Musical LFO randomization (controlled, non-chaotic)
    static const char* allLfoTargetIds[] = {
        "LFO_CUTOFF_AMT", "LFO_RESO_AMT", "LFO_ATTACK_AMT", "LFO_DECAY_AMT",
        "LFO_SUSTAIN_AMT", "LFO_RELEASE_AMT", "LFO_DETUNE_AMT", "LFO_GLIDE_AMT",
        "LFO_CHORUS_AMT", "LFO_PAN_AMT", "LFO_DELAY_AMT", "LFO_REV_MIX_AMT",
        "LFO_REV_SIZE_AMT", "LFO_DECIMATOR_AMT", "LFO_FILTER_ATTACK_AMT",
        "LFO_FILTER_DECAY_AMT", "LFO_FILTER_SUSTAIN_AMT", "LFO_FILTER_ENV_AMT"
    };

    // Always clear ALL targets to 0 first
    for (const char* tid : allLfoTargetIds)
        setFloatParam(tid, 0.0f);

    // Only allow LFO modulation on core basic timbre controls.
    // EXPLICITLY: NEVER PAN, NEVER GLIDE, NEVER AMP DECAY/SUSTAIN, NEVER secondary envelope/FX!
    static const char* eligibleBasicTargets[] = {
        "LFO_CUTOFF_AMT",
        "LFO_RESO_AMT",
        "LFO_DETUNE_AMT",
        "LFO_CHORUS_AMT",
        "LFO_REV_MIX_AMT"
    };

    float mapRoll = r.nextFloat();
    int numTargets = 0;
    if (mapRoll < 0.50f)
        numTargets = 1; // 50% chance: 1 knob
    else if (mapRoll < 0.75f)
        numTargets = 2; // 25% chance: 2 knobs
    else
        numTargets = 0; // 25% chance: 0 knobs (no modulation)

    if (numTargets > 0)
    {
        float randRate = juce::jmap(r.nextFloat(), 0.15f, 6.0f);
        setFloatParam("LFO_RATE", randRate);
        setFloatParam("LFO_SHAPE", (float) r.nextInt(5));

        const int numEligible = (int)(sizeof(eligibleBasicTargets) / sizeof(eligibleBasicTargets[0]));
        int target1 = r.nextInt(numEligible);
        int target2 = -1;
        if (numTargets == 2)
        {
            do {
                target2 = r.nextInt(numEligible);
            } while (target2 == target1);
        }

        auto generateModAmt = [&]() {
            bool isDeepMod = (r.nextFloat() < 0.20f); // 1 in 5 (20%) chance > 50%
            return isDeepMod ? juce::jmap(r.nextFloat(), 0.52f, 0.85f)
                             : juce::jmap(r.nextFloat(), 0.12f, 0.45f);
        };

        setFloatParam(eligibleBasicTargets[target1], generateModAmt());
        if (target2 >= 0)
            setFloatParam(eligibleBasicTargets[target2], generateModAmt());
    }

    int choiceIdx = 2;
    if (patch.oscMode1 == SeedGenerator::OscMode::Saw) choiceIdx = 0;
    else if (patch.oscMode1 == SeedGenerator::OscMode::Sine) choiceIdx = 1;
    else if (patch.oscMode1 == SeedGenerator::OscMode::Morph) choiceIdx = 2;
    else if (patch.oscMode1 == SeedGenerator::OscMode::Pulse) choiceIdx = 3;
    setFloatParam("OSC_MODE", (float)choiceIdx);
    setFloatParam("UNISON_MODE", (float)patch.unisonMode);
    setFloatParam("OCTAVE_TRANSPOSE", (float)(patch.octaveShift + 1));

    // Randomize wavetable seed on double click / new seed generator patch triggers
    float newSeed = r.nextFloat();
    prepareWavetablesForSeed(newSeed);
    setFloatParam("RANDOM_SEED", newSeed);

    // Randomize filter type on double click / new seed generator patch triggers (60% LP, 20% BP, 20% HP)
    float randVal = r.nextFloat();
    int randomFilterType = 0; // LP
    if (randVal < 0.6f)
        randomFilterType = 0; // LP (60%)
    else if (randVal < 0.8f)
        randomFilterType = 2; // BP (20%)
    else
        randomFilterType = 1; // HP (20%)
    setFloatParam("FILTER_TYPE", (float)randomFilterType);

    // Ensure cutoff is NEVER 0 for any randomized filter mode (min 0.15 / 15%)
    if (auto* param = apvts.getParameter("CUTOFF"))
    {
        if (randomFilterType == 1 || randomFilterType == 2)
        {
            float randomNormalizedCutoff = juce::jmap(r.nextFloat(), 0.15f, 0.40f);
            applyNormalised(param, randomNormalizedCutoff);
        }
        else
        {
            float currentNorm = param->getValue();
            if (currentNorm < 0.15f)
                applyNormalised(param, 0.15f);
        }
    }
    triggerRandomDucking();
}

StableAliveAudioProcessor::~StableAliveAudioProcessor()
{
}

const juce::String StableAliveAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool StableAliveAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool StableAliveAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool StableAliveAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double StableAliveAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int StableAliveAudioProcessor::getNumPrograms()
{
    return 1;
}

int StableAliveAudioProcessor::getCurrentProgram()
{
    return 0;
}

void StableAliveAudioProcessor::setCurrentProgram (int /*index*/)
{
}

const juce::String StableAliveAudioProcessor::getProgramName (int /*index*/)
{
    return {};
}

void StableAliveAudioProcessor::changeProgramName (int /*index*/, const juce::String& /*newName*/)
{
}

void StableAliveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0.0) sampleRate = 44100.0;
    if (samplesPerBlock <= 0) samplesPerBlock = 512;
    int numChans = std::max(1, getTotalNumOutputChannels());

    processorSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;
    internalPpqPosition = 0.0;
    synth.setCurrentPlaybackSampleRate(sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (auto voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
        {
            voice->prepareToPlay(sampleRate, samplesPerBlock, numChans);
            voice->setNotePulseTarget(&noteReactionPulse);
        }
    }

    randomDuckingGain.reset(sampleRate, 0.035);
    randomDuckingGain.setCurrentAndTargetValue(1.0f);

    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.sampleRate = sampleRate;
    spec.numChannels = (juce::uint32)numChans;
    
    effects.prepare(spec);

    masterOversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        2, // 2 channels
        1, // 2^1 = 2x oversampling
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true
    );
    masterOversampling->initProcessing((size_t) samplesPerBlock);

    int oversamplingLatency = (int) std::round(masterOversampling->getLatencyInSamples());
    setLatencySamples(512 + oversamplingLatency);
}

void StableAliveAudioProcessor::releaseResources()
{
    if (masterOversampling != nullptr)
        masterOversampling->reset();
}

bool StableAliveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void StableAliveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MUTE AUDIO COMPLETELY WHILE UNLICENSED / ON LICENSE ACTIVATION SCREEN
    if (!LicenseManager::isLicensed())
    {
        buffer.clear();
        return;
    }

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = 0; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Check if any synth voice is currently playing or sustaining
    bool hasIncomingMidi = !midiMessages.isEmpty();
    bool anyVoiceActive = false;
    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
        {
            if (voice->isVoiceActive())
            {
                anyVoiceActive = true;
                break;
            }
        }
    }

    // Cache PatchParameters by seed hash - only regenerate PRNG when seed actually changes!
    juce::uint32 currentHash = seedGenerator.getSeedHash();
    if (currentHash != lastCachedSeedHash)
    {
        cachedPatch = seedGenerator.generatePatch();
        lastCachedSeedHash = currentHash;
    }
    auto patch = cachedPatch;
    
    float globalCutoff = apvts.getRawParameterValue("CUTOFF")->load();
    float globalReso = apvts.getRawParameterValue("RESONANCE")->load();
    float globalDetune = apvts.getRawParameterValue("DETUNE")->load();
    float gAtk = apvts.getRawParameterValue("ATTACK")->load();
    float gDec = apvts.getRawParameterValue("DECAY")->load();
    float gSus = apvts.getRawParameterValue("SUSTAIN")->load();
    float gRel = apvts.getRawParameterValue("RELEASE")->load();
    float revSize = apvts.getRawParameterValue("REVERB_SIZE")->load();
    float revMix = apvts.getRawParameterValue("REVERB_MIX")->load();
    float delayMix = apvts.getRawParameterValue("DELAY_MIX")->load();
    float panMix = apvts.getRawParameterValue("PAN_MIX")->load();

    double currentBpm = 120.0;
    double blockStartPpq = internalPpqPosition;
    bool usingHostTransport = false;
    if (auto* playHead = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo positionInfo;
        if (playHead->getCurrentPosition(positionInfo))
        {
            if (positionInfo.bpm > 0.0)
                currentBpm = positionInfo.bpm;

            if (positionInfo.isPlaying && positionInfo.ppqPosition >= 0.0)
            {
                blockStartPpq = positionInfo.ppqPosition;
                usingHostTransport = true;
            }
        }
    }
    float glideAmount = apvts.getRawParameterValue("GLIDE")->load();
    float chorusDepth = apvts.getRawParameterValue("CHORUS")->load();
    float decMix = apvts.getRawParameterValue("DECIMATOR")->load();
    float effectiveCutoff = globalCutoff;
    float effectiveDecay = gDec;
    patch.detuneAmount = globalDetune;
    patch.glideRate = glideAmount;
    patch.chorusDepth = chorusDepth;

    // LFO computation
    bool lfoSync = apvts.getRawParameterValue("LFO_SYNC") ? (apvts.getRawParameterValue("LFO_SYNC")->load() > 0.5f) : false;
    float lfoRate = apvts.getRawParameterValue("LFO_RATE")->load();
    double effectiveLfoHz = lfoRate;
    if (lfoSync)
    {
        int beatsIdx = 3;
        if (auto* p = apvts.getRawParameterValue("LFO_BEATS"))
            beatsIdx = juce::jlimit(0, 7, (int) std::round(p->load()));

        static const double beatDivisions[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
        double beatsPerCycle = beatDivisions[beatsIdx];
        double beatsPerSec = (currentBpm > 0.0) ? (currentBpm / 60.0) : 2.0;
        effectiveLfoHz = beatsPerSec / beatsPerCycle;
    }

    int lfoShape = (int) std::round(apvts.getRawParameterValue("LFO_SHAPE")->load());

    double sampleRate = getSampleRate();
    if (sampleRate <= 0.0) sampleRate = 44100.0;
    float phaseDelta = (float) (effectiveLfoHz * (double) buffer.getNumSamples() / sampleRate);
    lfoPhase += phaseDelta;
    if (lfoPhase >= 1.0f)
    {
        lfoPhase -= std::floor(lfoPhase);
        lfoLastRandVal = lfoTargetRandVal;
        lfoTargetRandVal = (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f);
    }

    float lfoVal = 0.0f;
    switch (lfoShape)
    {
        case 0: // SINE
            lfoVal = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);
            break;
        case 1: // TRIANGLE
            lfoVal = 4.0f * std::abs(lfoPhase - 0.5f) - 1.0f;
            break;
        case 2: // SAW
            lfoVal = 1.0f - 2.0f * lfoPhase;
            break;
        case 3: // SQUARE
            lfoVal = (lfoPhase < 0.5f) ? 1.0f : -1.0f;
            break;
        case 4: // S&H
            lfoVal = lfoTargetRandVal;
            break;
        default:
            lfoVal = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);
            break;
    }

    lfoCurrentValue.store(lfoVal);
    lfoCurrentPhase.store(lfoPhase);

    // IDLE FAST PATH: nothing playing, no MIDI arriving, and the tails have died
    // away - so skip the voices, the decimator, the effects and the master stage,
    // which is nearly all of the cost.
    //
    // It sits HERE, after the LFO, and not before it: the LFO costs a handful of
    // operations per block, and stopping it froze the indicator on the knobs
    // whenever playback stopped.
    //
    // Entering idle also flushes the effects. Skipping effects.process() freezes
    // the delay line, the reverb and the packet-loss engine's 512-sample delay
    // with whatever was in them, and the next note pushed that stale audio back
    // out - clicks and dropouts that sounded like Signal Loss was stuck on, on
    // patches that never touched it.
    const int idleBlocksNeeded = juce::jmax(8, (int) (processorSampleRate * 0.5 / juce::jmax(1, buffer.getNumSamples())));

    if (!anyVoiceActive && !hasIncomingMidi && silentBlocksCount > idleBlocksNeeded)
    {
        if (! effectsIdleFlushed)
        {
            effects.reset();
            decimatorCounters[0] = decimatorCounters[1] = 0.0f;
            decimatorHeldSamples[0] = decimatorHeldSamples[1] = 0.0f;
            if (masterOversampling != nullptr)
                masterOversampling->reset();
            effectsIdleFlushed = true;
        }

        smoothedRMS = 0.12f;
        currentAutoGain = 1.0f;
        return;
    }

    effectsIdleFlushed = false;

    // Apply LFO modulations across all modulatable parameters
    auto getLfoAmt = [this](const char* id) -> float {
        if (auto* p = apvts.getRawParameterValue(id)) return p->load();
        return 0.0f;
    };

    float lfoCutoffAmt = getLfoAmt("LFO_CUTOFF_AMT");
    if (lfoCutoffAmt > 0.001f)
        effectiveCutoff = juce::jlimit(20.0f, 20000.0f, globalCutoff * std::pow(2.0f, lfoVal * lfoCutoffAmt * 3.5f));

    float lfoResoAmt = getLfoAmt("LFO_RESO_AMT");
    if (lfoResoAmt > 0.001f)
        globalReso = juce::jlimit(0.1f, 10.0f, globalReso + lfoVal * lfoResoAmt * 3.5f);

    float lfoAtkAmt = getLfoAmt("LFO_ATTACK_AMT");
    if (lfoAtkAmt > 0.001f)
        gAtk = juce::jlimit(0.001f, 5.0f, gAtk * std::pow(2.0f, lfoVal * lfoAtkAmt * 2.0f));

    float lfoDecAmt = getLfoAmt("LFO_DECAY_AMT");
    if (lfoDecAmt > 0.001f)
        effectiveDecay = juce::jlimit(0.01f, 10.0f, effectiveDecay * std::pow(2.0f, lfoVal * lfoDecAmt * 2.0f));

    float lfoSusAmt = getLfoAmt("LFO_SUSTAIN_AMT");
    if (lfoSusAmt > 0.001f)
        gSus = juce::jlimit(0.0f, 1.0f, gSus + lfoVal * lfoSusAmt * 0.5f);

    float lfoRelAmt = getLfoAmt("LFO_RELEASE_AMT");
    if (lfoRelAmt > 0.001f)
        gRel = juce::jlimit(0.001f, 10.0f, gRel * std::pow(2.0f, lfoVal * lfoRelAmt * 2.0f));

    float lfoDetuneAmt = getLfoAmt("LFO_DETUNE_AMT");
    if (lfoDetuneAmt > 0.001f)
        patch.detuneAmount = juce::jlimit(0.0f, 1.0f, patch.detuneAmount + lfoVal * lfoDetuneAmt * 0.4f);

    float lfoGlideAmt = getLfoAmt("LFO_GLIDE_AMT");
    if (lfoGlideAmt > 0.001f)
        glideAmount = juce::jlimit(0.0f, 1.0f, glideAmount + lfoVal * lfoGlideAmt * 0.5f);

    float lfoChorusAmt = getLfoAmt("LFO_CHORUS_AMT");
    if (lfoChorusAmt > 0.001f)
        chorusDepth = juce::jlimit(0.0f, 1.0f, chorusDepth + lfoVal * lfoChorusAmt * 0.5f);

    float lfoPanAmt = getLfoAmt("LFO_PAN_AMT");
    if (lfoPanAmt > 0.001f)
        panMix = juce::jlimit(0.0f, 1.0f, panMix + lfoVal * lfoPanAmt * 0.8f);

    float lfoDelayAmt = getLfoAmt("LFO_DELAY_AMT");
    if (lfoDelayAmt > 0.001f)
        delayMix = juce::jlimit(0.0f, 1.0f, delayMix + lfoVal * lfoDelayAmt * 0.5f);

    float lfoRevMixAmt = getLfoAmt("LFO_REV_MIX_AMT");
    if (lfoRevMixAmt > 0.001f)
        revMix = juce::jlimit(0.0f, 1.0f, revMix + lfoVal * lfoRevMixAmt * 0.5f);

    float lfoRevSizeAmt = getLfoAmt("LFO_REV_SIZE_AMT");
    if (lfoRevSizeAmt > 0.001f)
        revSize = juce::jlimit(0.0f, 1.0f, revSize + lfoVal * lfoRevSizeAmt * 0.5f);

    float lfoDecMixAmt = getLfoAmt("LFO_DECIMATOR_AMT");
    if (lfoDecMixAmt > 0.001f)
        decMix = juce::jlimit(0.0f, 1.0f, decMix + lfoVal * lfoDecMixAmt * 0.5f);

    patch.glideRate = glideAmount;
    patch.chorusDepth = chorusDepth;
    
    bool driftAlive = apvts.getRawParameterValue("DRIFT_ALIVE")->load() > 0.5f;
    bool legatoEnabled = apvts.getRawParameterValue("LEGATO")->load() > 0.5f;
    patch.isMonophonic = legatoEnabled;
    bool envLoop = apvts.getRawParameterValue("ENV_LOOP")->load() > 0.5f;
    bool envLoopSync = apvts.getRawParameterValue("ENV_LOOP_SYNC")->load() > 0.5f;
    float envLoopMs = apvts.getRawParameterValue("ENV_LOOP_MS")->load();
    float envLoopBeats = apvts.getRawParameterValue("ENV_LOOP_BEATS")->load();

    if (envLoopSync)
    {
        static const double MusicalBeats[] = {
            0.25,   // 0: 1/16
            0.375,  // 1: 1/16.
            0.5,    // 2: 1/8
            0.75,   // 3: 1/8.
            1.0,    // 4: 1/4 (CENTER - 12 o'clock!)
            1.5,    // 5: 1/4.
            2.0,    // 6: 1/2
            3.0,    // 7: 1/2.
            4.0     // 8: 1/1 (1 BAR)
        };
        int stepIdx = juce::jlimit(0, 8, (int) std::round(envLoopBeats));
        const double loopLengthBeats = MusicalBeats[stepIdx];
        envLoopMs = (float) ((60000.0 / currentBpm) * loopLengthBeats);

        if (usingHostTransport)
        {
            double loopPhaseBeats = std::fmod(blockStartPpq, loopLengthBeats);
            if (loopPhaseBeats < 0.0)
                loopPhaseBeats += loopLengthBeats;

            const double boundaryTolerance = 1.0e-7;
            const double beatsUntilBoundary = loopPhaseBeats < boundaryTolerance
                ? 0.0
                : loopLengthBeats - loopPhaseBeats;
            const double samplesPerBeat = processorSampleRate * 60.0 / currentBpm;
            const double firstBoundarySample = beatsUntilBoundary * samplesPerBeat;
            const double intervalSamples = loopLengthBeats * samplesPerBeat;

            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
                    voice->setLoopSyncTiming(envLoop, firstBoundarySample, intervalSamples);
        }
        else
        {
            // Standalone / stopped transport: loop with pure, uninterrupted tempo periodicity
            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
                    voice->setLoopSyncTiming(false, 0.0, 1.0);
        }
    }
    else
    {
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
                voice->setLoopSyncTiming(false, 0.0, 1.0);
    }

    if (!usingHostTransport)
    {
        blockStartPpq = internalPpqPosition;
        internalPpqPosition += (double) buffer.getNumSamples() * currentBpm
                             / (60.0 * processorSampleRate);
    }

    int oscModeIdx = (int) std::round(apvts.getRawParameterValue("OSC_MODE")->load());
    float randomSeed = apvts.getRawParameterValue("RANDOM_SEED")->load();
    int filterTypeIdx = (int) std::round(apvts.getRawParameterValue("FILTER_TYPE")->load());
    int unisonModeIdx = (int) std::round(apvts.getRawParameterValue("UNISON_MODE")->load());
    int octaveShiftIdx = (int) std::round(apvts.getRawParameterValue("OCTAVE_TRANSPOSE")->load()) - 1;
    float fAtk = apvts.getRawParameterValue("FILTER_ATTACK") ? apvts.getRawParameterValue("FILTER_ATTACK")->load() : 0.01f;
    float fDec = apvts.getRawParameterValue("FILTER_DECAY") ? apvts.getRawParameterValue("FILTER_DECAY")->load() : 0.8f;
    float fSus = apvts.getRawParameterValue("FILTER_SUSTAIN") ? apvts.getRawParameterValue("FILTER_SUSTAIN")->load() : 0.0f;
    float fEnvAmt = apvts.getRawParameterValue("FILTER_ENV_AMOUNT") ? apvts.getRawParameterValue("FILTER_ENV_AMOUNT")->load() : 2500.0f;

    float lfoFAtkAmt = getLfoAmt("LFO_FILTER_ATTACK_AMT");
    if (lfoFAtkAmt > 0.001f)
        fAtk = juce::jlimit(0.001f, 5.0f, fAtk * std::pow(2.0f, lfoVal * lfoFAtkAmt * 2.0f));

    float lfoFDecAmt = getLfoAmt("LFO_FILTER_DECAY_AMT");
    if (lfoFDecAmt > 0.001f)
        fDec = juce::jlimit(0.01f, 10.0f, fDec * std::pow(2.0f, lfoVal * lfoFDecAmt * 2.0f));

    float lfoFSusAmt = getLfoAmt("LFO_FILTER_SUSTAIN_AMT");
    if (lfoFSusAmt > 0.001f)
        fSus = juce::jlimit(0.0f, 1.0f, fSus + lfoVal * lfoFSusAmt * 0.5f);

    float lfoFAmtAmt = getLfoAmt("LFO_FILTER_ENV_AMT");
    if (lfoFAmtAmt > 0.001f)
        fEnvAmt = juce::jlimit(-5000.0f, 8000.0f, fEnvAmt + lfoVal * lfoFAmtAmt * 3000.0f);

    float fRel = fDec * 0.85f;

    int bankIdx = activeWavetableBank.load(std::memory_order_acquire);
    if (std::abs(wavetableBanks[bankIdx].seedValue - randomSeed) > 0.0001f)
    {
        int inactive = 1 - bankIdx;
        wavetableBanks[inactive].build(randomSeed);
        activeWavetableBank.store(inactive, std::memory_order_release);
        bankIdx = inactive;
    }
    const WavetableBank* currentBank = &wavetableBanks[bankIdx];

    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (auto voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
        {
            voice->setWavetableBank(currentBank);
            voice->updateParameters(patch, effectiveCutoff, globalReso, gAtk, effectiveDecay, gSus, gRel,
                                    envLoop, envLoopMs, driftAlive, oscModeIdx, randomSeed, filterTypeIdx, unisonModeIdx, octaveShiftIdx,
                                    fAtk, fDec, fSus, fRel, fEnvAmt);
        }
    }

    float sigLoss = apvts.getRawParameterValue("REVERB_SIZE")->load();
    effects.updateParameters(sigLoss, revMix, delayMix, patch.delayFeedback, patch.delayTone,
                             panMix, patch.autopanRate16th, patch.autopanShapeSaw, chorusDepth);

    // Update synth monophonic state
    synth.isMonophonic = legatoEnabled;

    // Monophonic legato mode switch handling
    if (legatoEnabled != wasMonophonic)
    {
        heldNotes.clear();
        synth.allNotesOff(0, false);
        activeMidiNote = -1;
        wasMonophonic = legatoEnabled;
    }

    // Trigger reactive radar disturbance on MIDI note strikes
    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            float vel = msg.getFloatVelocity();
            noteReactionPulse.store(std::min(1.0f, vel * 0.7f + 0.3f));
            break;
        }
    }

    if (legatoEnabled)
    {
        juce::MidiBuffer monoMidiMessages;
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            auto samplePosition = metadata.samplePosition;
            
            if (msg.isNoteOn())
            {
                int note = msg.getNoteNumber();
                for (auto it = heldNotes.begin(); it != heldNotes.end(); )
                {
                    if (*it == note) it = heldNotes.erase(it);
                    else ++it;
                }
                heldNotes.push_back(note);
                
                activeMidiNote = note;
                monoMidiMessages.addEvent(msg, samplePosition);
            }
            else if (msg.isNoteOff())
            {
                int note = msg.getNoteNumber();
                for (auto it = heldNotes.begin(); it != heldNotes.end(); )
                {
                    if (*it == note) it = heldNotes.erase(it);
                    else ++it;
                }
                
                if (note == activeMidiNote)
                {
                    if (!heldNotes.empty())
                    {
                        activeMidiNote = heldNotes.back();
                        monoMidiMessages.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), activeMidiNote, 0.8f), samplePosition);
                    }
                    else
                    {
                        monoMidiMessages.addEvent(msg, samplePosition);
                        activeMidiNote = -1;
                    }
                }
            }
            else
            {
                monoMidiMessages.addEvent(msg, samplePosition);
            }
        }
        midiMessages.swapWith(monoMidiMessages);
    }

    // Render Synth
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    // Decimator processing
    float activeDecimatorMix = juce::jlimit(0.0f, 1.0f, decMix);
    if (activeDecimatorMix > 0.01f)
    {
        float step = std::pow(2.0f, juce::jlimit(2.0f, 24.0f, 24.0f - activeDecimatorMix * 20.0f));
        float rateDivide = 1.0f + activeDecimatorMix * 7.0f;
        int numChannels = buffer.getNumChannels();
        int numSamples = buffer.getNumSamples();
        
        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            float chanCounter = decimatorCounters[channel % 2];
            float chanHeld = decimatorHeldSamples[channel % 2];
            
            for (int i = 0; i < numSamples; ++i)
            {
                chanCounter += 1.0f;
                if (chanCounter >= rateDivide)
                {
                    chanCounter = 0.0f;
                    chanHeld = std::round(data[i] * step) / step;
                }
                data[i] = data[i] * (1.0f - activeDecimatorMix) + chanHeld * activeDecimatorMix;
            }
            decimatorCounters[channel % 2] = chanCounter;
            decimatorHeldSamples[channel % 2] = chanHeld;
        }
    }

    // Render Effects
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    effects.process(context, currentBpm);

    // Track audio magnitude to detect when reverb/delay tails have completely settled
    float currentMag = buffer.getMagnitude(0, buffer.getNumSamples());
    if (!anyVoiceActive && !hasIncomingMidi && currentMag < 1e-5f)
    {
        silentBlocksCount++;
    }
    else
    {
        silentBlocksCount = 0;
    }

    // 1. Calculate current block RMS across all channels
    float sumSquared = 0.0f;
    int totalSamples = buffer.getNumSamples() * buffer.getNumChannels();
    if (totalSamples > 0)
    {
        for (int chan = 0; chan < buffer.getNumChannels(); ++chan)
        {
            auto* readPtr = buffer.getReadPointer(chan);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                sumSquared += readPtr[i] * readPtr[i];
            }
        }
        float currentRMS = std::sqrt(sumSquared / (float)totalSamples);
        
        if (std::isnan(smoothedRMS) || std::isinf(smoothedRMS))
            smoothedRMS = 0.12f;

        // Sample-rate and block-size independent time constants (tau_att = 50ms, tau_rel = 400ms)
        float blockDurationSec = (float) buffer.getNumSamples() / (float) processorSampleRate;
        if (blockDurationSec <= 0.0f) blockDurationSec = 0.01f;

        float alphaAtt = 1.0f - std::exp(-blockDurationSec / 0.050f);
        float alphaRel = 1.0f - std::exp(-blockDurationSec / 0.400f);
        float alphaSilence = 1.0f - std::exp(-blockDurationSec / 0.800f);

        if (currentRMS > 0.005f && !std::isnan(currentRMS) && !std::isinf(currentRMS))
        {
            float alpha = (currentRMS > smoothedRMS) ? alphaAtt : alphaRel;
            smoothedRMS += alpha * (currentRMS - smoothedRMS);
        }
        else
        {
            // Decay slowly back to target during silence to avoid startup volume spikes
            smoothedRMS += alphaSilence * (0.12f - smoothedRMS);
        }
    }

    // 2. Compute target auto-gain multiplier (limited to transparent ±2.5 dB window to eliminate pumping)
    float targetAutoGain = 1.0f;
    if (smoothedRMS > 0.001f && !std::isnan(smoothedRMS) && !std::isinf(smoothedRMS))
    {
        targetAutoGain = 0.12f / smoothedRMS;
        targetAutoGain = juce::jlimit(0.75f, 1.35f, targetAutoGain);
    }
    if (std::isnan(targetAutoGain) || std::isinf(targetAutoGain))
        targetAutoGain = 1.0f;

    // 3. Apply master gain and auto-gain leveling
    float masterGain = apvts.getRawParameterValue("GAIN")->load();
    int masterChannels = buffer.getNumChannels();
    int masterSamples = buffer.getNumSamples();
    float sampleDuration = 1.0f / (float) processorSampleRate;
    float gainSlewCoeff = 1.0f - std::exp(-sampleDuration / 0.030f); // 30ms slew
    
    for (int i = 0; i < masterSamples; ++i)
    {
        currentAutoGain += gainSlewCoeff * (targetAutoGain - currentAutoGain);
        if (std::isnan(currentAutoGain) || std::isinf(currentAutoGain))
            currentAutoGain = 1.0f;

        float gainToApply = masterGain * currentAutoGain;

        for (int channel = 0; channel < masterChannels; ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            data[i] *= gainToApply;
        }
    }

    // 4. 2x Oversampled Non-Linear Tanh Saturation (Anti-Aliased Master Stage)
    // Only run expensive oversampling and per-sample tanh when there is audible audio!
    if (currentMag > 1e-6f)
    {
        if (masterOversampling != nullptr && masterChannels == 2 && masterSamples <= preparedBlockSize)
        {
            juce::dsp::AudioBlock<float> masterBlock (buffer);
            juce::dsp::AudioBlock<float> overBlock = masterOversampling->processSamplesUp (masterBlock);
            
            size_t overSamples = overBlock.getNumSamples();
            auto* left = overBlock.getChannelPointer(0);
            auto* right = overBlock.getChannelPointer(1);
            
            for (size_t s = 0; s < overSamples; ++s)
            {
                float l = left[s];
                float r = right[s];
                if (std::isnan(l) || std::isinf(l)) l = 0.0f;
                if (std::isnan(r) || std::isinf(r)) r = 0.0f;
                left[s] = std::tanh(l) * 0.95f;
                right[s] = std::tanh(r) * 0.95f;
            }
            
            masterOversampling->processSamplesDown (masterBlock);
        }
        else
        {
            for (int channel = 0; channel < masterChannels; ++channel)
            {
                auto* data = buffer.getWritePointer(channel);
                for (int i = 0; i < masterSamples; ++i)
                {
                    float val = data[i];
                    if (std::isnan(val) || std::isinf(val)) val = 0.0f;
                    data[i] = std::tanh(val) * 0.95f;
                }
            }
        }
    }
}

bool StableAliveAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* StableAliveAudioProcessor::createEditor()
{
    return new StableAliveAudioProcessorEditor (*this);
}

void StableAliveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("uiScale", uiScale, nullptr);
    state.setProperty("currentPresetName", presetManager.getCurrentPresetName(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void StableAliveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xmlState);
            apvts.replaceState (vt);
            uiScale = vt.getProperty("uiScale", 1.0f);
            presetManager.setCurrentPresetName(vt.getProperty("currentPresetName", "INIT"));
            if (auto* seedParam = apvts.getRawParameterValue("RANDOM_SEED"))
                prepareWavetablesForSeed(seedParam->load());
        }

}

juce::AudioProcessorValueTreeState::ParameterLayout StableAliveAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Envelope
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("ATTACK", "Attack", juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.25f), 0.1f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("DECAY", "Decay", juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.25f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("SUSTAIN", "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.8f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("RELEASE", "Release", juce::NormalisableRange<float> (0.001f, 10.0f, 0.001f, 0.25f), 1.0f));

    // Filter
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("CUTOFF", "Cutoff", juce::NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.25f), 2000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("RESONANCE", "Resonance", juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("DETUNE", "Detune", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // Space
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("REVERB_SIZE", "Signal Loss", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("REVERB_MIX", "Reverb Mix", 0.0f, 1.0f, 0.3f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("DELAY_MIX", "Delay Mix", 0.0f, 1.0f, 0.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("GLIDE", "Glide", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("CHORUS", "Chorus", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("PAN_MIX", "Autopan Mix", 0.0f, 1.0f, 0.0f));

    // Drift
    params.push_back (std::make_unique<juce::AudioParameterBool> ("LEGATO", "Legato", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("DRIFT_ALIVE", "Drift Alive", true));

    // Decimator
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("DECIMATOR", "Decimator", 0.0f, 1.0f, 0.0f));

    // Envelope Loop
    params.push_back (std::make_unique<juce::AudioParameterBool> ("ENV_LOOP", "Env Loop", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("ENV_LOOP_SYNC", "Env Loop BPM Sync", true));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("ENV_LOOP_MS", "Env Loop MS", juce::NormalisableRange<float> (30.0f, 4000.0f, 1.0f, 0.35f), 500.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("ENV_LOOP_BEATS", "Env Loop Beats", juce::NormalisableRange<float> (0.0f, 8.0f, 1.0f), 4.0f));

    // Oscillator Mode (choices: saw, sine, morph, pulse)
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("OSC_MODE", "Oscillator Mode", juce::StringArray { "saw", "sine", "morph", "pulse" }, 2));

    // Master volume/gain
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("GAIN", "Output Volume", 0.0f, 1.0f, 0.7f));

    // Random Wavetable Seed
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("RANDOM_SEED", "Random Seed", 0.0f, 1.0f, 0.5f));

    // Filter Type (choices: LP, HP, BP)
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("FILTER_TYPE", "Filter Type", juce::StringArray { "LP", "HP", "BP" }, 0));

    // Unison Mode (choices: 2VU, 5VU)
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("UNISON_MODE", "Unison Mode", juce::StringArray { "2VU", "5VU" }, 1));

    // Octave Transpose (choices: MONE, NORM, PONE)
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("OCTAVE_TRANSPOSE", "Octave Transpose", juce::StringArray { "MONE", "NORM", "PONE" }, 1));

    // Dedicated Filter Envelope (Page 2 - 4 Knobs)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "FILTER_ATTACK", "Filter Attack",
        juce::NormalisableRange<float> (0.001f, 5.0f, 0.001f, 0.35f), 0.01f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "FILTER_DECAY", "Filter Decay",
        juce::NormalisableRange<float> (0.01f, 10.0f, 0.01f, 0.35f), 0.8f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "FILTER_SUSTAIN", "Filter Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "FILTER_ENV_AMOUNT", "Filter Env Amount",
        juce::NormalisableRange<float> (-5000.0f, 8000.0f, 1.0f), 2500.0f));

    // LFO Module Parameters (Page 2 Top-Left)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "LFO_RATE", "LFO Rate",
        juce::NormalisableRange<float> (0.05f, 20.0f, 0.01f, 0.35f), 2.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "LFO_SYNC", "LFO BPM Sync", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "LFO_BEATS", "LFO Sync Division",
        juce::StringArray { "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" }, 3));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "LFO_SHAPE", "LFO Shape",
        juce::StringArray { "SIN", "TRI", "SAW", "SQR", "RND" }, 0));

    // LFO Modulation Depths for ALL knobs
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_CUTOFF_AMT", "LFO to Cutoff", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_RESO_AMT", "LFO to Resonance", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_ATTACK_AMT", "LFO to Attack", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_DECAY_AMT", "LFO to Decay", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_SUSTAIN_AMT", "LFO to Sustain", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_RELEASE_AMT", "LFO to Release", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_DETUNE_AMT", "LFO to Detune", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_GLIDE_AMT", "LFO to Glide", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_CHORUS_AMT", "LFO to Chorus", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_PAN_AMT", "LFO to Pan", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_DELAY_AMT", "LFO to Delay Mix", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_REV_MIX_AMT", "LFO to Reverb Mix", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_REV_SIZE_AMT", "LFO to Reverb Size", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_DECIMATOR_AMT", "LFO to Decimator", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_FILTER_ATTACK_AMT", "LFO to Filter Attack", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_FILTER_DECAY_AMT", "LFO to Filter Decay", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_FILTER_SUSTAIN_AMT", "LFO to Filter Sustain", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("LFO_FILTER_ENV_AMT", "LFO to Filter Env Amount", 0.0f, 1.0f, 0.0f));

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StableAliveAudioProcessor();
}
