#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

// Runs body(i) for every i in [0, count) across the machine's cores and
// returns once all of them have finished. Iterations must be independent;
// callers write each result into its own pre-sized slot so order is kept.
template <typename Body>
void parallelFor(int count, Body&& body)
{
    const int workers = std::min(count, (int)std::max(1u, std::thread::hardware_concurrency()));
    std::atomic<int> next { 0 };
    const auto drain = [&]
    {
        for (int index = next++; index < count; index = next++)
            body(index);
    };

    std::vector<std::thread> helpers;
    for (int worker = 1; worker < workers; ++worker)
        helpers.emplace_back(drain);
    drain();
    for (auto& helper : helpers)
        helper.join();
}
