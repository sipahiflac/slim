#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/WeatherRadarDisplay.h"
#include "UI/UtilityPanel.h"
#include "UI/PresetManager.h"
#include "UI/Fonts.h"
#include "UI/LicenseManager.h"

class TooltipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        // Dark slate background matching the cockpit
        g.fillAll(juce::Colour(0xFF0E1116));
        
        // Fine amber border
        g.setColour(juce::Colour(0xFFFF9100));
        g.drawRect(0, 0, width, height, 1);
        
        juce::StringArray lines;
        lines.addLines(text);
        
        float y = 6.0f;
        for (auto& line : lines)
        {
            if (line.startsWith("WX"))
            {
                g.setFont(WXFonts::get(13.0f, juce::Font::bold));
                g.setColour(juce::Colour(0xFFFF9100)); // WX logo in amber!
            }
            else if (line.contains("v1.1") || line.contains("build"))
            {
                g.setFont(WXFonts::get(11.0f, juce::Font::plain));
                g.setColour(juce::Colour(0xFF8FA0B5)); // Cool slate gray for details
            }
            else
            {
                g.setFont(WXFonts::get(10.0f, juce::Font::plain));
                g.setColour(juce::Colour(0xFF5C6A7B)); // Dimmer text for copyright
            }
            
            g.drawText(line, 0, (int)y, width, 16, juce::Justification::centred);
            y += 16.0f;
        }
    }
    
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> mousePos, juce::Rectangle<int> keepWithin) override
    {
        juce::StringArray lines;
        lines.addLines(text);
        int maxW = 120;
        for (auto& line : lines)
            maxW = juce::jmax(maxW, (int) WXFonts::get(12.0f, juce::Font::bold).getStringWidth(line) + 24);
        
        int w = maxW;
        int h = lines.size() * 16 + 12;
        
        // Position below mouse to avoid clipping top window boundary
        int x = mousePos.x + 12;
        int y = mousePos.y + 16;
        
        // Clip within window boundaries
        if (x + w > keepWithin.getRight()) x = keepWithin.getRight() - w - 4;
        if (y + h > keepWithin.getBottom()) y = keepWithin.getBottom() - h - 4;
        if (x < keepWithin.getX()) x = keepWithin.getX() + 4;
        if (y < keepWithin.getY()) y = keepWithin.getY() + 4;
        
        return { x, y, w, h };
    }
};

class StableAliveAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                         public juce::KeyListener
{
public:
    StableAliveAudioProcessorEditor (StableAliveAudioProcessor&);
    ~StableAliveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;

private:
    StableAliveAudioProcessor& audioProcessor;

    // Declared before every child component, so it is destroyed after them:
    // keeps the embedded Poppins typefaces and the shared grain tile alive for
    // as long as an editor exists. Without a holder, each WXFonts::get() call
    // would build and immediately tear down the typefaces.
    WXFonts::KeepAlive fontResources;
    WXGrain::KeepAlive grainResources;

    std::unique_ptr<WeatherRadarDisplay> radarDisplay;
    std::unique_ptr<UtilityPanel> utilityPanel;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    juce::Slider masterGainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterGainAttachment;

    class WXLogoComponent : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        WXLogoComponent()
        {
            setTooltip("WX Synthesizer v1.1\n(Click to change Window Scale)");
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void updateTooltip(bool trailerMode)
        {
            juce::String nfrTag = LicenseManager::isNfrLicense() ? "\nNFR LICENSE - NOT FOR RESALE" : "";
            if (trailerMode)
                setTooltip("WX Synthesizer v1.1" + nfrTag + "\n(C) 2026 scrap brain\ntrailer mode\n(Click to change Window Scale)");
            else
                setTooltip("WX Synthesizer v1.1" + nfrTag + "\n(C) 2026 scrap brain\n(Click to change Window Scale)");
        }

        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds();
            g.setFont(WXFonts::get(28.0f, juce::Font::bold));
            g.setColour(juce::Colours::white);
            g.drawText("WX", bounds, juce::Justification::centredLeft);
        }

        std::function<void(double)> onScaleChange;
        double currentScale = 1.0;

        void mouseDown(const juce::MouseEvent&) override
        {
            juce::PopupMenu m;
            m.addItem(1, "75%",  true, std::abs(currentScale - 0.75) < 0.01);
            m.addItem(2, "85%",  true, std::abs(currentScale - 0.85) < 0.01);
            m.addItem(3, "90%",  true, std::abs(currentScale - 0.90) < 0.01);
            m.addItem(4, "100% (Default)", true, std::abs(currentScale - 1.0) < 0.01);
            m.addItem(5, "110%", true, std::abs(currentScale - 1.10) < 0.01);
            m.addItem(6, "125%", true, std::abs(currentScale - 1.25) < 0.01);
            m.addItem(7, "150%", true, std::abs(currentScale - 1.50) < 0.01);
            
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int result)
            {
                double newScale = currentScale;
                if (result == 1) newScale = 0.75;
                else if (result == 2) newScale = 0.85;
                else if (result == 3) newScale = 0.90;
                else if (result == 4) newScale = 1.0;
                else if (result == 5) newScale = 1.10;
                else if (result == 6) newScale = 1.25;
                else if (result == 7) newScale = 1.50;

                if (newScale != currentScale && onScaleChange)
                {
                    currentScale = newScale;
                    onScaleChange(newScale);
                }
            });
        }
    };
    WXLogoComponent logoComponent;

    PresetManager& presetManager;
    PresetPanel presetPanel;
    TooltipLookAndFeel tooltipLaf;

    // Trailer Mode variables
    bool isTrailerMode = false;
    int trailerPresetIndex = 0;

    // QWERTY-to-MIDI mapping (Ableton style)
    struct KeyNote { int keyCode; int midiNote; bool isDown = false; int activeMidiNote = -1; };
    std::vector<KeyNote> keyMap;
    void initKeyMap();
    int octaveOffset = 0;

    // DRM License Screen
    std::unique_ptr<LicenseScreen> licenseScreen;
    void showMainUI();
    void showLicenseScreen();

    // Undo history for random patches (Ctrl + Z)
    std::vector<juce::ValueTree> patchHistory;
    void undoLastRandomPatch();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StableAliveAudioProcessorEditor)
};
