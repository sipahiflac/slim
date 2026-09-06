#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class PresetManager
{
public:
    PresetManager(juce::AudioProcessorValueTreeState& apvtsRef);
    
    juce::File getPresetsFolder() const;

    /** Writes any factory preset that is not already on disk. Called once from
        the constructor; never overwrites a file the user has. */
    void installFactoryPresets() const;
    juce::StringArray getPresetList() const;
    
    void savePreset(const juce::String& name);
    void loadPreset(const juce::String& name);
    void deletePreset(const juce::String& name);
    
    juce::String getCurrentPresetName() const { return currentPresetName; }
    void setCurrentPresetName(const juce::String& name) { currentPresetName = name; }
    
private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentPresetName { "INIT" };
};

// Minimal arrow button drawn as a filled triangle
class ArrowButton : public juce::Component
{
public:
    enum Direction { Left, Right };
    ArrowButton(Direction d) : dir(d) {}
    
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(5.0f, 4.0f);
        juce::Path p;
        if (dir == Left)
        {
            p.addTriangle(b.getRight(), b.getY(), b.getRight(), b.getBottom(), b.getX(), b.getCentreY());
        }
        else
        {
            p.addTriangle(b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
        }
        
        bool hover = isMouseOver();
        g.setColour(hover ? juce::Colour(0xFFD9D9D9) : juce::Colour(0xFF8FA0B5));
        g.fillPath(p);
    }
    
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    
    std::function<void()> onClick;
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    
private:
    Direction dir;
};

// A minimal cockpit-style preset browser panel
class PresetPanel : public juce::Component
{
public:
    PresetPanel(PresetManager& pm);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    
    void refreshPresetName();
    
    std::function<void()> onPresetLoaded;
    
private:
    PresetManager& presetManager;
    
    ArrowButton prevBtn { ArrowButton::Left };
    ArrowButton nextBtn { ArrowButton::Right };
    juce::Label presetNameLabel;
    
    juce::StringArray presetList;
    int currentIndex = -1;
    
    void loadPresetByIndex(int index);
    void showSaveDialog();
    void showPresetPopup();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetPanel)
};
