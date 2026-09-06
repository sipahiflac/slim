#include "Effects.h"

void SpaceEffects::prepare(const juce::dsp::ProcessSpec& spec)
{
    chorus.prepare(spec);
    chorus.setRate(0.18f);
    chorus.setCentreDelay(7.0f);
    chorus.setFeedback(0.08f);
    chorus.setMix(0.0f);

    reverb.prepare(spec);
    signalLossPLC.prepare(spec);
    
    delayL.prepare(spec);
    delayR.prepare(spec);
    
    // Stereo ping-pong feel: L=375ms, R=500ms (3/8 and 1/2 of ~120bpm)
    double sr = (spec.sampleRate > 0.0) ? spec.sampleRate : 44100.0;
    delayL.setMaximumDelayInSamples((int)(sr * 2.0));
    delayR.setMaximumDelayInSamples((int)(sr * 2.0));
    delayL.setDelay((float)(sr * 0.375));
    delayR.setDelay((float)(sr * 0.500));

    currentSampleRate = sr;
    delayFilterStateL = 0.0f;
    delayFilterStateR = 0.0f;
    smoothedPanPos = 0.5f;
}

void SpaceEffects::reset()
{
    delayL.reset();
    delayR.reset();
    reverb.reset();
    chorus.reset();
    signalLossPLC.reset();

    delayFilterStateL = 0.0f;
    delayFilterStateR = 0.0f;
    smoothedPanPos = 0.5f;
    lfoPhase = 0.0f;
}

void SpaceEffects::updateParameters(float signalLoss, float reverbMix, float delayMix, 
                                    float delayFeedback, float delayTone,
                                    float autopanAmount, bool autopanRate16th, bool autopanShapeSaw,
                                    float chorusDepth)
{
    // Reverb update
    reverbParams.roomSize = 0.65f;
    reverbParams.damping = 0.50f;
    reverbParams.width = 1.0f;
    reverbParams.wetLevel = reverbMix * 0.80f;
    reverbParams.dryLevel = 1.0f - (reverbMix * 0.30f);
    reverb.setParameters(reverbParams);

    // Signal Loss (Packet Loss Concealment) update
    signalLossPLC.updateParameters(signalLoss);
    
    // Delay parameter update
    currentDelayMix = delayMix;
    currentFeedback = juce::jlimit(0.0f, 0.85f, delayFeedback);
    currentDelayTone = delayTone;

    // Calculate 1-pole lowpass coefficient for delay tone damping with strict division safety
    float cutoff = juce::jmap(currentDelayTone, -100.0f, 0.0f, 400.0f, 20000.0f);
    double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;
    float dt = 1.0f / (float)sr;
    float rc = 1.0f / (juce::MathConstants<float>::twoPi * std::max(20.0f, cutoff));
    float rawCoeff = dt / (rc + dt);
    if (std::isnan(rawCoeff) || std::isinf(rawCoeff))
        delayFilterCoeff = 1.0f;
    else
        delayFilterCoeff = juce::jlimit(0.0001f, 1.0f, rawCoeff);

    // Autopan parameter update
    currentAutopanAmount = autopanAmount;
    currentAutopanRate16th = autopanRate16th;
    currentAutopanShapeSaw = autopanShapeSaw;

    // Chorus update
    chorus.setDepth(juce::jlimit(0.0f, 1.0f, chorusDepth));
    chorus.setMix(juce::jlimit(0.0f, 0.5f, chorusDepth * 0.45f));
}

void SpaceEffects::process(const juce::dsp::ProcessContextReplacing<float>& context, double currentBpm)
{
    auto& outputBlock = context.getOutputBlock();
    auto numSamples = outputBlock.getNumSamples();
    auto numChannels = outputBlock.getNumChannels();
    
    if (numChannels >= 2)
    {
        auto* dataL = outputBlock.getChannelPointer(0);
        auto* dataR = outputBlock.getChannelPointer(1);
        
        // Calculate LFO frequency
        float lfoFreq = ((float) currentBpm / 60.0f) * (currentAutopanRate16th ? 4.0f : 2.0f);
        float phaseIncrement = lfoFreq / (float) currentSampleRate;

        // Loop-invariant: this was recomputed (with a divide) on every sample.
        const float panSmoothCoeff = 1.0f / (0.005f * (float) currentSampleRate + 1.0f);

        for (size_t i = 0; i < numSamples; ++i)
        {
            float inL = dataL[i];
            float inR = dataR[i];
            
            float delayedL = delayL.popSample(0);
            float delayedR = delayR.popSample(0);
            
            // Apply 1-pole lowpass filter to delay feedback loop paths
            float fbL = delayedR * currentFeedback;
            float fbR = delayedL * currentFeedback;
            
            delayFilterStateL = fbL * delayFilterCoeff + delayFilterStateL * (1.0f - delayFilterCoeff);
            delayFilterStateR = fbR * delayFilterCoeff + delayFilterStateR * (1.0f - delayFilterCoeff);

            if (std::isnan(delayFilterStateL) || std::isinf(delayFilterStateL)) delayFilterStateL = 0.0f;
            if (std::isnan(delayFilterStateR) || std::isinf(delayFilterStateR)) delayFilterStateR = 0.0f;

            // Feed filtered delayed signal back into delay lines (ping-pong crossfeed)
            delayL.pushSample(0, inL + delayFilterStateL);
            delayR.pushSample(0, inR + delayFilterStateR);
            
            // Mix dry signal and filtered delay return
            float outL = inL + delayedL * currentDelayMix;
            float outR = inR + delayedR * currentDelayMix;

            // Autopan LFO phase increment and LFO shape resolution
            lfoPhase += phaseIncrement;
            if (lfoPhase >= 1.0f)
                lfoPhase -= 1.0f;

            float lfoVal = 0.0f;
            if (currentAutopanShapeSaw)
                lfoVal = lfoPhase * 2.0f - 1.0f; // Saw [-1.0, 1.0]
            else
                lfoVal = std::sin(lfoPhase * juce::MathConstants<float>::twoPi); // Sine [-1.0, 1.0]

            // Apply Constant-Power Panning
            float pan = lfoVal * currentAutopanAmount; // Range [-currentAutopanAmount, currentAutopanAmount]
            float panPos = pan * 0.5f + 0.5f; // Map to [0.0 (left), 1.0 (right)]

            // Smooth panning position to prevent clicky sounds on LFO wraps (using a 5ms time constant)
            smoothedPanPos = panPos * panSmoothCoeff + smoothedPanPos * (1.0f - panSmoothCoeff);

            // Constant-power law, normalised so that dead centre is unity gain.
            // Without the sqrt(2) the plug-in lost a constant 3 dB even with
            // Autopan fully off, because centre panning gives 0.707 per side.
            float gainL = std::sqrt(1.0f - smoothedPanPos) * juce::MathConstants<float>::sqrt2;
            float gainR = std::sqrt(smoothedPanPos)        * juce::MathConstants<float>::sqrt2;

            dataL[i] = outL * gainL;
            dataR[i] = outR * gainR;
        }
    }
    else if (numChannels == 1)
    {
        auto* data = outputBlock.getChannelPointer(0);
        for (size_t i = 0; i < numSamples; ++i)
        {
            float in = data[i];
            float delayed = delayL.popSample(0);
            
            float fb = delayed * currentFeedback;
            delayFilterStateL = fb * delayFilterCoeff + delayFilterStateL * (1.0f - delayFilterCoeff);

            delayL.pushSample(0, in + delayFilterStateL);
            data[i] = in + delayed * currentDelayMix;
        }
    }
    
    chorus.process(context);
    reverb.process(context);
    signalLossPLC.process(context);
}
