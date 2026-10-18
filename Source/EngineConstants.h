#pragma once

// Single source of truth for the synth engine's internal sample rate.
// Kept at 16 kHz: that rate's tone character was confirmed good. The real
// problem was never resolution -- it was the loop system (see
// LoopFinder.cpp and the crossfade in Voice.cpp). Don't change this without confirming
// with the user first; it has been bounced back and forth once already.
constexpr double kEngineSampleRate = 16000.0;
constexpr float kEngineSampleRateF = 16000.0f;
