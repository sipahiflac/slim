#pragma once

#include <vector>

// A sustain loop inside a sample buffer (engine-rate sample indices).
// Playback crossfades the `crossfade` samples leading up to `end` with the
// same span leading up to `start`, so a loop lap is exactly end - start long.
struct LoopRegion
{
    int start = 0;
    int end = 0;
    int crossfade = 0;            // 0 = let playback pick a short default
    float seamCorrelation = 1.0f; // similarity of the two crossfaded spans, 0..1
};

namespace LoopFinder
{
    // Where the onset transient has passed and the held sound begins.
    int findOnsetEnd(const std::vector<float>& audio);

    // Up to maxLoops distinct, credible sustain loops, best first. Seams are
    // judged on the spectrum (timbre, harmonics, vibrato phase, breath) and
    // level around both ends; no pitch is estimated anywhere.
    std::vector<LoopRegion> findLoops(const std::vector<float>& audio, int maxLoops = 3);

    // Chooses the crossfade and measures the seam of a loop whose points are
    // fixed already (e.g. markers stored in the WAV file).
    void fitSeam(const std::vector<float>& audio, LoopRegion& loop);
}
