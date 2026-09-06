#pragma once

#include <juce_dsp/juce_dsp.h>

class SignalLossPLC
{
public:
    SignalLossPLC() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = (spec.sampleRate > 0.0) ? spec.sampleRate : 44100.0;
        
        fftOrder = 9; // 2^9 = 512
        fftSize = 512;
        hopSize = 128; // 75% overlap
        
        fft = std::make_unique<juce::dsp::FFT>(fftOrder);

        fifoL.assign(fftSize, 0.0f);
        fifoR.assign(fftSize, 0.0f);
        fifoWritePos = 0;
        hopCounter = 0;

        fftBufferL.assign(fftSize * 2, 0.0f);
        fftBufferR.assign(fftSize * 2, 0.0f);

        lastGoodMagnitudeL.assign(fftSize / 2 + 1, 0.0f);
        lastGoodMagnitudeR.assign(fftSize / 2 + 1, 0.0f);

        faultyPhaseL.assign(fftSize / 2 + 1, 0.0f);
        faultyPhaseR.assign(fftSize / 2 + 1, 0.0f);

        binDecayL.resize(fftSize / 2 + 1);
        binDecayR.resize(fftSize / 2 + 1);

        juce::Random r(4321);
        for (size_t k = 0; k <= fftSize / 2; ++k)
        {
            // Gentle per-bin decay (0.925 to 0.995) so dropped STFT frames hold frozen magnitude textures without dying into silence!
            binDecayL[k] = 0.925f + 0.070f * r.nextFloat();
            binDecayR[k] = 0.925f + 0.070f * r.nextFloat();
        }

        outputBufferL.assign(fftSize * 2, 0.0f);
        outputBufferR.assign(fftSize * 2, 0.0f);
        outReadPos = 0;
        outWritePos = 0;
        currentLossKnob = 0.0f;

        dryDelayL.assign(fftSize, 0.0f);
        dryDelayR.assign(fftSize, 0.0f);
        dryDelayPos = 0;

        hpStateL = hpStateR = 0.0f;
        prevInL = prevInR = 0.0f;
        lpStateL = lpStateR = 0.0f;
    }

    /** Drops every sample the PLC is holding. Called when the plug-in goes idle,
        so nothing from before the silence can be replayed when audio resumes. */
    void reset()
    {
        std::fill(fifoL.begin(), fifoL.end(), 0.0f);
        std::fill(fifoR.begin(), fifoR.end(), 0.0f);
        std::fill(dryDelayL.begin(), dryDelayL.end(), 0.0f);
        std::fill(dryDelayR.begin(), dryDelayR.end(), 0.0f);
        std::fill(outputBufferL.begin(), outputBufferL.end(), 0.0f);
        std::fill(outputBufferR.begin(), outputBufferR.end(), 0.0f);
        std::fill(lastGoodMagnitudeL.begin(), lastGoodMagnitudeL.end(), 0.0f);
        std::fill(lastGoodMagnitudeR.begin(), lastGoodMagnitudeR.end(), 0.0f);
        std::fill(faultyPhaseL.begin(), faultyPhaseL.end(), 0.0f);
        std::fill(faultyPhaseR.begin(), faultyPhaseR.end(), 0.0f);

        fifoWritePos = 0;
        hopCounter   = 0;
        dryDelayPos  = 0;
        outReadPos   = 0;
        outWritePos  = 0;

        hpStateL = hpStateR = lpStateL = lpStateR = prevInL = prevInR = 0.0f;
    }

    void updateParameters(float signalLossAmount)
    {
        currentLossKnob = juce::jlimit(0.0f, 1.0f, signalLossAmount);

        // Frame-loss probability. This was quadratic * 0.65, so a knob at 50%
        // only dropped 16% of frames - the effect was inaudible until nearly
        // full. Now near-linear with an early lift, so 25% already drops about
        // a fifth of the frames.
        lossProbability = std::pow(currentLossKnob, 0.75f) * 0.90f;
    }

    void process(const juce::dsp::ProcessContextReplacing<float>& context)
    {
        auto& block = context.getOutputBlock();
        size_t numSamples = block.getNumSamples();
        size_t numChannels = block.getNumChannels();

        if (numChannels < 2)
            return;

        auto* dataL = block.getChannelPointer(0);
        auto* dataR = block.getChannelPointer(1);

        // Dynamic telephone bandpass. Held back into the upper half of the
        // knob (squared) and to gentler extremes, because the old linear sweep
        // to 380 Hz / 3.2 kHz gutted the level long before the packet-loss
        // artefacts themselves became audible.
        const float toneAmount = currentLossKnob * currentLossKnob;
        float hpCutoff = juce::jmap(toneAmount, 20.0f, 220.0f);
        float lpCutoff = juce::jmap(toneAmount, 20000.0f, 5200.0f);

        // Makeup for what that bandpass removes, so turning the knob up changes
        // the character rather than just turning the synth down.
        const float makeupGain = 1.0f + toneAmount * 1.15f;

        // FAST PATH: If bypassed/zero loss: only run 512-sample dry delay line. ZERO FFT overhead!
        if (currentLossKnob <= 0.001f)
        {
            for (size_t i = 0; i < numSamples; ++i)
            {
                float inL = dataL[i];
                float inR = dataR[i];

                dryDelayL[dryDelayPos] = inL;
                dryDelayR[dryDelayPos] = inR;
                size_t readPos = (dryDelayPos + 1) % fftSize;
                dataL[i] = dryDelayL[readPos];
                dataR[i] = dryDelayR[readPos];
                dryDelayPos = (dryDelayPos + 1) % fftSize;

                fifoL[fifoWritePos] = inL;
                fifoR[fifoWritePos] = inR;
                fifoWritePos = (fifoWritePos + 1) % fftSize;
            }
            // Keep the overlap-add pointers moving with real time even while
            // bypassed. If they stall here they drift out of step with each
            // other, and the moment Signal Loss is turned up the output is read
            // at the wrong offset - gaps and bursts that sound exactly like the
            // effect is stuck on.
            for (size_t i = 0; i < numSamples; ++i)
            {
                outputBufferL[outReadPos] = 0.0f;
                outputBufferR[outReadPos] = 0.0f;
                outReadPos = (outReadPos + 1) % (fftSize * 2);
            }
            outWritePos = (outWritePos + ((hopCounter + numSamples) / hopSize) * hopSize) % (fftSize * 2);

            hopCounter = (hopCounter + numSamples) % hopSize;
            hpStateL = hpStateR = lpStateL = lpStateR = prevInL = prevInR = 0.0f;
            return;
        }

        float dt = 1.0f / (float)currentSampleRate;

        float rcHp = 1.0f / (juce::MathConstants<float>::twoPi * hpCutoff);
        float alphaHp = rcHp / (rcHp + dt);

        float rcLp = 1.0f / (juce::MathConstants<float>::twoPi * lpCutoff);
        float alphaLp = dt / (rcLp + dt);

        for (size_t i = 0; i < numSamples; ++i)
        {
            float inL = dataL[i];
            float inR = dataR[i];

            // Maintain exact 512-sample delay line for dry signal (matching STFT OLA latency)
            dryDelayL[dryDelayPos] = inL;
            dryDelayR[dryDelayPos] = inR;
            size_t readPos = (dryDelayPos + 1) % fftSize;
            float delayedDryL = dryDelayL[readPos];
            float delayedDryR = dryDelayR[readPos];
            dryDelayPos = (dryDelayPos + 1) % fftSize;

            // Circular analysis buffer: the STFT window must be the LAST
            // fftSize samples, sliding by hopSize each frame.
            fifoL[fifoWritePos] = inL;
            fifoR[fifoWritePos] = inR;
            fifoWritePos = (fifoWritePos + 1) % fftSize;

            float plcL = outputBufferL[outReadPos];
            float plcR = outputBufferR[outReadPos];

            outputBufferL[outReadPos] = 0.0f;
            outputBufferR[outReadPos] = 0.0f;

            outReadPos = (outReadPos + 1) % (fftSize * 2);

            if (++hopCounter >= hopSize)
            {
                hopCounter = 0;
                processSTFTFrame();
            }

            // Wet/dry blend. A clean (non-lost) PLC frame resynthesises the dry
            // signal, so pushing this to fully wet early costs nothing and lets
            // the dropouts come through at full strength instead of half-mixed.
            float wetMix = juce::jlimit(0.0f, 1.0f, currentLossKnob * 1.8f);
            float combinedL = delayedDryL * (1.0f - wetMix) + plcL * wetMix;
            float combinedR = delayedDryR * (1.0f - wetMix) + plcR * wetMix;

            // Apply 1-pole Highpass Filter (Low Cut: removing sub bass up to 380 Hz)
            hpStateL = alphaHp * (hpStateL + combinedL - prevInL);
            hpStateR = alphaHp * (hpStateR + combinedR - prevInR);
            prevInL = combinedL;
            prevInR = combinedR;

            // Apply 1-pole Lowpass Filter (High Cut: rolling off highs down to 3.2 kHz)
            lpStateL += alphaLp * (hpStateL - lpStateL);
            lpStateR += alphaLp * (hpStateR - lpStateR);

            // Subtle telephone speaker warmth saturation
            float teleCoeff = currentLossKnob * 0.25f;
            dataL[i] = std::tanh(lpStateL * (1.0f + teleCoeff)) * makeupGain;
            dataR[i] = std::tanh(lpStateR * (1.0f + teleCoeff)) * makeupGain;
        }
    }

private:
    void processSTFTFrame()
    {
        // Check if this STFT frame suffers Packet Loss.
        juce::Random& r = frameRandom;
        bool isLost = (r.nextFloat() < lossProbability);

        size_t numBins = fftSize / 2 + 1;

        // Windowed analysis frame, read oldest -> newest out of the circular
        // buffer. fifoWritePos is where the NEXT sample goes, so it is also the
        // oldest sample in the buffer.
        std::fill(fftBufferL.begin(), fftBufferL.end(), 0.0f);
        std::fill(fftBufferR.begin(), fftBufferR.end(), 0.0f);

        float maxMag = 0.0f;
        for (size_t i = 0; i < fftSize; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)i / (float)fftSize);
            const size_t src = (fifoWritePos + i) % fftSize;
            float l = fifoL[src];
            float r = fifoR[src];
            fftBufferL[i] = l * w;
            fftBufferR[i] = r * w;
            maxMag = std::max(maxMag, std::max(std::abs(l), std::abs(r)));
        }

        // Fast silence bypass: skip FFTs if frame is completely silent and not currently concealing lost frames
        if (maxMag < 1e-6f && !isLost)
        {
            std::fill(lastGoodMagnitudeL.begin(), lastGoodMagnitudeL.end(), 0.0f);
            std::fill(lastGoodMagnitudeR.begin(), lastGoodMagnitudeR.end(), 0.0f);
            return;
        }

        fft->performRealOnlyForwardTransform(fftBufferL.data());
        fft->performRealOnlyForwardTransform(fftBufferR.data());

        if (!isLost)
        {
            // Clean frame: Store last good magnitude spectrum & phases
            for (size_t k = 0; k < numBins; ++k)
            {
                float reL = fftBufferL[2 * k];
                float imL = fftBufferL[2 * k + 1];
                lastGoodMagnitudeL[k] = std::sqrt(reL * reL + imL * imL);
                faultyPhaseL[k] = std::atan2(imL, reL);

                float reR = fftBufferR[2 * k];
                float imR = fftBufferR[2 * k + 1];
                lastGoodMagnitudeR[k] = std::sqrt(reR * reR + imR * imR);
                faultyPhaseR[k] = std::atan2(imR, reR);
            }
        }
        else
        {
            // PACKET LOSS EVENT: STFT Frame Concealment Engine
            // 1. Magnitude continuation with independent per-bin decay
            // 2. Faulty phase propagation (phase smearing with error estimation)
            for (size_t k = 0; k < numBins; ++k)
            {
                // Continue & decay magnitude spectrum smoothly
                lastGoodMagnitudeL[k] *= binDecayL[k];
                lastGoodMagnitudeR[k] *= binDecayR[k];

                // Propagate phase with faulty estimate (random phase jitter)
                float phaseAdvance = (float)k * 0.12f + (r.nextFloat() - 0.5f) * 0.35f;
                faultyPhaseL[k] += phaseAdvance;
                faultyPhaseR[k] += phaseAdvance;

                // Synthesize corrupted STFT spectrum bin
                fftBufferL[2 * k]     = lastGoodMagnitudeL[k] * std::cos(faultyPhaseL[k]);
                fftBufferL[2 * k + 1] = lastGoodMagnitudeL[k] * std::sin(faultyPhaseL[k]);

                fftBufferR[2 * k]     = lastGoodMagnitudeR[k] * std::cos(faultyPhaseR[k]);
                fftBufferR[2 * k + 1] = lastGoodMagnitudeR[k] * std::sin(faultyPhaseR[k]);
            }
        }

        // Inverse FFT & Overlap-Add with mathematically exact COLA Hann^2 scale (1 / 1.5 = 2/3)
        fft->performRealOnlyInverseTransform(fftBufferL.data());
        fft->performRealOnlyInverseTransform(fftBufferR.data());

        const float colaScale = 2.0f / 3.0f; // Exact Hann^2 COLA sum normalization for 75% overlap (hop = N/4)
        size_t writeHead = outWritePos;
        for (size_t i = 0; i < fftSize; ++i)
        {
            float window = (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)i / (float)fftSize));
            size_t idx = (writeHead + i) % (fftSize * 2);
            outputBufferL[idx] += fftBufferL[i] * window * colaScale;
            outputBufferR[idx] += fftBufferR[i] * window * colaScale;
        }

        outWritePos = (outWritePos + hopSize) % (fftSize * 2);
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    size_t fftOrder = 9;
    size_t fftSize = 512;
    size_t hopSize = 128;

    std::vector<float> fifoL, fifoR;
    size_t fifoWritePos = 0;
    size_t hopCounter = 0;
    juce::Random frameRandom { 0x57584A };

    std::vector<float> dryDelayL, dryDelayR;
    size_t dryDelayPos = 0;

    std::vector<float> fftBufferL, fftBufferR;
    std::vector<float> lastGoodMagnitudeL, lastGoodMagnitudeR;
    std::vector<float> faultyPhaseL, faultyPhaseR;
    std::vector<float> binDecayL, binDecayR;

    std::vector<float> outputBufferL, outputBufferR;
    size_t outReadPos = 0;
    size_t outWritePos = 0;

    float lossProbability = 0.0f;
    float currentLossKnob = 0.0f;
    double currentSampleRate = 44100.0;

    // 1-Pole Lowpass & Highpass state variables for Telephone Bandpass Range Filter
    float hpStateL = 0.0f, hpStateR = 0.0f;
    float prevInL = 0.0f, prevInR = 0.0f;
    float lpStateL = 0.0f, lpStateR = 0.0f;
};

class SpaceEffects
{
public:
    SpaceEffects() = default;
    
    void prepare(const juce::dsp::ProcessSpec& spec);
    void updateParameters(float signalLoss, float reverbMix, float delayMix, 
                          float delayFeedback, float delayTone,
                          float autopanAmount, bool autopanRate16th, bool autopanShapeSaw,
                          float chorusDepth);
    void process(const juce::dsp::ProcessContextReplacing<float>& context, double currentBpm);

    /** Clears every tail the effects are holding: delay lines, reverb, chorus and
        the packet-loss engine. */
    void reset();
    
private:
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters reverbParams;
    SignalLossPLC signalLossPLC;
    
    // Stereo delay: different times for L and R
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayL { 88200 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayR { 88200 };
    
    float currentDelayMix = 0.0f;
    float currentFeedback = 0.4f;
    float currentDelayTone = 0.0f;
    
    // 1-pole filter state for delay feedback loop
    float delayFilterStateL = 0.0f;
    float delayFilterStateR = 0.0f;
    float delayFilterCoeff = 1.0f;
    
    // Autopan parameters
    float currentAutopanAmount = 0.0f;
    bool currentAutopanRate16th = false;
    bool currentAutopanShapeSaw = false;
    float lfoPhase = 0.0f;
    float smoothedPanPos = 0.5f;
    
    double currentSampleRate = 44100.0;
};
