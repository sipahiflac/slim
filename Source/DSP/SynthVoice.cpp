#include "SynthVoice.h"

SynthVoice::SynthVoice(int voiceIndex) : voiceIdx(voiceIndex)
{
    filter.setMode(juce::dsp::LadderFilterMode::LPF24);
    filter.setDrive(1.35f);
    
    // Spread voices across stereo field based on index
    // Voices 0,1 = center-ish, 2,3 = wider, etc.
    float spread[] = { 0.45f, 0.55f, 0.3f, 0.7f, 0.2f, 0.8f, 0.35f, 0.65f };
    voicePan = spread[voiceIndex % 8];
}

bool SynthVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<SynthSound*>(sound) != nullptr;
}

void SynthVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    int transposedNote = juce::jlimit(0, 127, midiNoteNumber + octaveShift * 12);
    float newFreq = (float)juce::MidiMessage::getMidiNoteInHertz(transposedNote);
    
    if (isMonophonic && !isKeyReleased && env.isActive())
    {
        targetFrequency = newFreq;
        currentVelocity = velocity;
        smoothedVelocity.setTargetValue(velocity);
    }
    else
    {
        // Only reset oscillator phases and filter state if the voice is actually silent
        // to prevent waveform discontinuities and clicks when re-triggering active voices.
        bool wasSilent = (env.getCurrentLevel() < 0.001f);
        if (wasSilent)
        {
            juce::Random& prng = juce::Random::getSystemRandom();
            const float twoPi = juce::MathConstants<float>::twoPi;
            osc1.setPhase(prng.nextFloat() * twoPi);
            osc2.setPhase(prng.nextFloat() * twoPi);
            oscUnison1.setPhase(prng.nextFloat() * twoPi);
            oscUnison2.setPhase(prng.nextFloat() * twoPi);
            oscUnison3.setPhase(prng.nextFloat() * twoPi);
            subOsc.setPhase(prng.nextFloat() * twoPi);
            filter.reset();
            currentBaseFrequency = newFreq; // Snap instantly since it's silent
            smoothedVelocity.setCurrentAndTargetValue(velocity); // Snap velocity instantly
            smoothedVoiceCutoff.setCurrentAndTargetValue(baseCutoff);
        }
        else
        {
            // Do NOT snap currentBaseFrequency or currentVelocity instantly to prevent click.
            // Frequency and velocity will glide smoothly to target values over a short 5ms time-constant.
            smoothedVelocity.setTargetValue(velocity);
            smoothedVoiceCutoff.setTargetValue(baseCutoff);
        }

        targetFrequency = newFreq;
        currentVelocity = velocity;
        isKeyReleased = false;
        env.noteOn();
        filterEnv.noteOn();
    }
}

void SynthVoice::stopNote (float /*velocity*/, bool allowTailOff)
{
    if (allowTailOff)
    {
        isKeyReleased = true;
        env.noteOff();
        filterEnv.noteOff();
    }
    else
    {
        isKeyReleased = true;
        env.forceFastStop();
        filterEnv.forceFastStop();
    }
}

void SynthVoice::pitchWheelMoved (int /*newPitchWheelValue*/) {}
void SynthVoice::controllerMoved (int /*controllerNumber*/, int /*newControllerValue*/) {}

void SynthVoice::prepareToPlay(double sampleRate, int samplesPerBlock, int outputChannels)
{
    if (sampleRate <= 0.0) sampleRate = 44100.0;
    if (samplesPerBlock <= 0) samplesPerBlock = 512;
    if (outputChannels <= 0) outputChannels = 2;

    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.sampleRate = sampleRate;
    spec.numChannels = (juce::uint32)outputChannels;

    osc1.prepare(sampleRate);
    osc2.prepare(sampleRate);
    oscUnison1.prepare(sampleRate);
    oscUnison2.prepare(sampleRate);
    oscUnison3.prepare(sampleRate);
    subOsc.prepare(sampleRate);
    filter.prepare(spec);
    gain.prepare(spec);
    
    gain.setGainLinear(0.12f);
    
    smoothedVelocity.reset(sampleRate, 0.005);
    smoothedVoiceCutoff.reset(sampleRate, 0.006);
    
    env.setSampleRate(sampleRate);
    filterEnv.setSampleRate(sampleRate);
    
    drift.prepare(sampleRate);
    isPrepared = true;
}

void SynthVoice::updateParameters(const SeedGenerator::PatchParameters& params, float globalCutoff, float globalReso,
                                   float globalAttack, float globalDecay, float globalSustain, float globalRelease,
                                   bool envLoop, float envLoopMs, bool driftAlive, int oscModeIdx, float randomSeed,
                                   int filterTypeIdx, int unisonModeIdx, int octaveShiftIdx,
                                   float filterAttack, float filterDecay, float filterSustain, float filterRelease, float inFilterEnvAmount)
{
    unisonMode = unisonModeIdx;
    octaveShift = octaveShiftIdx;
    oscMorph = params.oscMorph;
    currentDetune = params.detuneAmount;
    subLevel = params.subOscLevel;
    stereoWidth = params.stereoWidth;
    
    // Set oscillator mode based on user selection or seed fallback
    if (oscModeIdx == 0)      { oscMode1 = SeedGenerator::OscMode::Saw;   oscMode2 = SeedGenerator::OscMode::Saw; }
    else if (oscModeIdx == 1) { oscMode1 = SeedGenerator::OscMode::Sine;  oscMode2 = SeedGenerator::OscMode::Sine; }
    else if (oscModeIdx == 2) { oscMode1 = SeedGenerator::OscMode::Morph; oscMode2 = SeedGenerator::OscMode::Morph; }
    else if (oscModeIdx == 3) { oscMode1 = SeedGenerator::OscMode::Pulse; oscMode2 = SeedGenerator::OscMode::Pulse; }
    else                      { oscMode1 = params.oscMode1;               oscMode2 = params.oscMode2; }

    currentSeed = randomSeed;

    
    isMonophonic = params.isMonophonic;
    
    // Map glide parameter (0..1) to portamento glide time (0..0.3 seconds)
    // Using a squared curve for finer control over shorter, snappy glide times
    float glideTime = params.glideRate * params.glideRate * 0.3f;
    if (glideTime > 0.001f)
    {
        glideRate = std::exp(-1.0f / (currentSampleRate * glideTime));
    }
    else
    {
        glideRate = 0.0f; // Instant note snap
    }
    
    baseCutoff = globalCutoff;
    
    if (filterTypeIdx == 0)      { filter.setMode(juce::dsp::LadderFilterMode::LPF24); filter.setDrive(1.35f); filter.setResonance(juce::jlimit(0.0f, 0.95f, globalReso * 0.08f)); }
    else if (filterTypeIdx == 1) { filter.setMode(juce::dsp::LadderFilterMode::HPF12); filter.setDrive(1.10f); filter.setResonance(juce::jlimit(0.0f, 0.90f, globalReso * 0.06f)); }
    else if (filterTypeIdx == 2) { filter.setMode(juce::dsp::LadderFilterMode::BPF12); filter.setDrive(1.10f); filter.setResonance(juce::jlimit(0.0f, 0.90f, globalReso * 0.06f)); }
    
    filterEnvAmount = inFilterEnvAmount;
    filterType = filterTypeIdx;
    
    // Amp envelope set from global UI ADSR controls
    env.setParameters(globalAttack, globalDecay, globalSustain, globalRelease, envLoop);
    env.setLoopControls(envLoopMs);
    
    // Dedicated Independent Filter Envelope
    filterEnv.setParameters(filterAttack, filterDecay, filterSustain, filterRelease, envLoop);
    filterEnv.setLoopControls(envLoopMs);
    
    driftAmount = driftAlive ? params.driftAmount : 0.0f;
}

void SynthVoice::setLoopSyncTiming(bool enabled, double firstBoundarySample, double intervalSamples)
{
    loopGridSyncEnabled = enabled;
    nextLoopBoundarySample = std::max<int64_t>(0, (int64_t) std::llround(firstBoundarySample));
    loopBoundaryIntervalSamples = std::max<int64_t>(1, (int64_t) std::llround(intervalSamples));
    env.setGridSyncEnabled(enabled);
    filterEnv.setGridSyncEnabled(enabled);
}

bool SynthVoice::isVoiceActive() const
{
    return getCurrentlyPlayingNote() >= 0 || env.isActive() || filterEnv.isActive();
}

void SynthVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isPrepared) return;
    
    if (!env.isActive() && !filterEnv.isActive())
    {
        if (getCurrentlyPlayingNote() >= 0)
            clearCurrentNote();
        return;
    }

    smoothedVoiceCutoff.setTargetValue(baseCutoff);

    for (int i = 0; i < numSamples; ++i)
    {
        const int64_t blockSample = (int64_t) startSample + i;
        while (loopGridSyncEnabled && nextLoopBoundarySample < blockSample)
            nextLoopBoundarySample += loopBoundaryIntervalSamples;

        const bool loopGridBoundary = loopGridSyncEnabled && nextLoopBoundarySample == blockSample;
        if (loopGridBoundary)
            nextLoopBoundarySample += loopBoundaryIntervalSamples;

        drift.process(driftAmount);
        
        // Pitch glide (handles user legato glide or automatic 5ms de-click glide on re-triggers)
        if (currentBaseFrequency != targetFrequency)
        {
            float activeGlideRate = isMonophonic ? glideRate : std::exp(-1.0f / (currentSampleRate * 0.005f));
            currentBaseFrequency = currentBaseFrequency * activeGlideRate + targetFrequency * (1.0f - activeGlideRate);
            if (std::abs(currentBaseFrequency - targetFrequency) < 0.1f)
                currentBaseFrequency = targetFrequency;
        }
        
        float pDrift = drift.getPitchDrift();
        float cDrift = drift.getCutoffDrift();

        const float driftRatio = juce::jlimit(0.9f, 1.1f, 1.0f + pDrift);
        const float scaledDetune = currentDetune * 0.45f;
        const float detuneUp1   = std::pow(2.0f, (+scaledDetune * 0.55f) / 12.0f);
        const float detuneDown1 = std::pow(2.0f, (-scaledDetune * 0.55f) / 12.0f);
        const float detuneUp2   = std::pow(2.0f, (+scaledDetune * 1.10f) / 12.0f);
        const float detuneDown2 = std::pow(2.0f, (-scaledDetune * 1.10f) / 12.0f);

        float freq1    = currentBaseFrequency * driftRatio;
        float freq2    = currentBaseFrequency * detuneUp1 * driftRatio;
        float freqUni1 = currentBaseFrequency * detuneDown1 * driftRatio;
        float freqUni2 = currentBaseFrequency * detuneUp2 * driftRatio;
        float freqUni3 = currentBaseFrequency * detuneDown2 * driftRatio;
        float subFreq  = currentBaseFrequency * 0.5f * driftRatio; // -1 octave
        
        osc1.setFrequency(freq1);
        osc2.setFrequency(freq2);
        oscUnison1.setFrequency(freqUni1);
        oscUnison2.setFrequency(freqUni2);
        oscUnison3.setFrequency(freqUni3);
        subOsc.setFrequency(subFreq);
        
        auto processOsc = [](DPWOscillator& osc, SeedGenerator::OscMode mode, float mix) -> float {
            switch (mode)
            {
                case SeedGenerator::OscMode::Saw:   return osc.processSaw();
                case SeedGenerator::OscMode::Pulse: return osc.processPulse(0.5f);
                case SeedGenerator::OscMode::Morph: return osc.processRandom();
                case SeedGenerator::OscMode::Sine:  return osc.processSine();
            }
            return 0.0f;
        };

        float sig1    = processOsc(osc1, oscMode1, oscMorph);
        float sig2    = processOsc(osc2, oscMode2, oscMorph);
        float sigUni1 = processOsc(oscUnison1, oscMode1, oscMorph);
        float sigUni2 = processOsc(oscUnison2, oscMode2, oscMorph);
        float sigUni3 = processOsc(oscUnison3, oscMode1, oscMorph);
        float sigSub  = subOsc.processTriangle();
        
        // True 5-Voice Symmetrical Super-Unison with Constant-Power Panning Law (cos/sin)
        float effectivePan = 0.5f + (voicePan - 0.5f) * stereoWidth;
        float spread = 0.35f * stereoWidth;

        auto calcPan = [](float p) -> std::pair<float, float> {
            float norm = juce::jlimit(0.0f, 1.0f, p);
            float angle = norm * juce::MathConstants<float>::halfPi;
            return { std::cos(angle), std::sin(angle) };
        };

        auto [panCenterL, panCenterR] = calcPan(effectivePan);
        auto [panLeft1L,  panLeft1R]  = calcPan(effectivePan - spread * 0.55f);
        auto [panRight1L, panRight1R] = calcPan(effectivePan + spread * 0.55f);
        auto [panLeft2L,  panLeft2R]  = calcPan(effectivePan - spread);
        auto [panRight2L, panRight2R] = calcPan(effectivePan + spread);

        // Sub oscillator level balanced with unison stack (sub * 0.28f * subLevel)
        float sub = sigSub * (subLevel * 0.28f);

        float mixedL = 0.0f;
        float mixedR = 0.0f;

        if (unisonMode == 0) // 2-Voice Stereo (2VU)
        {
            mixedL = (sig1 * panLeft1L + sig2 * panRight1L) * 0.45f + sub * panCenterL;
            mixedR = (sig1 * panLeft1R + sig2 * panRight1R) * 0.45f + sub * panCenterR;
        }
        else // 5-Voice Symmetrical Super-Unison (5VU)
        {
            mixedL = (sig1 * panCenterL + 
                      sig2 * panRight1L + 
                      sigUni1 * panLeft1L + 
                      sigUni2 * panRight2L + 
                      sigUni3 * panLeft2L) * 0.22f + sub * panCenterL;

            mixedR = (sig1 * panCenterR + 
                      sig2 * panRight1R + 
                      sigUni1 * panLeft1R + 
                      sigUni2 * panRight2R + 
                      sigUni3 * panLeft2R) * 0.22f + sub * panCenterR;
        }
        
        // Non-linear Transistor Ladder Filter with continuous smooth cutoff tracking (zero clicks!)
        float fEnvVal = filterEnv.getNextSample(loopGridBoundary);
        float sCutoff = smoothedVoiceCutoff.getNextValue();
        float normCutoff = (std::log10(juce::jlimit(20.0f, 20000.0f, sCutoff)) - 1.30103f) / (4.30103f - 1.30103f);
        normCutoff = juce::jlimit(0.0f, 1.0f, normCutoff);

        float effectiveEnvAmount = filterEnvAmount * normCutoff;
        float currentCutoff = sCutoff + (effectiveEnvAmount * fEnvVal) + (cDrift * normCutoff);
        float maxSafeCutoff = (currentSampleRate > 0.0) ? std::min(19950.0f, (float)(currentSampleRate * 0.45)) : 19950.0f;
        currentCutoff = juce::jlimit(20.0f, maxSafeCutoff, currentCutoff);
        
        filter.setCutoffFrequencyHz(currentCutoff);
        
        // Continuous stereo filtering through transistor ladder (NO abrupt boolean bypass toggling!)
        float frame[2] = { mixedL, mixedR };
        float* channels[2] = { &frame[0], &frame[1] };
        juce::dsp::AudioBlock<float> filterBlock (channels, 2, 1);
        juce::dsp::ProcessContextReplacing<float> filterCtx (filterBlock);
        filter.process (filterCtx);
        float filteredL = frame[0];
        float filteredR = frame[1];
        
        // Normalise perceived loudness differences between oscillator types
        float gainScale = 1.0f;
        if (oscMode1 == SeedGenerator::OscMode::Sine)
        {
            // Was 1.7, which was tuned on a single note. In unison the detuned
            // sines drift in and out of phase and stack coherently - measured,
            // the mixer already peaks at the same level as a saw stack BEFORE
            // any compensation. The extra 1.7 then drove the ladder filter at
            // 1.76 instead of 1.03, and because the beating brings that peak
            // back around every few hundred milliseconds the distortion came and
            // went. That was the intermittent crackle heard only on Sine.
            //
            // 1.2 keeps most of the perceptual lift a harmonic-free waveform
            // needs without spending the headroom the unison stack has already
            // used up.
            gainScale = 1.2f;
        }
        else if (oscMode1 == SeedGenerator::OscMode::Saw)
            gainScale = 1.0f;
        else if (oscMode1 == SeedGenerator::OscMode::Pulse)
            gainScale = 0.82f;
        else if (oscMode1 == SeedGenerator::OscMode::Morph)
            gainScale = 1.0f + (1.0f - oscMorph) * 0.6f;

        // Filter make-up gain. These used to reach 2.0x and were multiplied
        // straight onto the oscillator's own compensation with nothing bounding
        // the product - Sine into a high-pass came out at 1.7 * 2.0 = 3.4, by far
        // the hottest path in the instrument, and it slammed the master saturator.
        // The detune beating brought that peak around every few hundred
        // milliseconds, which is why it was heard as intermittent crackle rather
        // than as constant distortion.
        float filterGainScale = 1.0f;
        if (filterType == 1) // HP
        {
            filterGainScale = 1.0f + 0.55f * normCutoff;
        }
        else if (filterType == 2) // BP
        {
            filterGainScale = 1.35f + 0.40f * std::abs(0.5f - normCutoff);
        }

        // Ceiling on the combined figure. No oscillator and filter pairing may
        // stack past this, whatever each one asks for on its own.
        constexpr float maxMakeupGain = 1.6f;
        gainScale = juce::jmin (gainScale * filterGainScale, maxMakeupGain);

        // Amp envelope with smoothed velocity (prevents click on velocity jump)
        float eVal = env.getNextSample(loopGridBoundary) * smoothedVelocity.getNextValue();

        float sampleL = filteredL * eVal * 0.22f * gainScale;
        float sampleR = filteredR * eVal * 0.22f * gainScale;
        
        if (outputBuffer.getNumChannels() >= 2)
        {
            outputBuffer.addSample(0, startSample + i, sampleL);
            outputBuffer.addSample(1, startSample + i, sampleR);
        }
        else
        {
            outputBuffer.addSample(0, startSample + i, (sampleL + sampleR) * 0.5f);
        }
    }
}
