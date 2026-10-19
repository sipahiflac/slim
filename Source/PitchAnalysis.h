#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include <utility>

namespace SamplePitch
{
struct Result { bool pitched = false; int note = 60; float cents = 0, confidence = 0; };
struct Frame { double midi = 0, confidence = 0; };

// YIN difference/CMND, first threshold minimum and parabolic interpolation.
// Analysis only: never called from the realtime render callback.
inline Frame frame(const std::vector<float>& audio, int start, double rate, int window = 2048, double threshold = .15)
{
    const int low = std::max(2, (int)(rate / 3200.0)), high = (int)std::ceil(rate / 25.0);
    if (start < 0 || start + window + high >= (int)audio.size()) return {};
    std::vector<double> cmnd((size_t)high + 1, 1);
    std::vector<double> differences((size_t)high + 1, 0);
    double sum = 0;
    for (int lag = 1; lag <= high; ++lag) {
        double difference = 0;
        for (int i = 0; i < window; ++i) {
            const double d = audio[(size_t)(start + i)] - audio[(size_t)(start + i + lag)];
            difference += d * d;
        }
        sum += difference;
        differences[(size_t)lag] = difference;
        cmnd[(size_t)lag] = sum > 1e-15 ? difference * lag / sum : 1;
    }
    int lag = 0;
    for (int t = low; t < high; ++t) if (cmnd[(size_t)t] < threshold) {
        lag = t;
        while (lag + 1 < high && cmnd[(size_t)(lag + 1)] < cmnd[(size_t)lag]) ++lag;
        break;
    }
    // No periodic minimum: don't force drums/noise into a tuned keyboard source.
    if (lag <= low || lag >= high) return {};
    const double a = differences[(size_t)(lag - 1)], b = differences[(size_t)lag], c = differences[(size_t)(lag + 1)];
    const double curvature = a - 2*b + c;
    const double correction = std::abs(curvature) > 1e-15 ? std::clamp(.5*(a-c)/curvature, -.5, .5) : 0;
    // At high pitches an integer-lag parabola can miss several cents. Refine
    // the same minimum with a fractional, windowed-sinc delay. This is analysis
    // only and does not resample or otherwise change the stored recording.
    const auto fractionalDifference = [&](double delay) {
        const int whole = (int)std::floor(delay);
        const double fraction = delay-whole;
        double weights[16], total = 0;
        constexpr double pi = 3.14159265358979323846;
        for (int k=0;k<16;++k) {
            const double x = k-7-fraction;
            weights[k] = (std::abs(x)<1e-10 ? 1 : std::sin(pi*x)/(pi*x)) * (.5+.5*std::cos(pi*x/8));
            total += weights[k];
        }
        double error = 0;
        for (int i=8;i<window-8;++i) {
            double shifted = 0;
            for (int k=0;k<16;++k) shifted += weights[k]*audio[(size_t)(start+i+whole+k-7)];
            const double difference = audio[(size_t)(start+i)]-shifted/total;
            error += difference*difference;
        }
        return error;
    };
    double left=lag+correction-.6, right=lag+correction+.6;
    constexpr double golden=.6180339887498949;
    double x1=right-golden*(right-left), x2=left+golden*(right-left);
    double f1=fractionalDifference(x1), f2=fractionalDifference(x2);
    for(int i=0;i<20;++i) {
        if(f1<f2){right=x2;x2=x1;f2=f1;x1=right-golden*(right-left);f1=fractionalDifference(x1);}
        else{left=x1;x1=x2;f1=f2;x2=left+golden*(right-left);f2=fractionalDifference(x2);}
    }
    const double frequency = rate / (.5*(left+right));
    return {69 + 12*std::log2(frequency/440.0), std::clamp(1-cmnd[(size_t)lag], 0.0, 1.0)};
}

// The analysis behind the root screen's "Auto", run on what the user kept after trimming. The automatic pass
// at generation time is strict on purpose (drums and noise must not get a root note); here the user asked for a
// pitch, so it listens to the whole selection, accepts short selections, and takes a weaker but agreeing majority.
inline Result analyseSelection(const std::vector<float>& audio, double rate = 16000)
{
    Result result;
    const int size = (int)audio.size(), high = (int)std::ceil(rate/25.0);
    const int window = std::min(2048, size - high - 2);
    if (window < 512) return result; // not even a few periods of a low note fit
    const int last = size - window - high - 1;
    if (last < 0) return result;
    const int first = std::min(last, (int)(.02*rate));
    const int step = std::max(1, (int)(.02*rate));
    const int count = std::min(24, 1 + (last-first)/step);
    std::vector<int> positions;
    std::vector<double> energies;
    double peakEnergy = 0;
    for (int i=0;i<count;++i) {
        const int start = first + (last-first)*i/std::max(1,count-1);
        double energy = 0;
        for (int j=0;j<window;++j) energy += (double)audio[(size_t)(start+j)]*audio[(size_t)(start+j)];
        energy /= window;
        positions.push_back(start); energies.push_back(energy); peakEnergy=std::max(peakEnergy,energy);
    }
    std::vector<Frame> voiced;
    int eligible = 0;
    for (int i=0;i<count;++i) {
        if (energies[(size_t)i] < std::max(1e-9, peakEnergy*.003)) continue;
        ++eligible;
        const auto pitch = frame(audio, positions[(size_t)i], rate, window, .35);
        if (pitch.confidence >= .55) voiced.push_back(pitch);
    }
    if (voiced.empty()) return result;
    std::vector<Frame> best;
    for (const auto& centre : voiced) {
        std::vector<Frame> group;
        for (const auto& value : voiced) if (std::abs(value.midi-centre.midi) <= .6) group.push_back(value);
        if (group.size() > best.size()) best = std::move(group);
    }
    std::sort(best.begin(), best.end(), [](auto a, auto b){return a.midi<b.midi;});
    const double midi = best[best.size()/2].midi;
    std::vector<double> deviations;
    double periodicity = 0;
    for (auto value : best) { deviations.push_back(std::abs(value.midi-midi)); periodicity+=value.confidence; }
    std::sort(deviations.begin(), deviations.end());
    const double stability = std::exp(-deviations[deviations.size()/2]/.75);
    result.confidence = (float)(periodicity/best.size()*stability*best.size()/std::max(1,eligible));
    result.pitched = (int)best.size() >= std::min(2, count) && result.confidence >= .3f;
    if (result.pitched) {
        result.note = std::clamp((int)std::lround(midi), 0, 127);
        result.cents = (float)((midi-result.note)*100);
    }
    return result;
}

inline Result analyse(const std::vector<float>& audio, double rate = 16000)
{
    Result result;
    constexpr int window = 2048;
    const int last = (int)audio.size() - window - (int)std::ceil(rate/25.0) - 1;
    if (last < 0) return result;
    // Omit the attack and natural release. More frames than the old five-point
    // scan; relative energy gating excludes quiet tails from the confidence vote.
    const int first = std::min(last, (int)(.06*rate));
    const int end = std::max(first, std::min(last, (int)(audio.size()*.72)));
    const int count = std::min(18, 1 + (end-first)/std::max(1,(int)(.025*rate)));
    std::vector<int> positions;
    std::vector<double> energies;
    double peakEnergy = 0;
    for (int i=0;i<count;++i) {
        const int start = first + (end-first)*i/std::max(1,count-1);
        double energy = 0;
        for (int j=0;j<window;++j) energy += (double)audio[(size_t)(start+j)]*audio[(size_t)(start+j)];
        energy /= window;
        positions.push_back(start); energies.push_back(energy); peakEnergy=std::max(peakEnergy,energy);
    }
    std::vector<Frame> voiced;
    int eligible = 0;
    for (int i=0;i<count;++i) {
        if (energies[(size_t)i] < std::max(1e-8, peakEnergy*.01)) continue;
        ++eligible;
        auto pitch = frame(audio, positions[(size_t)i], rate);
        if (pitch.confidence >= .85) voiced.push_back(pitch);
    }
    if (voiced.size() < 3) return result;
    // Find the strongest agreeing group instead of averaging octave mistakes
    // into a nonexistent pitch. Agreement is required across the analysed sound.
    std::vector<Frame> best;
    for (const auto& centre : voiced) {
        std::vector<Frame> group;
        for (const auto& value : voiced) if (std::abs(value.midi-centre.midi) <= .45) group.push_back(value);
        if (group.size() > best.size()) best = std::move(group);
    }
    std::sort(best.begin(), best.end(), [](auto a, auto b){return a.midi<b.midi;});
    const double midi = best[best.size()/2].midi;
    std::vector<double> deviations;
    double periodicity = 0;
    for (auto value : best) { deviations.push_back(std::abs(value.midi-midi)); periodicity+=value.confidence; }
    std::sort(deviations.begin(), deviations.end());
    const double stability = std::exp(-deviations[deviations.size()/2]/.75);
    result.confidence = (float)(periodicity/best.size()*stability*best.size()/std::max(1,eligible));
    result.pitched = best.size() >= 3 && result.confidence >= .72f;
    if (result.pitched) {
        result.note = std::clamp((int)std::lround(midi), 0, 127);
        result.cents = (float)((midi-result.note)*100);
    }
    return result;
}
}
