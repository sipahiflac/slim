#include "Voice.h"
#include "ToneCard.h"
#include <cmath>
#include <algorithm>

namespace
{
    // Every timing constant below (attack ms, decay ms, retrigger tail ms,
    // parameter smoothing ms) was originally tuned as a raw sample count at
    // 16 kHz. This ratio rescales them so they still mean the same
    // real-world duration now that the engine runs at kEngineSampleRateF.
    const float kRateScale = kEngineSampleRateF / 16000.0f;

    // ~12.5 ms one-pole smoothing for cutoff/volume parameter changes.
    const float kParamSmoothAlpha = 1.0f - std::exp(-1.0f / (0.0125f * kEngineSampleRateF));

    // A short perceived-level follower used for stable voice stealing. An
    // instantaneous sample would call every zero crossing "the quietest".
    const float kAudibilityAlpha = 1.0f - std::exp(-1.0f / (0.030f * kEngineSampleRateF));
}

Voice::Voice()
{
}

void Voice::Resonator::configure(float frequency, float q) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    const float w = 2.0f * pi * std::clamp(frequency, 80.0f, 7600.0f) / kEngineSampleRateF;
    const float alpha = std::sin(w) / (2.0f * q);
    const float a0 = 1.0f + alpha;
    b0 = alpha / a0;
    b2 = -alpha / a0;
    a1 = (-2.0f * std::cos(w)) / a0;
    a2 = (1.0f - alpha) / a0;
}

void Voice::setSampleRate(float /*rate*/)
{
    // Engine is strictly locked to kEngineSampleRateF
}

void Voice::updateParams(const VoiceParams& p)
{
    currentParams = p;
    currentParams.clampAll();

    const float attackT = (currentParams.attack - 1.0f) / 9.0f;
    const float attackSeconds = 0.002f * std::pow(1000.0f, attackT); // 2 ms .. 2 s, logarithmic time.
    // The analog-like rise approaches 1.05 and reaches 1 at the chosen time.
    attackCoefficient = -std::expm1(-std::log(21.0f) / (attackSeconds * kEngineSampleRateF));

    releaseFull = currentParams.release >= 9.999f;
    const float releaseT = (std::min(currentParams.release, 9.0f) - 1.0f) / 8.0f;
    const float releaseSeconds = 0.02f * std::pow(200.0f, releaseT); // 20 ms .. 4 s
    releaseFactor = releaseFull ? 1.0f
        : std::exp(std::log(0.0005f) / (releaseSeconds * kEngineSampleRateF));

    const float brightT = (currentParams.brightness - 1.0f) / 9.0f;
    const float cutoffHz = 100.0f * std::pow(7200.0f / 100.0f, brightT);
    targetCutoffLog = std::log(cutoffHz);
    targetFilterMix = currentParams.brightness >= 9.999f ? 0.0f : 1.0f;
    // Value 5 is the exact zero point; either end reaches only +/-5 cents.
    const float detuneCents = currentParams.detune <= 5.0f
        ? (currentParams.detune - 5.0f) * 1.25f
        : (currentParams.detune - 5.0f);
    detuneRatio = std::pow(2.0, detuneCents / 1200.0);
    const float noiseT = (currentParams.noise - 1.0f) / 9.0f;
    constexpr float noiseCurve = 4.0f;
    noiseGain = 0.21f * (std::exp(noiseCurve * noiseT) - 1.0f)
                      / (std::exp(noiseCurve) - 1.0f);
    const float qualityT = (currentParams.quality - 1.0f) / 9.0f;
    qualityHoldSamples = std::max(1, (int)std::lround(10.0f * std::pow(0.1f, qualityT)));
    const float bits = 3.0f + 9.0f * qualityT;
    qualityStep = 2.0f / (std::pow(2.0f, bits) - 1.0f); // 3..12 bit
    const float formantT = (currentParams.formant - 1.0f) / 9.0f;
    formantMix = 0.12f + 0.38f * formantT;
    formantLow.configure(500.0f * std::pow(1600.0f / 500.0f, formantT), 3.2f);
    formantHigh.configure(950.0f * std::pow(3400.0f / 950.0f, formantT), 4.0f);
}

void Voice::noteOn(int midiNote, float velocity, const Instrument* instrument, const VoiceParams& params)
{
    menuPreview = false;
    currentNote = midiNote;
    endFadeRemaining = 0; endFadeOutput = 0.0f;
    currentInstrument = instrument;
    updateParams(params);

    targetVolume = std::clamp(velocity, 0.0f, 1.0f);
    if (!active)
    {
        smoothedVolume = 0.0f;
    }
    smoothedCutoffLog = targetCutoffLog;
    cutoffG = std::tan(3.14159265358979323846f * std::exp(smoothedCutoffLog) / kEngineSampleRateF);
    smoothedFilterMix = targetFilterMix;
    brightnessLow.reset();
    brightnessHigh.reset();

    // Pitch shifting intentionally cheap: linear interpolation, NO anti-aliasing
    float baseNote = (instrument != nullptr) ? instrument->baseNote : 60;
    if (instrument != nullptr && instrument->liveBaseNote != nullptr)
        baseNote = instrument->liveBaseNote->load(std::memory_order_relaxed);
    pitchRatio = std::pow(2.0, (midiNote - baseNote) / 12.0) * detuneRatio;

    const float normalStart = instrument != nullptr && instrument->playbackStart != nullptr
        ? std::clamp(instrument->playbackStart->load(std::memory_order_relaxed), 0.0f, 0.99f)
        : 0.0f;
    tickPos = instrument != nullptr && instrument->body.empty()
        ? normalStart * (double)instrument->tick.size() : 0.0;
    bodyPos = instrument != nullptr
        ? normalStart * (double)instrument->body.size() : 0.0;
    formantLow.reset();
    formantHigh.reset();
    qualityCounter = 0;
    heldQualitySample = 0.0f;
    age = 0;
    envLevel = 0.0f;
    envStage = EnvStage::ATTACK;
    loopReleased = false;
    loopCrossfading = false;
    active = true;

    // Movement layer initialization with random phase/state per voice note
    prngState ^= (uint32_t)(midiNote * 2654435761u + (uint32_t)age);
    pitchNoiseState1 = nextRandomSigned() * 0.5f;
    pitchNoiseState2 = nextRandomSigned() * 0.5f;
    brightNoiseState1 = nextRandomSigned() * 0.5f;
    brightNoiseState2 = nextRandomSigned() * 0.5f;

}

void Voice::noteOff(int midiNote)
{
    if (active && (currentNote == midiNote || midiNote < 0))
    {
        loopReleased = true;
        envStage = releaseFull ? EnvStage::SUSTAIN_DECAY : EnvStage::RELEASE;
    }
}

void Voice::fadeOut(float seconds)
{
    if (!active)
        return;

    const float fadeSamples = std::max(1.0f, seconds * kEngineSampleRateF);
    releaseFactor = std::exp(std::log(0.0005f) / fadeSamples);
    envStage = EnvStage::RELEASE;
}

void Voice::stop()
{
    active = false;
    endFadeRemaining = 0; endFadeOutput = 0.0f;
    menuPreview = false;
    envStage = EnvStage::OFF;
    envLevel = 0.0f;
    brightnessLow.reset();
    brightnessHigh.reset();
    formantLow.reset();
    formantHigh.reset();
    qualityCounter = 0;
    heldQualitySample = 0.0f;
    lastOutput = 0.0f;
    audibility = 0.0f;
    currentNote = -1;
}

float Voice::renderSample(bool movementEnabled)
{
    if (!active || currentInstrument == nullptr)
        return 0.0f;

    if (endFadeRemaining > 0) return renderEndFade();
    age++;

    // 1. Amplitude envelope
    if (envStage == EnvStage::ATTACK)
    {
        envLevel += attackCoefficient * (1.05f - envLevel);
        if (envLevel >= 1.0f)
        {
            envLevel = 1.0f;
            envStage = EnvStage::SUSTAIN_DECAY;
        }
    }
    else if (envStage == EnvStage::SUSTAIN_DECAY)
    {
        if (envLevel < 0.0005f)
        {
            endFadeRemaining = std::max(2, (int)(0.004f * kEngineSampleRateF));
            endFadeOutput = lastOutput;
            return renderEndFade();
        }
    }
    else if (envStage == EnvStage::RELEASE)
    {
        envLevel *= releaseFactor;
        if (envLevel < 0.0005f)
        {
            endFadeRemaining = std::max(2, (int)(0.004f * kEngineSampleRateF));
            endFadeOutput = lastOutput;
            return renderEndFade();
        }
    }

    // 2. Smooth cutoff in log Hz, volume and the fully-open bypass mix.
    smoothedCutoffLog += kParamSmoothAlpha * (targetCutoffLog - smoothedCutoffLog);
    if ((age & 15u) == 0u)
        cutoffG = std::tan(3.14159265358979323846f * std::exp(smoothedCutoffLog) / kEngineSampleRateF);
    smoothedFilterMix += kParamSmoothAlpha * (targetFilterMix - smoothedFilterMix);
    smoothedVolume += kParamSmoothAlpha * (targetVolume - smoothedVolume);

    // 3. Movement Layer: Pitch & Brightness smooth irregular drift (0.5 - 3 Hz)
    double effectivePitchRatio = pitchRatio;
    float effectiveCutoffG = cutoffG;

    if (movementEnabled)
    {
        // Low-pass filtered noise generators, rates tuned in Hz (not raw
        // per-sample alpha) so they stay ~0.8 Hz / ~2.2 Hz regardless of
        // engine sample rate: alpha ~= 2*pi*fc/fs, so these were originally
        // measured at 16 kHz and must be divided down as fs grows.
        const float kAlphaSlow = 0.00031f / kRateScale;
        const float kAlphaFast = 0.00086f / kRateScale;

        // Pitch drift noise
        pitchNoiseState1 += kAlphaSlow * (nextRandomSigned() - pitchNoiseState1);
        pitchNoiseState2 += kAlphaFast * (nextRandomSigned() - pitchNoiseState2);
        float pitchDriftVal = (pitchNoiseState1 + pitchNoiseState2); // approximately [-1.0, 1.0]

        // Pitch drift depth: ~±7 cents default. 1 cent = 2^(1/1200)
        // Ratio factor = 2^(cents / 1200) ≈ 1.0 + (cents * ln(2) / 1200)
        double centsOffset = (double)(pitchDriftVal * pitchDriftDepthCents);
        double pitchModFactor = std::pow(2.0, centsOffset / 1200.0);
        effectivePitchRatio = pitchRatio * pitchModFactor;

        // Brightness drift noise (independent random source), same Hz-rate
        // scaling as the pitch drift above.
        const float kBAlpha1 = 0.00024f / kRateScale; // ~0.6 Hz
        const float kBAlpha2 = 0.00071f / kRateScale; // ~1.8 Hz
        brightNoiseState1 += kBAlpha1 * (nextRandomSigned() - brightNoiseState1);
        brightNoiseState2 += kBAlpha2 * (nextRandomSigned() - brightNoiseState2);
        float brightDriftVal = (brightNoiseState1 + brightNoiseState2); // approximately [-1.0, 1.0]

        // Brightness depth: ±15% of cutoff frequency
        float brightModFactor = 1.0f + (brightDriftVal * brightnessDriftRatio);
        effectiveCutoffG = std::clamp(cutoffG * brightModFactor, 0.01f, 7.0f);
    }

    // 4. Sample reading: tick (one-shot) + body (looped)
    // Linear interpolation, no anti-aliasing
    float sampleTick = 0.0f;
    const auto& tickBuf = currentInstrument->tick;
    if (!tickBuf.empty())
    {
        size_t tLen = tickBuf.size();
        const float normalEnd = currentInstrument->playbackEnd != nullptr
            ? std::clamp(currentInstrument->playbackEnd->load(std::memory_order_relaxed), 0.01f, 1.0f)
            : 1.0f;
        const size_t tickEnd = currentInstrument->body.empty()
            ? std::clamp((size_t)std::lround(normalEnd * (double)tLen), (size_t)1, tLen)
            : tLen;
        if (tickPos < (double)(tickEnd - 1))
        {
            size_t i0 = (size_t)tickPos;
            size_t i1 = i0 + 1;
            float frac = (float)(tickPos - (double)i0);
            sampleTick = (1.0f - frac) * tickBuf[i0] + frac * tickBuf[i1];
            tickPos += effectivePitchRatio;
        }
        else if (tickPos < (double)tickEnd)
        {
            sampleTick = tickBuf[(size_t)tickPos];
            tickPos += effectivePitchRatio;
        }
        else if (currentInstrument->body.empty())
        {
            // Tick-only instrument finished its one-shot playback
            endFadeRemaining = std::max(2, (int)(0.004f * kEngineSampleRateF));
            endFadeOutput = lastOutput;
            return renderEndFade();
        }
    }

    float sampleBody = 0.0f;
    const bool rawSource = currentInstrument->useOriginalSource != nullptr
                        && currentInstrument->useOriginalSource->load(std::memory_order_relaxed)
                        && !currentInstrument->originalBody.empty();
    const auto& bodyBuf = rawSource ? currentInstrument->originalBody : currentInstrument->body;
    if (!bodyBuf.empty())
    {
        const size_t bLen = bodyBuf.size();
        const float normalStart = currentInstrument->playbackStart != nullptr
            ? std::clamp(currentInstrument->playbackStart->load(std::memory_order_relaxed), 0.0f, 0.99f)
            : 0.0f;
        const float normalEnd = currentInstrument->playbackEnd != nullptr
            ? std::clamp(currentInstrument->playbackEnd->load(std::memory_order_relaxed), normalStart + 0.01f, 1.0f)
            : 1.0f;
        const int playStart = std::clamp((int)std::lround(normalStart * (double)bLen), 0, (int)bLen - 1);
        const int playEnd = std::clamp((int)std::lround(normalEnd * (double)bLen), playStart + 1, (int)bLen);
        if (bodyPos < playStart) bodyPos = playStart;
        const auto readLinear = [&bodyBuf, playStart, playEnd](double position)
        {
            position = std::clamp(position, (double)playStart, (double)playEnd - 1.0);
            const size_t i0 = (size_t)position;
            const size_t i1 = std::min(i0 + 1, (size_t)playEnd - 1);
            const float fraction = (float)(position - (double)i0);
            return bodyBuf[i0] + fraction * (bodyBuf[i1] - bodyBuf[i0]);
        };

        int selected = currentInstrument->selectedLoop != nullptr
            ? currentInstrument->selectedLoop->load(std::memory_order_relaxed) : 0;
        int lStart = 0, lEnd = 0, crossfade = 0;
        float seamCorrelation = 1.0f;
        bool validLoop = selected > 0 && selected <= (int)currentInstrument->loopRegions.size();
        if (validLoop)
        {
            const auto region = currentInstrument->loopRegions[(size_t)selected - 1];
            lStart = region.start;
            lEnd = region.end;
            const int wanted = region.crossfade > 0 ? region.crossfade
                                                    : (int)(0.012f * kEngineSampleRateF);
            // The blend reads the span just before lStart, which a trimmed
            // playback start may have cut into.
            crossfade = std::min({ wanted, (lEnd - lStart) / 2, lStart - playStart });
            seamCorrelation = std::clamp(region.seamCorrelation, 0.0f, 1.0f);
            validLoop = lStart >= playStart && lEnd > lStart + 8 && lEnd <= playEnd && crossfade >= 4;
        }
        if (!validLoop)
            loopCrossfading = false;

        if (bodyPos < (double)playEnd)
        {
            if (validLoop)
            {
                // The last `crossfade` samples before lEnd blend with the same
                // span before lStart -- exactly one loop length apart, the lag
                // the loop was matched at -- and playback then carries on from
                // lStart. No new seam starts after release, so a released note
                // simply continues into the recording's own tail.
                const int loopLength = lEnd - lStart;
                const double fadeStart = (double)(lEnd - crossfade);
                if (!loopReleased && !loopCrossfading && bodyPos >= (double)lEnd)
                    bodyPos = lStart + std::fmod(bodyPos - lEnd, (double)loopLength);
                if (bodyPos >= fadeStart && bodyPos < (double)lEnd && (loopCrossfading || !loopReleased))
                {
                    loopCrossfading = true;
                    // Equal-power sine/cosine law, rescaled by how alike the two
                    // spans are: correlated material sums like a linear fade,
                    // uncorrelated breath noise keeps its power instead of dipping.
                    const float t = std::clamp((float)((bodyPos - fadeStart) / crossfade), 0.0f, 1.0f);
                    constexpr float halfPi = 1.57079632679f;
                    const float fadeIn = std::sin(halfPi * t), fadeOut = std::cos(halfPi * t);
                    const float norm = 1.0f / std::sqrt(1.0f + seamCorrelation * 2.0f * fadeIn * fadeOut);
                    sampleBody = (readLinear(bodyPos) * fadeOut + readLinear(bodyPos - loopLength) * fadeIn) * norm;
                }
                else sampleBody = readLinear(bodyPos);
                bodyPos += effectivePitchRatio;
                if (loopCrossfading && bodyPos >= (double)lEnd)
                {
                    bodyPos -= loopLength;
                    loopCrossfading = false;
                }
            }
            else
            {
                sampleBody = readLinear(bodyPos);
                bodyPos += effectivePitchRatio;
            }
        }
        else if (tickPos >= (double)tickBuf.size())
        {
            endFadeRemaining = std::max(2, (int)(0.004f * kEngineSampleRateF));
            endFadeOutput = lastOutput;
            return renderEndFade();
        }
    }

    // Mix tick and body
    float raw = (sampleTick * 0.76f) + (sampleBody * 0.88f) + nextRandomSigned() * noiseGain;

    // 5. Sample-rate and bit-depth reduction. Value 10 remains effectively clean.
    if (qualityCounter-- <= 0)
    {
        heldQualitySample = qualityStep > 0.0f ? std::round(raw / qualityStep) * qualityStep : raw;
        qualityCounter = qualityHoldSamples - 1;
    }
    raw = heldQualitySample;

    // 6. Cascaded Butterworth stages: a useful 24 dB/octave cutoff instead of
    // the former gentle one-pole roll-off. Open is a smoothed unity bypass.
    const float lowpass = brightnessHigh.process(
        brightnessLow.process(raw, effectiveCutoffG, 1.847759065f),
        effectiveCutoffG, 0.765366865f);
    const float filtered = raw + smoothedFilterMix * (lowpass - raw);

    // 7. Two moving resonant bands provide a compact telephone/formant colour.
    const float resonant = formantLow.process(filtered) * 0.72f
                         + formantHigh.process(filtered) * 0.46f;
    const float shaped = filtered * (1.0f - formantMix) + resonant * formantMix;

    // 8. Amplitude envelope. The engine crossfades a copied old voice when
    // this slot is retriggered or stolen, preserving its real waveform.
    float output = std::clamp(shaped, -1.15f, 1.15f) * envLevel * smoothedVolume
                 * currentInstrument->outputGain;
    output = std::clamp(output, -1.15f, 1.15f);
    lastOutput = output;
    audibility += kAudibilityAlpha * (std::abs(output) - audibility);
    return output;
}

float Voice::renderEndFade()
{
    const int length = std::max(2, (int)(0.004f * kEngineSampleRateF));
    const float t = (float)std::max(0, endFadeRemaining - 1) / (float)(length - 1);
    const float output = endFadeOutput * t * t * (3.0f - 2.0f * t);
    if (--endFadeRemaining <= 0) stop();
    else { lastOutput = output; audibility += kAudibilityAlpha * (std::abs(output) - audibility); }
    return output;
}

void Voice::refreshPitch()
{
    if (!active || currentInstrument == nullptr)
        return;
    float baseNote = currentInstrument->baseNote;
    if (currentInstrument->liveBaseNote != nullptr)
        baseNote = currentInstrument->liveBaseNote->load(std::memory_order_relaxed);
    pitchRatio = std::pow(2.0, (currentNote - baseNote) / 12.0) * detuneRatio;
}

void Voice::applyParamsLive(const VoiceParams& params)
{
    const double previousDetune = detuneRatio;
    updateParams(params);
    if (previousDetune > 0.0)
        pitchRatio *= detuneRatio / previousDetune;
}
