#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class ScanFilterSection;
class AmpEnvelopeSection;
class MotionSection;
class SpaceFxSection;
class FilterEnvelopeSection;
class PlaceholderSection;
class LfoSection;
class StableAliveAudioProcessor;

class CockpitSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (getProperties().getWithDefault("isMappingFocus", false))
        {
            mapDragStartY = e.position.y;
            mapStartAmt = (float) getProperties().getWithDefault("lfoAmount", 0.0);
            if (onMappingDragStart) onMappingDragStart();
            return;
        }
        juce::Slider::mouseDown(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (getProperties().getWithDefault("isMappingFocus", false))
        {
            float delta = (mapDragStartY - e.position.y) / 120.0f;
            float newAmt = juce::jlimit(0.0f, 1.0f, mapStartAmt + delta);
            if (onMappingDrag) onMappingDrag(newAmt);
            return;
        }
        juce::Slider::mouseDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (getProperties().getWithDefault("isMappingFocus", false))
        {
            if (onMappingDragEnd) onMappingDragEnd();
            return;
        }
        juce::Slider::mouseUp(e);
    }

    std::function<void()> onMappingDragStart;
    std::function<void(float)> onMappingDrag;
    std::function<void()> onMappingDragEnd;

private:
    float mapDragStartY = 0.0f;
    float mapStartAmt = 0.0f;
};

class CutoffSlider : public CockpitSlider
{
public:
    CutoffSlider()
    {
        getProperties().set("pressScale", 0.0f);
    }
    
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (onDoubleClickAction)
            onDoubleClickAction();
        
        getProperties().set("pressScale", 1.0f);
        repaint();
    }
    
    std::function<void()> onDoubleClickAction;
};

class UtilityPanel : public juce::Component,
                     private juce::Timer
{
public:
    UtilityPanel(juce::AudioProcessorValueTreeState& apvts, StableAliveAudioProcessor& processor);
    ~UtilityPanel() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void triggerRandomizationAnimation();

    float getDrawerProgress() const { return drawerProgress; }
    bool isDrawerOpen() const { return targetDrawerProgress > 0.5f; }
    void setDrawerOpen(bool open);
    void toggleDrawer();

    void startMappingMode();
    void stopMappingMode();
    void toggleMappingMode();
    bool getIsMappingMode() const { return isMappingMode; }

private:
    void timerCallback() override;

    float drawerProgress = 0.0f;
    float targetDrawerProgress = 0.0f;
    void updateDrawerLayout();
    float legatoTransition = 0.0f;
    float envLoopTransition = 0.0f;
    int lastOscMode = -1;
    bool wasAnySliderActive = false;
    int lastFilterType = -1;
    int filterTypeChangeTimer = 0;
    bool isMappingMode = false;
    float visualLfoVal = 0.0f;
    float lastRepaintedLfoVal = -999.0f;

    juce::AudioProcessorValueTreeState& apvts;
    StableAliveAudioProcessor& processor;

    // Sliders
    CockpitSlider attackSlider;
    CockpitSlider decaySlider;
    CockpitSlider sustainSlider;
    CockpitSlider releaseSlider;
    CockpitSlider resonanceSlider;
    CutoffSlider cutoffSlider;
    CockpitSlider decimatorSlider;
    CockpitSlider reverbSizeSlider;
    CockpitSlider reverbMixSlider;
    CockpitSlider detuneSlider;
    juce::Slider envLoopMsSlider;
    juce::Slider envLoopBeatsSlider;
    CockpitSlider glideSlider;
    CockpitSlider chorusSlider;
    CockpitSlider delaySlider;
    CockpitSlider panSlider;

    // Page 2 Sliders (Filter Envelope - 4 Knobs + LFO Rate)
    CockpitSlider filterAttackSlider;
    CockpitSlider filterDecaySlider;
    CockpitSlider filterSustainSlider;
    CockpitSlider filterEnvAmountSlider;
    juce::Slider lfoRateSlider;
    juce::Slider lfoBeatsSlider;

    juce::TextButton legatoButton{"legato"};
    juce::TextButton envLoopButton{"env loop"};

    // Attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> atkAttach;
    std::unique_ptr<SliderAttachment> decAttach;
    std::unique_ptr<SliderAttachment> susAttach;
    std::unique_ptr<SliderAttachment> relAttach;
    std::unique_ptr<SliderAttachment> rsnAttach;
    std::unique_ptr<SliderAttachment> cutAttach;
    std::unique_ptr<SliderAttachment> dcmAttach;
    std::unique_ptr<SliderAttachment> rszAttach;
    std::unique_ptr<SliderAttachment> rmxAttach;
    std::unique_ptr<SliderAttachment> detuneAttach;
    std::unique_ptr<SliderAttachment> glideAttach;
    std::unique_ptr<SliderAttachment> chorusAttach;
    std::unique_ptr<SliderAttachment> delayMixAttach;
    std::unique_ptr<SliderAttachment> panMixAttach;
    std::unique_ptr<SliderAttachment> envLoopMsAttach;
    std::unique_ptr<SliderAttachment> envLoopBeatsAttach;
    std::unique_ptr<ButtonAttachment> legatoAttach;
    std::unique_ptr<ButtonAttachment> envlAttach;

    // Page 2 Attachments
    std::unique_ptr<SliderAttachment> fAtkAttach;
    std::unique_ptr<SliderAttachment> fDecAttach;
    std::unique_ptr<SliderAttachment> fSusAttach;
    std::unique_ptr<SliderAttachment> fAmtAttach;
    std::unique_ptr<SliderAttachment> lfoRateAttach;
    std::unique_ptr<SliderAttachment> lfoBeatsAttach;

    // Base Sections
    std::unique_ptr<ScanFilterSection> scanFilterSection;
    std::unique_ptr<AmpEnvelopeSection> ampEnvelopeSection;
    std::unique_ptr<MotionSection> motionSection;
    std::unique_ptr<SpaceFxSection> spaceFxSection;

    // Slide-out Drawer Sections (Overlays Amp Env and Space FX)
    std::unique_ptr<LfoSection> page2TopLeftSection;
    std::unique_ptr<FilterEnvelopeSection> filterEnvelopeSection;

    // Drawer Controls & Dimmer Overlay
    class LeftDimmerOverlay;
    class DrawerNotchButton;
    std::unique_ptr<LeftDimmerOverlay> leftDimmerOverlay;
    std::unique_ptr<DrawerNotchButton> notchButton;

    void setupSlider(juce::Slider& slider, const juce::String& name);
    juce::String getValueText(juce::Slider& slider) const;
    void drawActiveValue(juce::Graphics& g, juce::Slider& slider) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UtilityPanel)
};
