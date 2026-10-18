#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <vector>

// Keeps a resizable editor in the handset's 320:440 shape and on one of the sizes the page offers (whole device
// pixels per art pixel), so dragging a host window edge never blurs the pixel art. The page reports the sizes
// through setResizeSteps; with none it still holds the aspect ratio.
class HandsetSizeConstrainer : public juce::ComponentBoundsConstrainer
{
public:
    void setSteps(std::vector<double> percents) { steps = std::move(percents); } // message thread

    void checkBounds(juce::Rectangle<int>& bounds, const juce::Rectangle<int>& old, const juce::Rectangle<int>&,
                     bool stretchingTop, bool stretchingLeft, bool, bool) override
    {
        double percent = std::clamp(std::max(bounds.getWidth() / 320.0, bounds.getHeight() / 440.0) * 100.0, 40.0, 400.0);
        if (!steps.empty())
            percent = *std::min_element(steps.begin(), steps.end(),
                                        [percent](double a, double b) { return std::abs(a - percent) < std::abs(b - percent); });
        const int width = juce::roundToInt(320.0 * percent / 100.0), height = juce::roundToInt(440.0 * percent / 100.0);
        if (stretchingLeft) bounds.setX(old.getRight() - width);
        if (stretchingTop) bounds.setY(old.getBottom() - height);
        bounds.setSize(width, height);
    }

private:
    std::vector<double> steps;
};
