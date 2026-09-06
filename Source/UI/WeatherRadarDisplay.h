#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class StableAliveAudioProcessor;

class WeatherRadarDisplay : public juce::Component, public juce::Timer
{
public:
    WeatherRadarDisplay(juce::AudioProcessorValueTreeState& apvtsIn, StableAliveAudioProcessor& processorIn, std::function<void()> onDoubleClick);
    ~WeatherRadarDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void timerCallback() override;

    void regenerateWeather(juce::uint32 seed, bool isPresetChange = false);
    void setSeedCode(const juce::String& code);
    void triggerBootSequence();
    void setNoteReactionSource(std::atomic<float>* pulseSource) { noteReactionPulsePtr = pulseSource; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    StableAliveAudioProcessor& processor;
    std::function<void()> onDoubleClick;
    juce::Image weatherImage;
    float sweepAngle = 0.0f;
    float lastSweepAngle = 0.0f;
    float sweepTime = 0.0f;
    float planeProgress = 0.0f;
    float scrollAccumulator = 0.0f;
    std::atomic<float>* noteReactionPulsePtr = nullptr;
    float noteReactiveJitter = 0.0f;
    float noteIntensityOffset = 0.0f;
    float lastCutoffVal = -1.0f;
    juce::uint32 lastCutoffRebuildMs = 0;
    juce::uint32 seedHash = 0;
    juce::String seedCode = "RPL-0000-A";
    bool isClearSkies = false;
    float bootTimeSeconds = 0.0f;
    bool hasRandomized = false;
    float randomizeBlinkTime = 0.0f;
    bool isPresetRecallBlink = false;

    // Value noise grid for procedural weather
    static constexpr int gridSize = 24;
    float noiseGrid[gridSize + 1][gridSize + 1] = {};

    // Waypoint data generated from seed
    struct Waypoint { float x, y; juce::String name; };
    std::vector<Waypoint> waypoints;

    // HUD values derived from seed
    int heading = 113;
    int gainValue = 27;
    float tiltValue = 0.5f;

    float interpolatedNoise(float x, float y) const;
    float sampleNoise(float x, float y) const;
    void rebuildWeatherImage();
    void updateSweptWeather();
    void generateWaypoints(juce::uint32 seed);
    juce::Colour getWeatherColour(float intensity) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WeatherRadarDisplay)
};
