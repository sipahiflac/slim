#include "LoopFinder.h"
#include "EngineConstants.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <map>

namespace
{
constexpr double kPi = 3.14159265358979323846;
const int kRateScale = (int)std::lround(kEngineSampleRate / 16000.0);

int samplesFor(double seconds) { return (int)std::lround(seconds * kEngineSampleRate); }

// RMS of a short region, used for envelope/level comparisons.
double regionRms(const std::vector<float>& audio, int from, int to)
{
    from = std::max(0, from);
    to = std::min((int)audio.size(), to);
    if (to <= from)
        return 0.0;
    double energy = 0.0;
    for (int i = from; i < to; ++i)
        energy += (double)audio[(size_t)i] * audio[(size_t)i];
    return std::sqrt(energy / (to - from));
}

// Find where the recording's actual CONTENT ends, ignoring trailing silence
// and anything stranded after it. Samples are often exported into a
// fixed-length region with a quiet ramp and silence after the note; treating
// that as material would put loops into the silence.
int findContentEnd(const std::vector<float>& audio, int attackEnd)
{
    const int count = (int)audio.size();
    const int bucket = std::max(1, samplesFor(0.025));
    const int numBuckets = count / bucket;
    if (numBuckets < 8)
        return count;

    double peak = 1.0e-12;
    std::vector<double> level((size_t)numBuckets);
    for (int i = 0; i < numBuckets; ++i)
    {
        level[(size_t)i] = regionRms(audio, i * bucket, (i + 1) * bucket);
        peak = std::max(peak, level[(size_t)i]);
    }

    // -30 dB relative to the loudest part, held for 150 ms.
    const double floorLevel = peak * 0.0316;
    const int quietRunToEnd = std::max(2, samplesFor(0.15) / bucket);
    int quietRun = 0;
    for (int i = std::max(0, attackEnd / bucket); i < numBuckets; ++i)
    {
        if (level[(size_t)i] < floorLevel)
        {
            if (++quietRun >= quietRunToEnd)
                return std::max(attackEnd + bucket, (i - quietRun + 1) * bucket);
        }
        else quietRun = 0;
    }
    return count;
}

// The stretch where the note is actually held: from the end of the onset to
// the last moment still within 12 dB of the held level (the median level
// after the onset). The natural release and any export fade at the end are
// left out; how much the level moves inside that stretch is judged per
// candidate by the seam score, not by a global "settled" test.
std::pair<int, int> findSustainSpan(const std::vector<float>& audio, int onsetEnd, int contentEnd)
{
    const int bucket = std::max(1, samplesFor(0.025));
    const int first = onsetEnd / bucket, last = contentEnd / bucket;
    if (last - first < 4)
        return { onsetEnd, contentEnd };
    std::vector<double> level;
    for (int i = first; i < last; ++i)
        level.push_back(20.0 * std::log10(regionRms(audio, i * bucket, (i + 1) * bucket) + 1.0e-9));
    auto sorted = level;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const double held = sorted[sorted.size() / 2];
    int end = (int)level.size() - 1;
    while (end > 0 && level[(size_t)end] < held - 12.0) --end;
    return { onsetEnd, std::min(contentEnd, (first + end + 1) * bucket) };
}

void fft(std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * kPi / (double)len;
        const std::complex<float> step((float)std::cos(angle), (float)std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w(1.0f);
            for (size_t k = 0; k < len / 2; ++k, w *= step)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

// Short-time spectra on a fixed hop. Each frame keeps its unit-length
// square-root magnitude spectrum up to 5 kHz -- fine enough (31 Hz bins at
// 16 kHz) that harmonics, vibrato position and breath colour all show up --
// plus its level. Nothing here estimates a pitch.
struct Frames
{
    int first = 0, hop = 64, size = 512, count = 0, bins = 0;
    std::vector<float> spectrum; // count * bins
    std::vector<float> levelDb;  // count

    int centre(int f) const { return first + f * hop + size / 2; }
    int frameAt(int sample) const { return (int)std::lround((double)(sample - first - size / 2) / hop); }

    float similarity(int a, int b) const
    {
        const float* x = &spectrum[(size_t)a * (size_t)bins];
        const float* y = &spectrum[(size_t)b * (size_t)bins];
        float dot = 0.0f;
        for (int k = 0; k < bins; ++k) dot += x[k] * y[k];
        return dot;
    }
};

Frames analyse(const std::vector<float>& audio, int from, int to, int hop)
{
    Frames frames;
    frames.size = 512 * kRateScale;
    frames.hop = hop;
    frames.first = std::max(0, from - frames.size / 2);
    frames.count = std::max(0, (std::min(to, (int)audio.size()) - frames.size - frames.first) / hop + 1);
    frames.bins = std::min(frames.size / 2, (int)std::lround(5000.0 * frames.size / kEngineSampleRate));
    frames.spectrum.assign((size_t)frames.count * (size_t)frames.bins, 0.0f);
    frames.levelDb.assign((size_t)frames.count, -120.0f);

    std::vector<float> window((size_t)frames.size);
    for (int i = 0; i < frames.size; ++i)
        window[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / frames.size));

    std::vector<std::complex<float>> buffer((size_t)frames.size);
    for (int f = 0; f < frames.count; ++f)
    {
        const int start = frames.first + f * hop;
        double energy = 0.0;
        for (int i = 0; i < frames.size; ++i)
        {
            const float x = audio[(size_t)(start + i)];
            energy += (double)x * x;
            buffer[(size_t)i] = x * window[(size_t)i];
        }
        frames.levelDb[(size_t)f] = (float)(10.0 * std::log10(energy / frames.size + 1.0e-12));
        fft(buffer);
        float* out = &frames.spectrum[(size_t)f * (size_t)frames.bins];
        double norm = 0.0;
        for (int k = 0; k < frames.bins; ++k)
        {
            out[k] = std::sqrt(std::abs(buffer[(size_t)k]));
            norm += (double)out[k] * out[k];
        }
        const float scale = norm > 1.0e-20 ? (float)(1.0 / std::sqrt(norm)) : 0.0f;
        for (int k = 0; k < frames.bins; ++k) out[k] *= scale;
    }
    return frames;
}

double median(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2];
}

// Range-maximum over frame indices in O(1) per query.
struct SparseMax
{
    std::vector<std::vector<float>> table;
    explicit SparseMax(const std::vector<float>& values)
    {
        table.push_back(values);
        for (size_t width = 2; width <= values.size(); width *= 2)
        {
            const auto& previous = table.back();
            std::vector<float> next(values.size() - width + 1);
            for (size_t i = 0; i < next.size(); ++i)
                next[i] = std::max(previous[i], previous[i + width / 2]);
            table.push_back(std::move(next));
        }
    }
    float query(int from, int to) const // inclusive
    {
        if (to < from) return 0.0f;
        int level = 0;
        while ((2 << level) <= to - from + 1) ++level;
        return std::max(table[(size_t)level][(size_t)from], table[(size_t)level][(size_t)(to - (1 << level) + 1)]);
    }
};

// Crossfade length for a loop: long enough to melt breath noise and small
// timbre differences, never more than a quarter of the loop.
int crossfadeFor(int loopLength)
{
    return std::clamp(loopLength / 4, samplesFor(0.030), samplesFor(0.150));
}

// Normalised correlation of the two spans playback blends at the seam
// (the crossfade before each point plus a little after), 0..1.
double seamCorrelation(const std::vector<float>& audio, int start, int end, int crossfade)
{
    const int margin = samplesFor(0.004);
    const int from = -crossfade - margin, to = margin;
    if (start + from < 0 || end + to > (int)audio.size())
        return 0.0;
    double dot = 0.0, aa = 0.0, bb = 0.0;
    for (int t = from; t < to; ++t)
    {
        const double x = audio[(size_t)(start + t)], y = audio[(size_t)(end + t)];
        dot += x * y; aa += x * x; bb += y * y;
    }
    if (aa < 1.0e-14 || bb < 1.0e-14)
        return 0.0;
    return std::clamp(dot / std::sqrt(aa * bb), 0.0, 1.0);
}

// Moves the frame-accurate points to the sample offsets where the blended
// spans line up best, so harmonics add instead of cancelling mid-crossfade.
void refineSeam(const std::vector<float>& audio, LoopRegion& loop, int earliestStart, int latestEnd, int hop)
{
    // Wide enough to cover a whole period of a low note (about 10 ms for 100 Hz), so the seam can land on the
    // same phase of the wave instead of the nearest frame boundary.
    const int startRadius = std::max(hop / 2, samplesFor(0.006));
    const int endRadius = std::max(hop + samplesFor(0.003), samplesFor(0.010));
    const int margin = samplesFor(0.004);
    const auto correlation = [&](int a, int b, int stride)
    {
        double dot = 0.0, aa = 0.0, bb = 0.0;
        for (int t = -loop.crossfade - margin; t < margin; t += stride)
        {
            const double x = audio[(size_t)(a + t)], y = audio[(size_t)(b + t)];
            dot += x * y; aa += x * x; bb += y * y;
        }
        return aa < 1.0e-14 || bb < 1.0e-14 ? -2.0 : dot / std::sqrt(aa * bb);
    };
    const auto allowed = [&](int a, int b)
    {
        return a - loop.crossfade - margin >= earliestStart && b + margin <= latestEnd && b - a >= 4 * loop.crossfade;
    };
    // Coarse pass every 4th offset on every 4th sample, then a fine pass around the winner.
    double best = -2.0;
    int bestStart = loop.start, bestEnd = loop.end;
    for (int a = loop.start - startRadius; a <= loop.start + startRadius; a += 4)
        for (int b = loop.end - endRadius; b <= loop.end + endRadius; b += 4)
        {
            if (!allowed(a, b)) continue;
            const double r = correlation(a, b, 4);
            if (r > best) { best = r; bestStart = a; bestEnd = b; }
        }
    if (best > -2.0)
    {
        const int coarseStart = bestStart, coarseEnd = bestEnd;
        best = -2.0;
        for (int a = coarseStart - 4; a <= coarseStart + 4; ++a)
            for (int b = coarseEnd - 4; b <= coarseEnd + 4; ++b)
            {
                if (!allowed(a, b)) continue;
                const double r = correlation(a, b, 1);
                if (r > best) { best = r; bestStart = a; bestEnd = b; }
            }
    }
    loop.start = bestStart;
    loop.end = bestEnd;
    loop.seamCorrelation = (float)std::clamp(best, 0.0, 1.0);
}

// The longest stretch of [from, to) whose level, smoothed over 100 ms so vibrato and tremolo do not count, stays
// inside a 5 dB band. A held note has one; a pluck, a hit or a piano note only decays and has none worth looping.
std::pair<int, int> findSteadyCore(const std::vector<float>& audio, int from, int to)
{
    const int bucket = std::max(1, samplesFor(0.025));
    const int buckets = (to - from) / bucket;
    if (buckets < 4) return { from, from };
    std::vector<double> level((size_t)buckets), smooth((size_t)buckets);
    for (int i = 0; i < buckets; ++i)
        level[(size_t)i] = 20.0 * std::log10(regionRms(audio, from + i * bucket, from + (i + 1) * bucket) + 1.0e-9);
    for (int i = 0; i < buckets; ++i)
    {
        double sum = 0.0; int used = 0;
        for (int k = i - 2; k <= i + 1; ++k)
            if (k >= 0 && k < buckets) { sum += level[(size_t)k]; ++used; }
        smooth[(size_t)i] = sum / std::max(1, used);
    }
    int bestStart = 0, bestLength = 0;
    for (int start = 0; start < buckets; ++start)
    {
        double low = smooth[(size_t)start], high = low;
        int end = start;
        while (end + 1 < buckets)
        {
            const double value = smooth[(size_t)(end + 1)];
            if (std::max(high, value) - std::min(low, value) > 5.0) break;
            low = std::min(low, value); high = std::max(high, value); ++end;
        }
        if (end - start + 1 > bestLength) { bestLength = end - start + 1; bestStart = start; }
    }
    return { from + bestStart * bucket, from + (bestStart + bestLength) * bucket };
}

double levelDb(const std::vector<float>& audio, int from, int to)
{
    return 20.0 * std::log10(regionRms(audio, from, to) + 1.0e-9);
}
}

namespace LoopFinder
{
// Where the onset transient ends: the peak of a sliding-RMS envelope within
// the early part of the recording (a later swell is not an attack).
int findOnsetEnd(const std::vector<float>& audio)
{
    const int count = (int)audio.size();
    if (count == 0) return 0;
    const int envWindow = std::clamp(count / 200, 32 * kRateScale, 256 * kRateScale);
    const int searchEnd = std::min(count, std::max(samplesFor(0.5), count / 4));
    int peakIndex = 0;
    double peakEnergy = -1.0, energy = 0.0;
    for (int i = 0; i < searchEnd; ++i)
    {
        energy += (double)audio[(size_t)i] * audio[(size_t)i];
        if (i >= envWindow)
        {
            const float old = audio[(size_t)(i - envWindow)];
            energy -= (double)old * old;
        }
        if (i >= envWindow - 1 && energy > peakEnergy)
        {
            peakEnergy = energy;
            peakIndex = i - envWindow / 2;
        }
    }
    return std::clamp(peakIndex, 0, count - 1);
}

void fitSeam(const std::vector<float>& audio, LoopRegion& loop)
{
    const int length = loop.end - loop.start;
    loop.crossfade = std::min(crossfadeFor(length), std::max(0, loop.start - samplesFor(0.004)));
    loop.seamCorrelation = (float)seamCorrelation(audio, loop.start, loop.end, loop.crossfade);
}

bool fullLoop(const std::vector<float>& audio, LoopRegion& loop)
{
    const int count = (int)audio.size();
    if (count < samplesFor(0.4)) return false;
    loop = LoopRegion();
    loop.crossfade = std::clamp(count / 8, samplesFor(0.04), samplesFor(0.15));
    loop.start = loop.crossfade;
    loop.end = count;
    loop.full = true;
    loop.confident = false; // offered, never switched on by the analysis (a sound asked for as a loop turns it on)
    loop.seamCorrelation = (float)seamCorrelation(audio, loop.start, loop.end, loop.crossfade);
    return true;
}

// How demanding a pass is. The strict pass offers only loops that cannot be heard. The relaxed pass exists for
// short sounds: loops are off by default and the user turns one on by choice, so a good-enough loop is worth
// having; it still has a floor (nothing under 0.35 s of sound, nothing under 0.15 s of loop).
struct Tuning
{
    double minSound, minRegion, minCrossfade, loopShare, minLoopFloor, minLoopCeil;
    bool requireSteadyCore;
    double maxNear, maxSeamLevel, maxJump, maxRange, maxLevelStep, minTonal, noisyNear, noisyLevel, extraBar;
};
constexpr Tuning kStrict { 1.0, 0.8, 0.030, 0.45, 0.5, 2.0, true, 0.9, 1.5, 9.0, 6.0, 1.0, 0.75, 0.35, 1.0, 0.75 };
constexpr Tuning kRelaxed { 0.35, 0.22, 0.020, 0.5, 0.15, 1.2, false, 1.4, 3.5, 16.0, 14.0, 3.0, 0.55, 0.7, 2.5, 0.55 };
// Last resort for sounds the two passes above reject (the owner found loops almost never turned up): short and
// uneven material still gets a loop to try. Always offered Off.
constexpr Tuning kLoose { 0.25, 0.15, 0.015, 0.5, 0.10, 1.0, false, 2.2, 6.0, 30.0, 24.0, 6.0, 0.35, 1.2, 4.0, 0.35 };

std::vector<LoopRegion> findLoopsWith(const std::vector<float>& audio, int maxLoops, const Tuning& t)
{
    // A loop is only offered when it cannot be heard: a held, steady part of the sound, a long lap, and a seam
    // whose level, spectrum and waveform all match. Anything less returns no loop and the sound plays as a
    // one-shot, which is always better than a loop that clicks, pumps or stutters.
    std::vector<LoopRegion> result;
    const int count = (int)audio.size();
    if (count < samplesFor(t.minSound) || maxLoops <= 0)
        return result;

    // 1. Where a loop may live: the steady part of the held note, after the onset.
    const int onsetEnd = findOnsetEnd(audio);
    const int contentEnd = findContentEnd(audio, onsetEnd);
    const auto sustain = findSustainSpan(audio, onsetEnd, contentEnd);
    const auto core = t.requireSteadyCore ? findSteadyCore(audio, sustain.first, sustain.second) : sustain;
    const int regionStart = std::max(core.first, onsetEnd + samplesFor(0.03));
    const int regionEnd = core.second - samplesFor(0.02);
    if (regionEnd - regionStart < samplesFor(t.minRegion)) // no usable part: a decaying or very short sound
        return result;
    const int minCrossfade = samplesFor(t.minCrossfade);
    // Laps shorter than half a second are heard as a stutter or a tremolo however clean the seam is; with more
    // steady sound available, a lap takes at least 45% of it.
    const int minLoop = std::clamp((int)((regionEnd - regionStart) * t.loopShare), samplesFor(t.minLoopFloor), samplesFor(t.minLoopCeil));
    if (regionEnd - regionStart < minLoop + minCrossfade)
        return result;

    // 2. Spectral frames over the region (plus context on both sides).
    const int span = regionEnd - regionStart;
    const int hop = std::max(64 * kRateScale, (int)std::ceil(span / 1000.0));
    const auto frames = analyse(audio, std::max(0, regionStart - samplesFor(0.25)), regionEnd + samplesFor(0.02), hop);
    if (frames.count < 16)
        return result;

    // Every pair of frames gets compared many times below, so do it once.
    const int n = frames.count;
    std::vector<float> similarity((size_t)n * (size_t)n, 1.0f);
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b)
            similarity[(size_t)a * n + b] = similarity[(size_t)b * n + a] = frames.similarity(a, b);
    const auto distance = [&similarity, n](int a, int b) { return 1.0 - similarity[(size_t)a * n + b]; };

    const int firstStartFrame = std::max(0, frames.frameAt(regionStart + minCrossfade) + 1);
    const int lastEndFrame = std::min(frames.count - 1, frames.frameAt(regionEnd) - 1);
    const int minLoopFrames = std::max(4, minLoop / hop);
    if (lastEndFrame - firstStartFrame < minLoopFrames)
        return result;

    // Reference distances: how different two unrelated moments of this
    // sustain are, and how much it moves from one frame to the next.
    std::vector<double> farPairs, steps;
    for (int f = firstStartFrame; f + 1 <= lastEndFrame; ++f)
    {
        steps.push_back(distance(f, f + 1));
        const int g = firstStartFrame + (int)(((long long)f * 7919) % std::max(1, lastEndFrame - firstStartFrame));
        if (std::abs(g - f) > minLoopFrames / 2) farPairs.push_back(distance(f, g));
    }
    const double pairDistance = std::max(1.0e-4, median(farPairs));
    const double stepDistance = std::max(1.0e-5, median(steps));

    // Spectral jumps and level inside a candidate loop, as range queries.
    std::vector<float> jump((size_t)frames.count, 0.0f);
    for (int f = 1; f < frames.count; ++f)
        jump[(size_t)f] = (float)(distance(f - 1, f) / stepDistance);
    std::vector<float> negLevel((size_t)frames.count);
    for (int f = 0; f < frames.count; ++f) negLevel[(size_t)f] = -frames.levelDb[(size_t)f];
    const SparseMax jumpMax(jump), levelMax(frames.levelDb), levelMin(negLevel);

    // 3. Seam weights: a near context (+-40 ms: timbre, harmonics, level at
    // the seam) and a wide one (+-200 ms: vibrato and breath phase).
    const auto gaussian = [hop](double reach)
    {
        const int k = std::max(1, (int)std::lround(reach * kEngineSampleRate / hop));
        std::vector<double> w((size_t)(2 * k + 1));
        for (int i = -k; i <= k; ++i) w[(size_t)(i + k)] = std::exp(-0.5 * std::pow(2.0 * i / k, 2.0));
        return w;
    };
    const auto nearW = gaussian(0.040), wideW = gaussian(0.200);
    const int nearK = (int)nearW.size() / 2, wideK = (int)wideW.size() / 2;

    struct Candidate { double score, nearDistance, levelDb; int startFrame, endFrame; };
    std::map<std::pair<int, int>, Candidate> buckets;
    const int positionBucket = std::max(1, samplesFor(0.15) / hop);

    for (int i = firstStartFrame; i <= lastEndFrame - minLoopFrames; ++i)
    {
        for (int j = i + minLoopFrames; j <= lastEndFrame; ++j)
        {
            double nearSum = 0.0, nearWeight = 0.0, levelSum = 0.0;
            for (int k = -nearK; k <= nearK; ++k)
            {
                const int a = i + k, b = j + k;
                if (a < 0 || b >= frames.count) continue;
                const double w = nearW[(size_t)(k + nearK)];
                nearSum += w * distance(a, b);
                levelSum += w * std::abs(frames.levelDb[(size_t)a] - frames.levelDb[(size_t)b]);
                nearWeight += w;
            }
            const double nearDistance = nearSum / nearWeight / pairDistance;
            const double seamLevelDb = levelSum / nearWeight;
            // Hard limits first: the two sides of the seam must sound alike and sit at the same level.
            if (nearDistance > t.maxNear || seamLevelDb > t.maxSeamLevel) continue;

            // Inside the loop: a breath burst or glitch repeats on every lap, and a level swing turns the
            // loop into a pulse.
            const double worstJump = jumpMax.query(i + 1, j);
            const double levelRange = levelMax.query(i, j) + levelMin.query(i, j);
            if (worstJump > t.maxJump || levelRange > t.maxRange) continue;

            double wideSum = 0.0, wideWeight = 0.0;
            for (int k = -wideK; k <= wideK; k += 2)
            {
                const int a = i + k, b = j + k;
                if (a < 0 || b >= frames.count) continue;
                const double w = wideW[(size_t)(k + wideK)];
                wideSum += w * distance(a, b);
                wideWeight += w;
            }
            const double wideDistance = wideSum / wideWeight / pairDistance;
            const double seconds = (double)(j - i) * hop / kEngineSampleRate;

            double score = -nearDistance - 0.6 * wideDistance - seamLevelDb / 2.0;
            score -= 0.08 * std::max(0.0, worstJump - 5.0);
            score -= 0.08 * std::max(0.0, levelRange - 3.0);
            // Longer laps repeat less often and sound less mechanical.
            score += 0.4 * std::clamp(std::log2(seconds / 0.8), -2.0, 2.5);

            const std::pair<int, int> key { frames.centre(i) / (positionBucket * hop),
                                            (int)std::floor(std::log(seconds) / std::log(1.25)) };
            auto found = buckets.find(key);
            if (found == buckets.end() || score > found->second.score)
                buckets[key] = { score, nearDistance, seamLevelDb, i, j };
        }
    }
    if (buckets.empty())
        return result;

    std::vector<Candidate> ranked;
    for (const auto& entry : buckets) ranked.push_back(entry.second);
    std::sort(ranked.begin(), ranked.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    // 4. Final checks on the actual samples, best candidate first. Loop 1 is the best seam that passes; Loop 2/3
    // must pass the same checks and be genuinely elsewhere or of a clearly different length.
    const double bestScore = ranked.front().score;
    int examined = 0;
    for (const auto& candidate : ranked)
    {
        if ((int)result.size() >= maxLoops || ++examined > 40) break;
        if (candidate.score < bestScore - 0.8) break;

        LoopRegion loop;
        loop.start = frames.centre(candidate.startFrame);
        loop.end = frames.centre(candidate.endFrame);
        loop.crossfade = std::min(crossfadeFor(loop.end - loop.start), loop.start - onsetEnd - samplesFor(0.004));
        if (loop.crossfade < minCrossfade) continue;
        refineSeam(audio, loop, onsetEnd, contentEnd, hop);
        if (loop.end - loop.start < minLoop) continue;

        // The level the listener hears either side of the seam (the crossfaded spans).
        const double levelStep = std::abs(levelDb(audio, loop.start - loop.crossfade, loop.start)
                                          - levelDb(audio, loop.end - loop.crossfade, loop.end));
        // Tonal material must line up as a waveform; noise-like material (breath, air) cannot, so there the
        // spectrum and level have to match very closely instead.
        const bool tonalSeam = loop.seamCorrelation >= (float)t.minTonal;
        const bool noisySeam = candidate.nearDistance <= t.noisyNear && candidate.levelDb <= t.noisyLevel;
        if (levelStep > t.maxLevelStep || !(tonalSeam || noisySeam)) continue;

        const int length = loop.end - loop.start;
        bool distinct = true;
        for (const auto& existing : result)
        {
            const int existingLength = existing.end - existing.start;
            if (std::abs(loop.start - existing.start) < std::max(samplesFor(0.15), existingLength / 4)
                && std::abs(length - existingLength) < std::max(samplesFor(0.15), existingLength / 5))
                distinct = false;
        }
        if (distinct) result.push_back(loop);
    }
    // The best loop always stays; extra choices must have a seam that is nearly as clean.
    if (result.size() > 1)
    {
        const float bar = std::max((float)t.extraBar, result.front().seamCorrelation - 0.1f);
        result.erase(std::remove_if(result.begin() + 1, result.end(),
                                    [bar](const LoopRegion& loop) { return loop.seamCorrelation < bar; }),
                     result.end());
    }
    return result;
}

std::vector<LoopRegion> findLoops(const std::vector<float>& audio, int maxLoops)
{
    auto loops = findLoopsWith(audio, maxLoops, kStrict);
    if (loops.empty())
    {
        loops = findLoopsWith(audio, maxLoops, kRelaxed);
        for (auto& loop : loops) loop.confident = false;
    }
    if (loops.empty())
    {
        loops = findLoopsWith(audio, maxLoops, kLoose);
        for (auto& loop : loops) loop.confident = false;
    }
    return loops;
}
}
