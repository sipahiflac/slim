#include "UtilityPanel.h"
#include "Fonts.h"
#include "../PluginProcessor.h"

namespace
{
constexpr auto panelBg   = 0xFF0E1116;
constexpr auto frameBg   = 0xFF1B2028;
constexpr auto sectionBg = 0xFF0D0F13;
constexpr auto textColor = 0xFFB8C8DBu;
constexpr auto valueBg   = 0xDD12151B;
constexpr auto valueText = 0xFF00E5CC;

static juce::Font getCockpitFont(float size, int style = juce::Font::plain)
{
    return WXFonts::get(size, style);
}

static void drawGrainTexture(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    WXGrain::draw(g, bounds, 0.04f);
}

static void drawSectionTitle(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    g.setFont(getCockpitFont(15.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xFFEDE8DF)); // Crisp bone white for title text
    g.drawText(title, bounds, juce::Justification::centred); // Center in the header bounds
}

static void drawSectionFrame(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    auto area = bounds.toFloat();

    // Clean dark background for the panel body
    g.setColour(juce::Colour(panelBg));
    g.fillRect(area.toNearestInt());

    // Fill the header background with cool dark slate
    auto headerRect = area.withHeight(30.0f);
    g.setColour(juce::Colour(0xFF1B2028));
    g.fillRect(headerRect.toNearestInt());

    // Header divider line (at y=30)
    g.setColour(juce::Colour(0xFF252D3A));
    g.drawHorizontalLine(30, area.getX(), area.getRight());

    // Bevel highlights
    g.setColour(juce::Colour(0x1BFFFFFF)); // Thin top highlight
    g.drawHorizontalLine((int)area.getY(), area.getX(), area.getRight());
    g.setColour(juce::Colour(0x3F000000)); // Thin bottom shadow
    g.drawHorizontalLine((int)area.getBottom() - 1, area.getX(), area.getRight());

    drawSectionTitle(g, bounds.withHeight(30), title);
}

class MatchKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override
    {
        auto area = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(3.0f);
        
        // Push-down compression effect for double-clicks
        float scaleMultiplier = 1.0f;
        if (slider.getProperties().contains("pressScale"))
        {
            float pressScale = slider.getProperties()["pressScale"];
            scaleMultiplier = 1.0f - 0.14f * pressScale; // Shrink up to 14% at maximum press
        }
        
        if (scaleMultiplier != 1.0f)
        {
            area = area.withSize(area.getWidth() * scaleMultiplier, area.getHeight() * scaleMultiplier)
                       .withCentre(area.getCentre());
        }

        const auto centre = area.getCentre();
        const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;

        float visualPos = sliderPosProportional;
        if (slider.getProperties().contains("visualVal"))
        {
            double visualVal = slider.getProperties()["visualVal"];
            auto normRange = slider.getNormalisableRange();
            visualPos = (float) normRange.convertTo0to1(visualVal);
        }
        const float angle = rotaryStartAngle + visualPos * (rotaryEndAngle - rotaryStartAngle);

        // 1. Draw flat dark inner face
        g.setColour(juce::Colour(0xFF252525));
        g.fillEllipse(area.reduced(1.0f));

        const float rimRadius = radius - 1.0f;
        float lfoAmt = (float) slider.getProperties().getWithDefault("lfoAmount", 0.0);
        bool hasMod = (lfoAmt > 0.001f);

        float a1 = 0.0f, a2 = 0.0f;
        if (hasMod)
        {
            float maxAngleSpan = (rotaryEndAngle - rotaryStartAngle) * 0.45f;
            float modSpan = lfoAmt * maxAngleSpan;
            a1 = juce::jlimit(rotaryStartAngle, rotaryEndAngle, angle - modSpan * 0.5f);
            a2 = juce::jlimit(rotaryStartAngle, rotaryEndAngle, angle + modSpan * 0.5f);
        }

        // 2. Draw outer white/gray border ring - HIDING IT where the green LFO arc is (with safety padding margin)!
        if (!hasMod || std::abs(a2 - a1) <= 0.015f)
        {
            // Full 360 degree white rim
            juce::Path whiteRim;
            whiteRim.addCentredArc(centre.x, centre.y, rimRadius, rimRadius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
            g.setColour(juce::Colour(0xFFD1D1D1));
            g.strokePath(whiteRim, juce::PathStrokeType(1.4f));
        }
        else
        {
            // Cutout white rim around the LFO arc with extra padding margin (pay bırakma)
            const float pad = 0.08f; // ~4.5 degrees padding on each side
            float whiteStart = a2 + pad;
            float whiteEnd = a1 - pad;
            if (whiteEnd < whiteStart)
                whiteEnd += juce::MathConstants<float>::twoPi;

            juce::Path whiteRim;
            whiteRim.addCentredArc(centre.x, centre.y, rimRadius, rimRadius, 0.0f, whiteStart, whiteEnd, true);
            g.setColour(juce::Colour(0xFFD1D1D1));
            g.strokePath(whiteRim, juce::PathStrokeType(1.4f));

            // 3. Draw Radar Green Arc sitting prominently on the outer perimeter (sharp flat ends, NO GLOW)
            const float greenRadius = rimRadius + 0.5f;
            juce::Path greenArc;
            greenArc.addCentredArc(centre.x, centre.y, greenRadius, greenRadius, 0.0f, a1, a2, true);
            g.setColour(juce::Colour(0xFF00E600));
            g.strokePath(greenArc, juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

            // 3b. Draw traveling Dark Radar Green tone segment according to real-time LFO (wide, prominent band)
            float curLfo = (float) slider.getProperties().getWithDefault("lfoCurrentVal", 0.0);
            float normLfo = (curLfo + 1.0f) * 0.5f; // [0.0, 1.0]
            float currentPosAngle = a1 + normLfo * (a2 - a1);
            float pipSpan = std::max(0.28f, (a2 - a1) * 0.52f); // Wide, bold band (not a point/dot)
            float p1 = std::max(a1, currentPosAngle - pipSpan * 0.5f);
            float p2 = std::min(a2, currentPosAngle + pipSpan * 0.5f);
            if (p2 > p1)
            {
                juce::Path darkSegment;
                darkSegment.addCentredArc(centre.x, centre.y, greenRadius, greenRadius, 0.0f, p1, p2, true);
                // A muted sage green rather than the old saturated forest green:
                // it should read as a shade travelling along the arc, not as a
                // second indicator competing with it.
                g.setColour(juce::Colour(0x99356B4B));
                g.strokePath(darkSegment, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // 4. Draw standard flat light-gray pointer indicator line
        const float lineStart = radius * 0.03f;
        const float lineEnd = radius * 0.82f;
        juce::Path indicator;
        indicator.startNewSubPath(centre.x + lineStart * std::sin(angle),
                                  centre.y - lineStart * std::cos(angle));
        indicator.lineTo(centre.x + lineEnd * std::sin(angle),
                         centre.y - lineEnd * std::cos(angle));

        g.setColour(juce::Colour(0xFFD9D9D9));
        g.strokePath(indicator, juce::PathStrokeType(radius * 0.06f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 5. In MAP mode when depth is 0: draw sharp radar green ready-tick at needle tip
        if (slider.getProperties().getWithDefault("isMappingFocus", false) && !hasMod)
        {
            const float tickR1 = radius * 0.86f;
            const float tickR2 = radius * 1.02f;
            float tx1 = centre.x + tickR1 * std::sin(angle);
            float ty1 = centre.y - tickR1 * std::cos(angle);
            float tx2 = centre.x + tickR2 * std::sin(angle);
            float ty2 = centre.y - tickR2 * std::cos(angle);
            g.setColour(juce::Colour(0xFF00E600));
            g.drawLine(tx1, ty1, tx2, ty2, 2.0f);
        }
    }
};

class ToggleSwitchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const bool on = button.getToggleState();
        auto txt = button.getButtonText();
        if (txt == "SINE" || txt == "TRIANGLE" || txt == "SAW" || txt == "SQUARE" ||
            txt == "RANDOM" || txt.contains("MAP") || txt == "FREE" || txt == "SYNC")
        {
            bool isHighlighted = txt.contains("MAPPING") || txt.contains("[MAP]") || txt == "SYNC";
            auto r = bounds.reduced(1.0f);
            
            // Face: sharp flat rectangle, ZERO border
            g.setColour(isHighlighted ? juce::Colour(0xFF0E2A15) : juce::Colour(0xFF161C26));
            g.fillRect(r);

            // Text
            g.setFont(getCockpitFont(12.5f, juce::Font::bold));
            g.setColour(isHighlighted ? juce::Colour(0xFF00E600) : juce::Colour(0xFFD6D1C4));
            g.drawText(txt, bounds.toNearestInt(), juce::Justification::centred);
            return;
        }

        // Underneath depth base (simulating the socket)
        auto base = bounds.withTrimmedTop(4.0f);
        
        // Physical push-down effect: translate the face down by 3 pixels when pressed
        float faceOffsetY = on ? 3.0f : 0.0f;
        auto face = bounds.withHeight(bounds.getHeight() - 4.0f).translated(0.0f, faceOffsetY);

        // Draw socket depth base
        g.setColour(juce::Colour(0xFF141922));
        g.fillRect(base.toNearestInt());

        // Draw button face (cool gray-blue matte)
        g.setColour(on ? juce::Colour(0xFF1E2838) : juce::Colour(0xFF242A34));
        g.fillRect(face.toNearestInt());

        // Draw button text
        const float btnFontSize = (bounds.getWidth() < 55) ? 11.5f : 12.5f;
        g.setFont(getCockpitFont(btnFontSize, juce::Font::bold));
        g.setColour(on ? juce::Colour(0xFF00E5CC) : juce::Colour(0xFFB8C8DB));
        g.drawText(button.getButtonText().toUpperCase(), face.toNearestInt(), juce::Justification::centred);
    }

    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override {}
};

MatchKnobLookAndFeel& getKnobLookAndFeel()
{
    static MatchKnobLookAndFeel laf;
    return laf;
}

ToggleSwitchLookAndFeel& getToggleLookAndFeel()
{
    static ToggleSwitchLookAndFeel laf;
    return laf;
}
}

class ScanFilterSection : public juce::Component
{
public:
    ScanFilterSection(juce::Slider& cutoffIn, juce::Slider& resonanceIn, juce::Slider& speedMsIn,
                      juce::Slider& speedBeatsIn, juce::TextButton& envLoopIn, juce::AudioProcessorValueTreeState& stateIn)
        : cutoff(cutoffIn), resonance(resonanceIn), speedMs(speedMsIn), speedBeats(speedBeatsIn), envLoop(envLoopIn), state(stateIn)
    {
        addAndMakeVisible(cutoff);
        addAndMakeVisible(resonance);
        addAndMakeVisible(speedMs);
        addAndMakeVisible(speedBeats);
        addAndMakeVisible(envLoop);
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override
    {
        drawSectionFrame(g, getLocalBounds(), "SCAN/FILTER");
        auto drawCaption = [&](const juce::Slider& slider, const juce::String& text)
        {
            auto area = slider.getBounds();
            g.setColour(juce::Colour(textColor));
            g.setFont(getCockpitFont(14.0f, juce::Font::bold));
            g.drawText(text, area.getX() - 14, area.getY() - 16, area.getWidth() + 28, 14, juce::Justification::centred);
        };
        drawCaption(cutoff, "CUTOFF");
        drawCaption(resonance, "RESONANCE");
        auto& visibleSpeed = (state.getRawParameterValue("ENV_LOOP_SYNC")->load() > 0.5f) ? speedBeats : speedMs;
        drawCaption(visibleSpeed, "SPEED");
    }

    void resized() override
    {
        cutoff.setBounds(12, 58, 100, 100);
        resonance.setBounds(132, 58, 58, 58);
        auto& visibleSpeed = (state.getRawParameterValue("ENV_LOOP_SYNC")->load() > 0.5f) ? speedBeats : speedMs;
        auto& hiddenSpeed = (&visibleSpeed == &speedBeats) ? speedMs : speedBeats;
        visibleSpeed.setBounds(132, 134, 58, 58);
        hiddenSpeed.setBounds(0, 0, 0, 0);
        envLoop.setBounds(46, 172, 62, 22);
    }

private:
    juce::Slider& cutoff;
    juce::Slider& resonance;
    juce::Slider& speedMs;
    juce::Slider& speedBeats;
    juce::TextButton& envLoop;
    juce::AudioProcessorValueTreeState& state;
};

class AmpEnvelopeSection : public juce::Component
{
public:
    AmpEnvelopeSection(juce::Slider& attackIn, juce::Slider& decayIn, juce::Slider& sustainIn,
                       juce::Slider& releaseIn)
        : attack(attackIn), decay(decayIn), sustain(sustainIn), release(releaseIn)
    {
        addAndMakeVisible(attack);
        addAndMakeVisible(decay);
        addAndMakeVisible(sustain);
        addAndMakeVisible(release);
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override
    {
        drawSectionFrame(g, getLocalBounds(), "AMP ENVELOPE");
        auto drawCaption = [&](const juce::Slider& slider, const juce::String& text)
        {
            auto area = slider.getBounds();
            g.setColour(juce::Colour(textColor));
            g.setFont(getCockpitFont(14.0f, juce::Font::bold));
            g.drawText(text, area.getX() - 14, area.getY() - 16, area.getWidth() + 28, 14, juce::Justification::centred);
        };
        drawCaption(attack, "ATTACK");
        drawCaption(decay, "DECAY");
        drawCaption(sustain, "SUSTAIN");
        drawCaption(release, "RELEASE");
    }

    void resized() override
    {
        attack.setBounds(28, 58, 58, 58);
        decay.setBounds(122, 58, 58, 58);
        sustain.setBounds(28, 134, 58, 58);
        release.setBounds(122, 134, 58, 58);
    }

private:
    juce::Slider& attack;
    juce::Slider& decay;
    juce::Slider& sustain;
    juce::Slider& release;
};

class MotionSection : public juce::Component
{
public:
    MotionSection(juce::Slider& detuneIn, juce::Slider& glideIn, juce::Slider& panIn,
                  juce::Slider& chorusIn, juce::TextButton& legatoIn)
        : detune(detuneIn), glide(glideIn), pan(panIn), chorus(chorusIn), legato(legatoIn)
    {
        addAndMakeVisible(detune);
        addAndMakeVisible(glide);
        addAndMakeVisible(pan);
        addAndMakeVisible(chorus);
        addAndMakeVisible(legato);
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override
    {
        drawSectionFrame(g, getLocalBounds(), "MOTION");
        auto drawCaption = [&](const juce::Slider& slider, const juce::String& text, int labelY)
        {
            auto area = slider.getBounds();
            g.setColour(juce::Colour(textColor));
            g.setFont(getCockpitFont(13.5f, juce::Font::bold));
            g.drawText(text, area.getX() - 14, labelY, area.getWidth() + 28, 14, juce::Justification::centred);
        };
        drawCaption(detune, "DETUNE", 40);
        drawCaption(glide, "GLIDE", 40);
        drawCaption(pan, "PAN", 126);
        drawCaption(chorus, "CHORUS", 126);
    }

    void resized() override
    {
        detune.setBounds(16, 56, 56, 56);
        glide.setBounds(134, 56, 56, 56);
        legato.setBounds(73, 114, 58, 22);
        pan.setBounds(16, 144, 56, 56);
        chorus.setBounds(134, 144, 56, 56);
    }

private:
    juce::Slider& detune;
    juce::Slider& glide;
    juce::Slider& pan;
    juce::Slider& chorus;
    juce::TextButton& legato;
};

class SpaceFxSection : public juce::Component
{
public:
    SpaceFxSection(juce::Slider& delayIn, juce::Slider& reverbMixIn, juce::Slider& reverbSizeIn, juce::Slider& decimatorIn)
        : delay(delayIn), reverbMix(reverbMixIn), reverbSize(reverbSizeIn), decimator(decimatorIn)
    {
        addAndMakeVisible(delay);
        addAndMakeVisible(reverbMix);
        addAndMakeVisible(reverbSize);
        addAndMakeVisible(decimator);
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override
    {
        drawSectionFrame(g, getLocalBounds(), "SPACE/FX");
        auto drawCaption = [&](const juce::Slider& slider, const juce::String& text, int labelY)
        {
            auto area = slider.getBounds();
            g.setColour(juce::Colour(textColor));
            g.setFont(getCockpitFont(13.5f, juce::Font::bold));
            g.drawText(text, area.getX() - 14, labelY, area.getWidth() + 28, 14, juce::Justification::centred);
        };
        drawCaption(delay, "DELAY", 40);
        drawCaption(decimator, "DECIMATOR", 40);
        drawCaption(reverbMix, "REVERB", 126);
        drawCaption(reverbSize, "SIGNAL LOSS", 126);
    }

    void resized() override
    {
        // 2x2 clean, symmetrical layout matching other blocks
        delay.setBounds(24, 58, 58, 58);
        decimator.setBounds(122, 58, 58, 58);
        reverbMix.setBounds(24, 142, 58, 58);
        reverbSize.setBounds(122, 142, 58, 58);
    }

private:
    juce::Slider& delay;
    juce::Slider& reverbMix;
    juce::Slider& reverbSize;
    juce::Slider& decimator;
};

class FilterEnvelopeSection : public juce::Component
{
public:
    FilterEnvelopeSection(juce::Slider& atkIn, juce::Slider& decIn, juce::Slider& susIn, juce::Slider& amtIn)
        : attack(atkIn), decay(decIn), sustain(susIn), envAmount(amtIn)
    {
        addAndMakeVisible(attack);
        addAndMakeVisible(decay);
        addAndMakeVisible(sustain);
        addAndMakeVisible(envAmount);
        setInterceptsMouseClicks(false, true);
    }

    void paint(juce::Graphics& g) override
    {
        drawSectionFrame(g, getLocalBounds(), "FILTER ENVELOPE");
        auto drawCaption = [&](const juce::Slider& slider, const juce::String& text)
        {
            auto area = slider.getBounds();
            g.setColour(juce::Colour(textColor));
            g.setFont(getCockpitFont(14.0f, juce::Font::bold));
            g.drawText(text, area.getX() - 14, area.getY() - 16, area.getWidth() + 28, 14, juce::Justification::centred);
        };
        drawCaption(attack,    "ATTACK");
        drawCaption(decay,     "DECAY");
        drawCaption(sustain,   "SUSTAIN");
        drawCaption(envAmount, "AMOUNT");
    }

    void resized() override
    {
        // 2x2 grid identically sized and positioned to AMP ENVELOPE (58x58 knobs)
        attack.setBounds(28, 58, 58, 58);
        decay.setBounds(122, 58, 58, 58);
        sustain.setBounds(28, 134, 58, 58);
        envAmount.setBounds(122, 134, 58, 58);
    }

private:
    juce::Slider& attack;
    juce::Slider& decay;
    juce::Slider& sustain;
    juce::Slider& envAmount;
};

class LfoSection : public juce::Component
{
public:
    LfoSection(juce::Slider& rateSliderIn, juce::Slider& beatsSliderIn, juce::AudioProcessorValueTreeState& stateIn,
               StableAliveAudioProcessor& procIn, UtilityPanel& panelIn)
        : rateSlider(rateSliderIn), beatsSlider(beatsSliderIn), state(stateIn), processor(procIn), panel(panelIn)
    {
        addAndMakeVisible(rateSlider);
        addChildComponent(beatsSlider);
        
        if (auto* param = state.getParameter("LFO_SHAPE"))
        {
            activeShape = juce::jlimit(0, 4, (int) std::round(param->getValue() * 4.0f));
            prevShape = activeShape;
        }

        // 1. Single Shape Button that cycles waveforms on each click
        updateShapeButtonText();
        shapeButton.onClick = [this]()
        {
            if (auto* param = state.getParameter("LFO_SHAPE"))
            {
                prevShape = activeShape;
                activeShape = (activeShape + 1) % 5;
                shapeMorphProgress = 0.0f;
                param->setValueNotifyingHost(param->convertTo0to1((float)activeShape));
                updateShapeButtonText();
                repaint();
            }
        };
        addAndMakeVisible(shapeButton);
        shapeButton.setLookAndFeel(&getToggleLookAndFeel());

        // 2. SYNC button toggles tempo sync / free running
        updateSyncAndSliders();
        syncButton.onClick = [this]()
        {
            if (auto* param = state.getParameter("LFO_SYNC"))
            {
                bool curr = param->getValue() > 0.5f;
                param->setValueNotifyingHost(curr ? 0.0f : 1.0f);
                updateSyncAndSliders();
                repaint();
            }
        };
        addAndMakeVisible(syncButton);
        syncButton.setLookAndFeel(&getToggleLookAndFeel());

        // 3. MAP button toggles mapping mode on/off
        updateMapButtonText();
        mapButton.onClick = [this]()
        {
            panel.toggleMappingMode();
            updateMapButtonText();
            repaint();
        };
        addAndMakeVisible(mapButton);
        mapButton.setLookAndFeel(&getToggleLookAndFeel());

        // Dragging sliders auto-selects their corresponding sync mode
        rateSlider.onDragStart = [this]()
        {
            if (auto* param = state.getParameter("LFO_SYNC"))
                param->setValueNotifyingHost(0.0f);
            updateSyncAndSliders();
            repaint();
        };

        beatsSlider.onDragStart = [this]()
        {
            if (auto* param = state.getParameter("LFO_SYNC"))
                param->setValueNotifyingHost(1.0f);
            updateSyncAndSliders();
            repaint();
        };
    }

    ~LfoSection() override
    {
        shapeButton.setLookAndFeel(nullptr);
        syncButton.setLookAndFeel(nullptr);
        mapButton.setLookAndFeel(nullptr);
    }

    void updateSyncAndSliders()
    {
        bool isSync = state.getRawParameterValue("LFO_SYNC") ? (state.getRawParameterValue("LFO_SYNC")->load() > 0.5f) : false;
        syncButton.setButtonText(isSync ? "SYNC" : "FREE");
        resized();
    }

    void updateShapeButtonText()
    {
        static const char* shapeNames[] = { "SINE", "TRIANGLE", "SAW", "SQUARE", "RANDOM" };
        if (auto* param = state.getParameter("LFO_SHAPE"))
            activeShape = juce::jlimit(0, 4, (int) std::round(param->getValue() * 4.0f));
        shapeButton.setButtonText(shapeNames[activeShape]);
    }

    void updateMapButtonText()
    {
        bool isMapping = panel.getIsMappingMode();
        mapButton.setButtonText(isMapping ? "[MAP]" : "MAP");
    }

    static float evaluateShape(int shape, float phase)
    {
        float p = std::fmod(phase, 1.0f);
        if (p < 0.0f) p += 1.0f;

        switch (shape)
        {
            case 0: // SINE
                return std::sin(p * juce::MathConstants<float>::twoPi);
            case 1: // TRIANGLE
                return 4.0f * std::abs(p - 0.5f) - 1.0f;
            case 2: // SAW
                return 1.0f - 2.0f * p;
            case 3: // SQUARE
                return (p < 0.5f) ? 0.95f : -0.95f;
            case 4: // RANDOM / S&H step wave
            {
                int step = (int)(p * 8.0f);
                return std::sin((float)step * 123.456f);
            }
            default:
                return std::sin(p * juce::MathConstants<float>::twoPi);
        }
    }

    void paint(juce::Graphics& g) override
    {
        drawSectionFrame(g, getLocalBounds(), "LFO / TELEMETRY");

        // Clean, sharp flat matte screen bounds
        auto screenRect = juce::Rectangle<float>(12.0f, 38.0f, 182.0f, 82.0f);
        
        // Dark flat matte background (NO CRT gradient / NO fake scanlines!)
        g.setColour(juce::Colour(0xFF090D14));
        g.fillRect(screenRect);
        g.setColour(juce::Colour(0xFF1E2532));
        g.drawRect(screenRect, 1.0f);

        // Minimal hairline reticle grid
        const float midY = screenRect.getCentreY();
        g.setColour(juce::Colour(0x1800E600));
        g.drawHorizontalLine((int)midY, screenRect.getX() + 3.0f, screenRect.getRight() - 3.0f);

        for (float tx = screenRect.getX() + 10.0f; tx < screenRect.getRight() - 8.0f; tx += 10.0f)
            g.drawVerticalLine((int)tx, midY - 2.0f, midY + 3.0f);

        const float colW = screenRect.getWidth() / 4.0f;
        for (int i = 1; i < 4; ++i)
        {
            float gx = screenRect.getX() + (float) i * colW;
            g.drawVerticalLine((int)gx, screenRect.getY() + 3.0f, screenRect.getBottom() - 3.0f);
        }

        // Smooth Shape Morphing Animation step
        if (shapeMorphProgress < 1.0f)
        {
            shapeMorphProgress = std::min(1.0f, shapeMorphProgress + 0.08f);
            if (shapeMorphProgress < 1.0f)
                repaint();
        }

        float t = shapeMorphProgress;
        float smoothT = t * t * (3.0f - 2.0f * t);
        bool isSync = state.getRawParameterValue("LFO_SYNC") ? (state.getRawParameterValue("LFO_SYNC")->load() > 0.5f) : false;

        // Real DSP LFO phase (sweeps from 0.0 to 1.0 across the screen at actual speed)
        float realPhase = processor.lfoCurrentPhase.load();
        realPhase = std::fmod(realPhase, 1.0f);
        if (realPhase < 0.0f) realPhase += 1.0f;

        float sweepX = screenRect.getX() + realPhase * screenRect.getWidth();
        const float gapHalfWidth = 8.0f; // 16px wide vertical gap

        juce::Path wavePath;
        const int numPts = (int) screenRect.getWidth();
        const float waveAmp = (screenRect.getHeight() * 0.5f) - 10.0f;
        bool inSubPath = false;

        for (int px = 0; px < numPts; ++px)
        {
            float drawX = screenRect.getX() + (float) px;
            float dist = std::abs(drawX - sweepX);
            dist = std::min(dist, screenRect.getWidth() - dist);

            // Dikey bir boşluk lfo'yu gerçek hızında takip etsin
            if (dist < gapHalfWidth)
            {
                inSubPath = false;
                continue;
            }

            // Stationary wave coordinates (ZERO horizontal motion / ZERO dizziness!)
            float normX = (float) px / (float) numPts;
            float cyclePhase = std::fmod(normX * 2.0f, 1.0f);

            float yFrom = evaluateShape(prevShape, cyclePhase);
            float yTo   = evaluateShape(activeShape, cyclePhase);
            float sampleY = yFrom * (1.0f - smoothT) + yTo * smoothT;
            float drawY = midY - sampleY * waveAmp;

            if (!inSubPath)
            {
                wavePath.startNewSubPath(drawX, drawY);
                inSubPath = true;
            }
            else
            {
                wavePath.lineTo(drawX, drawY);
            }
        }

        // Radar Green trace line (crisp, stationary with clean vertical gap)
        g.setColour(juce::Colour(0xFF00E600));
        g.strokePath(wavePath, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Caption for knob (RATE or SYNC)
        g.setColour(juce::Colour(textColor));
        g.setFont(getCockpitFont(13.5f, juce::Font::bold));
        g.drawText(isSync ? "SYNC" : "RATE", 6, 120, 72, 16, juce::Justification::centred);

        // Update button text and border state
        updateShapeButtonText();
        updateMapButtonText();
    }

    void resized() override
    {
        bool isSync = state.getRawParameterValue("LFO_SYNC") ? (state.getRawParameterValue("LFO_SYNC")->load() > 0.5f) : false;
        if (isSync)
        {
            beatsSlider.setBounds(14, 134, 56, 56);
            beatsSlider.setVisible(true);
            rateSlider.setBounds(0, 0, 0, 0);
            rateSlider.setVisible(false);
        }
        else
        {
            rateSlider.setBounds(14, 134, 56, 56);
            rateSlider.setVisible(true);
            beatsSlider.setBounds(0, 0, 0, 0);
            beatsSlider.setVisible(false);
        }

        shapeButton.setBounds(82, 132, 112, 26);
        syncButton.setBounds(82, 164, 53, 26);
        mapButton.setBounds(141, 164, 53, 26);
    }

private:
    juce::Slider& rateSlider;
    juce::Slider& beatsSlider;
    juce::TextButton shapeButton;
    juce::TextButton syncButton;
    juce::TextButton mapButton;
    juce::AudioProcessorValueTreeState& state;
    StableAliveAudioProcessor& processor;
    UtilityPanel& panel;

    int activeShape = 0;
    int prevShape = 0;
    float shapeMorphProgress = 1.0f;
    float displayedRealPhase = 0.0f;
    juce::int64 lastPhaseUpdateTime = 0;
};

class UtilityPanel::LeftDimmerOverlay : public juce::Component
{
public:
    LeftDimmerOverlay(UtilityPanel& p) : panel(p)
    {
        setInterceptsMouseClicks(true, false);
    }

    bool hitTest(int x, int) override
    {
        if (panel.getDrawerProgress() <= 0.05f)
            return false;

        // Any click on or inside the drawer or its notch is passed to the drawer
        float drawerX = 244.0f + (1.0f - panel.getDrawerProgress()) * 222.0f;
        if (x >= (int)drawerX - 12)
            return false;

        // Click anywhere to the left of the drawer (left cards & central gap) closes the drawer
        return true;
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (panel.getDrawerProgress() > 0.05f)
        {
            panel.setDrawerOpen(false);
        }
    }

    void paint(juce::Graphics& g) override
    {
        float progress = panel.getDrawerProgress();
        if (progress <= 0.001f)
            return;

        auto bounds = getLocalBounds().toFloat();

        // 1. Dark semi-transparent matte cockpit blackout tint across the ENTIRE base layer
        // (left cards, central gap, and right underlying controls all blacken out smoothly together)
        g.setColour(juce::Colour(0xEE080B12).withMultipliedAlpha(progress));
        g.fillRect(bounds);

        // 2. Subtle frosted HUD crosshatch scanlines for an avionic glass depth effect (zero CPU lag)
        g.setColour(juce::Colour(0x18000000).withMultipliedAlpha(progress));
        for (int y = (int)bounds.getY(); y < (int)bounds.getBottom(); y += 3)
            g.drawHorizontalLine(y, bounds.getX(), bounds.getRight());

        // 3. Vignette towards edges
        juce::ColourGradient vig(juce::Colour(0x18000000).withMultipliedAlpha(progress), bounds.getCentreX(), bounds.getCentreY(),
                                 juce::Colour(0x60000000).withMultipliedAlpha(progress), bounds.getX(), bounds.getY(), true);
        g.setGradientFill(vig);
        g.fillRect(bounds);
    }

private:
    UtilityPanel& panel;
};

class UtilityPanel::DrawerNotchButton : public juce::Component
{
public:
    DrawerNotchButton(UtilityPanel& p) : panel(p)
    {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        bool over = isMouseOver();
        bool open = panel.isDrawerOpen();

        // 1. Sleek cockpit metal pull-tab body (zero border, zero radius, flush flat metal tab)
        g.setColour(over ? juce::Colour(0xFF1E2530) : juce::Colour(0xFF11151D));
        g.fillRect(bounds);

        // 2. Minimal avionic leading edge hairline (1px indicator bar on the left seam)
        juce::Colour accentCol = open ? juce::Colour(0xFFFF9100) : (over ? juce::Colour(0xFF00E600) : juce::Colour(0xFF3B485A));
        g.setColour(accentCol);
        g.drawVerticalLine(0, bounds.getY(), bounds.getBottom());

        float cx = bounds.getCentreX() + 0.5f;
        float cy = bounds.getCentreY();

        // 3. Ultra-crisp minimal directional chevron
        g.setColour(over ? juce::Colour(0xFFFFFFFF) : juce::Colour(0xFF8FA0B5));
        const float arrowW = 2.8f;
        const float arrowH = 4.5f;
        juce::Path p;
        if (open)
        {
            // '>'
            p.startNewSubPath(cx - arrowW * 0.5f, cy - arrowH);
            p.lineTo(cx + arrowW * 0.5f, cy);
            p.lineTo(cx - arrowW * 0.5f, cy + arrowH);
        }
        else
        {
            // '<'
            p.startNewSubPath(cx + arrowW * 0.5f, cy - arrowH);
            p.lineTo(cx - arrowW * 0.5f, cy);
            p.lineTo(cx + arrowW * 0.5f, cy + arrowH);
        }
        g.strokePath(p, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains(e.getPosition()))
            panel.toggleDrawer();
    }

private:
    UtilityPanel& panel;
};

UtilityPanel::UtilityPanel(juce::AudioProcessorValueTreeState& vts, StableAliveAudioProcessor& proc)
    : apvts(vts), processor(proc)
{
    cutoffSlider.onDoubleClickAction = [this]()
    {
        if (auto* param = apvts.getParameter("FILTER_TYPE"))
        {
            int currentVal = (int) std::round(param->getValue() * 2.0f);
            int nextVal = (currentVal + 1) % 3;
            param->setValueNotifyingHost(param->convertTo0to1(nextVal));
            repaint();
        }
    };

    for (juce::Slider* slider : std::initializer_list<juce::Slider*>{ &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &resonanceSlider,
                                  static_cast<juce::Slider*>(&cutoffSlider), &decimatorSlider, &reverbSizeSlider, &reverbMixSlider, &detuneSlider,
                                  &envLoopMsSlider, &envLoopBeatsSlider, &glideSlider, &chorusSlider, &delaySlider, &panSlider })
    {
        setupSlider(*slider, {});
        slider->setLookAndFeel(&getKnobLookAndFeel());
    }

    for (auto* button : { &legatoButton, &envLoopButton })
    {
        button->setClickingTogglesState(true);
        button->setLookAndFeel(&getToggleLookAndFeel());
    }



    attackSlider.textFromValueFunction = [](double v) { return juce::String(v, 2) + "s"; };
    decaySlider.textFromValueFunction = [](double v) { return juce::String(v, 2) + "s"; };
    sustainSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    releaseSlider.textFromValueFunction = [](double v) { return juce::String(v, 2) + "s"; };
    resonanceSlider.textFromValueFunction = [](double v) { return juce::String(v, 2); };
    cutoffSlider.textFromValueFunction = [](double v)
    {
        if (v < 1000.0)
            return juce::String((int)std::round(v)) + " Hz";
        else
            return juce::String(v / 1000.0, 2) + " kHz";
    };
    decimatorSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    reverbMixSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    reverbSizeSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    detuneSlider.textFromValueFunction = [](double v) { return juce::String(v, 2) + " st"; };
    glideSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    chorusSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    delaySlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    panSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    envLoopMsSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v)) + " ms"; };
    envLoopBeatsSlider.textFromValueFunction = [](double v)
    {
        if (std::abs(v - 0.125) < 0.001) return juce::String("1/8");
        if (std::abs(v - 0.25) < 0.001) return juce::String("1/4");
        if (std::abs(v - 0.5) < 0.001) return juce::String("1/2");
        if (std::abs(v - 1.0) < 0.001) return juce::String("1");
        if (std::abs(v - 2.0) < 0.001) return juce::String("2");
        if (std::abs(v - 4.0) < 0.001) return juce::String("4");
        return juce::String(v, 2);
    };

    atkAttach = std::make_unique<SliderAttachment>(apvts, "ATTACK", attackSlider);
    decAttach = std::make_unique<SliderAttachment>(apvts, "DECAY", decaySlider);
    susAttach = std::make_unique<SliderAttachment>(apvts, "SUSTAIN", sustainSlider);
    relAttach = std::make_unique<SliderAttachment>(apvts, "RELEASE", releaseSlider);
    rsnAttach = std::make_unique<SliderAttachment>(apvts, "RESONANCE", resonanceSlider);
    cutAttach = std::make_unique<SliderAttachment>(apvts, "CUTOFF", cutoffSlider);
    dcmAttach = std::make_unique<SliderAttachment>(apvts, "DECIMATOR", decimatorSlider);
    rszAttach = std::make_unique<SliderAttachment>(apvts, "REVERB_SIZE", reverbSizeSlider);
    rmxAttach = std::make_unique<SliderAttachment>(apvts, "REVERB_MIX", reverbMixSlider);
    detuneAttach = std::make_unique<SliderAttachment>(apvts, "DETUNE", detuneSlider);
    glideAttach = std::make_unique<SliderAttachment>(apvts, "GLIDE", glideSlider);
    chorusAttach = std::make_unique<SliderAttachment>(apvts, "CHORUS", chorusSlider);
    delayMixAttach = std::make_unique<SliderAttachment>(apvts, "DELAY_MIX", delaySlider);
    panMixAttach = std::make_unique<SliderAttachment>(apvts, "PAN_MIX", panSlider);
    envLoopMsAttach = std::make_unique<SliderAttachment>(apvts, "ENV_LOOP_MS", envLoopMsSlider);
    envLoopBeatsAttach = std::make_unique<SliderAttachment>(apvts, "ENV_LOOP_BEATS", envLoopBeatsSlider);
    legatoAttach = std::make_unique<ButtonAttachment>(apvts, "LEGATO", legatoButton);
    envlAttach = std::make_unique<ButtonAttachment>(apvts, "ENV_LOOP", envLoopButton);

    fAtkAttach = std::make_unique<SliderAttachment>(apvts, "FILTER_ATTACK", filterAttackSlider);
    fDecAttach = std::make_unique<SliderAttachment>(apvts, "FILTER_DECAY", filterDecaySlider);
    fSusAttach = std::make_unique<SliderAttachment>(apvts, "FILTER_SUSTAIN", filterSustainSlider);
    fAmtAttach = std::make_unique<SliderAttachment>(apvts, "FILTER_ENV_AMOUNT", filterEnvAmountSlider);

    for (juce::Slider* slider : std::initializer_list<juce::Slider*>{ &filterAttackSlider, &filterDecaySlider, &filterSustainSlider, &filterEnvAmountSlider, &lfoRateSlider, &lfoBeatsSlider })
    {
        setupSlider(*slider, {});
        slider->setLookAndFeel(&getKnobLookAndFeel());
    }

    lfoRateAttach = std::make_unique<SliderAttachment>(apvts, "LFO_RATE", lfoRateSlider);
    lfoRateSlider.textFromValueFunction = [](double v) { return juce::String(v, (v < 1.0) ? 2 : 1) + " Hz"; };

    lfoBeatsAttach = std::make_unique<SliderAttachment>(apvts, "LFO_BEATS", lfoBeatsSlider);
    lfoBeatsSlider.textFromValueFunction = [](double v)
    {
        static const char* beatLabels[] = { "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" };
        int idx = juce::jlimit(0, 7, (int) std::round(v));
        return juce::String(beatLabels[idx]);
    };

    filterAttackSlider.textFromValueFunction = [](double v) { return juce::String(v, 2) + "s"; };
    filterDecaySlider.textFromValueFunction = [](double v) { return juce::String(v, 2) + "s"; };
    filterSustainSlider.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100.0)) + "%"; };
    filterEnvAmountSlider.textFromValueFunction = [](double v)
    {
        if (std::abs(v) < 1000.0)
            return juce::String((int)std::round(v)) + " Hz";
        else
            return juce::String(v / 1000.0, 2) + " kHz";
    };

    scanFilterSection = std::make_unique<ScanFilterSection>(cutoffSlider, resonanceSlider, envLoopMsSlider, envLoopBeatsSlider, envLoopButton, apvts);
    ampEnvelopeSection = std::make_unique<AmpEnvelopeSection>(attackSlider, decaySlider, sustainSlider, releaseSlider);
    motionSection = std::make_unique<MotionSection>(detuneSlider, glideSlider, panSlider, chorusSlider, legatoButton);
    spaceFxSection = std::make_unique<SpaceFxSection>(delaySlider, reverbMixSlider, reverbSizeSlider, decimatorSlider);

    // Slide-out drawer sections (Overlays Amp Env and Space FX)
    filterEnvelopeSection = std::make_unique<FilterEnvelopeSection>(filterAttackSlider, filterDecaySlider, filterSustainSlider, filterEnvAmountSlider);
    page2TopLeftSection = std::make_unique<LfoSection>(lfoRateSlider, lfoBeatsSlider, apvts, processor, *this);

    leftDimmerOverlay = std::make_unique<LeftDimmerOverlay>(*this);
    notchButton = std::make_unique<DrawerNotchButton>(*this);

    // 1. Base sections
    for (juce::Component* section : { static_cast<juce::Component*>(scanFilterSection.get()),
                                      static_cast<juce::Component*>(motionSection.get()),
                                      static_cast<juce::Component*>(ampEnvelopeSection.get()),
                                      static_cast<juce::Component*>(spaceFxSection.get()) })
        addAndMakeVisible(section);

    // 2. Left Dimmer Overlay (sits above base left sections to dim & intercept clicks when drawer is open)
    addAndMakeVisible(leftDimmerOverlay.get());

    // 3. Sliding drawer sections (sit above right sections)
    addChildComponent(filterEnvelopeSection.get());
    addChildComponent(page2TopLeftSection.get());

    // 4. Notch toggle button (sits top-most, glides with drawer)
    addAndMakeVisible(notchButton.get());

    // Setup interactive LFO target mapping on ALL synthesis controls (knob value NEVER changes!)
    auto setupMappingSlider = [this](CockpitSlider& slider, const char* paramId)
    {
        slider.onMappingDragStart = [this, &slider]()
        {
            slider.getProperties().set("isDraggingMapping", true);
            repaint();
        };

        slider.onMappingDrag = [this, &slider, paramId](float newAmt)
        {
            if (auto* p = apvts.getParameter(paramId))
                p->setValueNotifyingHost(newAmt);

            slider.getProperties().set("lfoAmount", (double) newAmt);
            slider.repaint();
            repaint();
        };

        slider.onMappingDragEnd = [this, &slider]()
        {
            slider.getProperties().remove("isDraggingMapping");
            repaint();
        };
    };

    setupMappingSlider(cutoffSlider, "LFO_CUTOFF_AMT");
    setupMappingSlider(resonanceSlider, "LFO_RESO_AMT");
    setupMappingSlider(attackSlider, "LFO_ATTACK_AMT");
    setupMappingSlider(decaySlider, "LFO_DECAY_AMT");
    setupMappingSlider(sustainSlider, "LFO_SUSTAIN_AMT");
    setupMappingSlider(releaseSlider, "LFO_RELEASE_AMT");
    setupMappingSlider(detuneSlider, "LFO_DETUNE_AMT");
    setupMappingSlider(glideSlider, "LFO_GLIDE_AMT");
    setupMappingSlider(chorusSlider, "LFO_CHORUS_AMT");
    setupMappingSlider(panSlider, "LFO_PAN_AMT");
    setupMappingSlider(delaySlider, "LFO_DELAY_AMT");
    setupMappingSlider(reverbMixSlider, "LFO_REV_MIX_AMT");
    setupMappingSlider(reverbSizeSlider, "LFO_REV_SIZE_AMT");
    setupMappingSlider(decimatorSlider, "LFO_DECIMATOR_AMT");
    setupMappingSlider(filterAttackSlider, "LFO_FILTER_ATTACK_AMT");
    setupMappingSlider(filterDecaySlider, "LFO_FILTER_DECAY_AMT");
    setupMappingSlider(filterSustainSlider, "LFO_FILTER_SUSTAIN_AMT");
    setupMappingSlider(filterEnvAmountSlider, "LFO_FILTER_ENV_AMT");

    setDrawerOpen(false);
    updateDrawerLayout();

    envLoopMsSlider.onDragStart = [this]()
    {
        if (auto* param = apvts.getParameter("ENV_LOOP_SYNC"))
            param->setValueNotifyingHost(0.0f);
        resized();
        repaint();
    };

    envLoopBeatsSlider.onDragStart = [this]()
    {
        if (auto* param = apvts.getParameter("ENV_LOOP_SYNC"))
            param->setValueNotifyingHost(1.0f);
        resized();
        repaint();
    };

    startTimerHz(60);
}

UtilityPanel::~UtilityPanel()
{
    stopTimer();
    for (juce::Slider* slider : std::initializer_list<juce::Slider*>{ &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &resonanceSlider,
                                  static_cast<juce::Slider*>(&cutoffSlider), &decimatorSlider, &reverbSizeSlider, &reverbMixSlider, &detuneSlider,
                                  &envLoopMsSlider, &envLoopBeatsSlider, &glideSlider, &chorusSlider, &delaySlider, &panSlider,
                                  &filterAttackSlider, &filterDecaySlider, &filterSustainSlider, &filterEnvAmountSlider, &lfoRateSlider, &lfoBeatsSlider })
        slider->setLookAndFeel(nullptr);

    for (auto* button : { &legatoButton, &envLoopButton })
        button->setLookAndFeel(nullptr);
}

void UtilityPanel::timerCallback()
{
    // Animate Cutoff press scale (faster, clickier decay for a mechanical feel)
    if (cutoffSlider.getProperties().contains("pressScale"))
    {
        float p = cutoffSlider.getProperties()["pressScale"];
        if (p > 0.0f)
        {
            p -= 0.18f; // Mechanical spring-like fast recovery (~5-6 frames)
            if (p < 0.0f) p = 0.0f;
            cutoffSlider.getProperties().set("pressScale", p);
            cutoffSlider.repaint();
        }
    }

    // Monitor filter type changes to show display bubble automatically
    int currentFilterType = 0;
    if (auto* param = apvts.getParameter("FILTER_TYPE"))
        currentFilterType = juce::jlimit(0, 2, (int) std::round(param->getValue() * 2.0f));

    if (lastFilterType == -1)
    {
        lastFilterType = currentFilterType;
    }
    else if (currentFilterType != lastFilterType)
    {
        lastFilterType = currentFilterType;
        filterTypeChangeTimer = 90; // Show for 1.5s on change
    }

    if (filterTypeChangeTimer > 0)
    {
        filterTypeChangeTimer--;
        // Don't repaint the whole panel, just the cutoffSlider area to trigger value bubble update
        cutoffSlider.repaint();
    }

    const float targetLegato = legatoButton.getToggleState() ? 1.0f : 0.0f;
    const float targetEnvLoop = envLoopButton.getToggleState() ? 1.0f : 0.0f;

    if (std::abs(legatoTransition - targetLegato) > 0.005f)
    {
        legatoTransition += (targetLegato - legatoTransition) * 0.2f;
        legatoButton.getProperties().set("transition", legatoTransition);
        legatoButton.repaint();
    }
    else if (legatoTransition != targetLegato)
    {
        legatoTransition = targetLegato;
        legatoButton.getProperties().set("transition", legatoTransition);
        legatoButton.repaint();
    }

    if (std::abs(envLoopTransition - targetEnvLoop) > 0.005f)
    {
        envLoopTransition += (targetEnvLoop - envLoopTransition) * 0.2f;
        envLoopButton.getProperties().set("transition", envLoopTransition);
        envLoopButton.repaint();
    }
    else if (envLoopTransition != targetEnvLoop)
    {
        envLoopTransition = targetEnvLoop;
        envLoopButton.getProperties().set("transition", envLoopTransition);
        envLoopButton.repaint();
    }

    // Smooth mechanical sliding drawer animation (fast, critically damped 60 FPS ease-out)
    bool isDrawerMoving = false;
    if (std::abs(drawerProgress - targetDrawerProgress) > 0.001f)
    {
        isDrawerMoving = true;
        float diff = targetDrawerProgress - drawerProgress;
        float step = diff * 0.26f;
        if (std::abs(step) < 0.015f)
            step = (diff > 0.0f) ? 0.015f : -0.015f;

        drawerProgress += step;
        if (std::abs(drawerProgress - targetDrawerProgress) <= 0.018f)
        {
            drawerProgress = targetDrawerProgress;
        }
        updateDrawerLayout();
        if (leftDimmerOverlay)
            leftDimmerOverlay->repaint();
        if (notchButton)
            notchButton->repaint();
        repaint();
    }

    const bool isSliding = isDrawerMoving;

    // Smooth visual interpolation for rotary sliders (only on active visible page)
    if (!isSliding)
    {
        for (juce::Slider* slider : std::initializer_list<juce::Slider*>{ &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &resonanceSlider,
                                      static_cast<juce::Slider*>(&cutoffSlider), &decimatorSlider, &reverbSizeSlider, &reverbMixSlider, &detuneSlider,
                                      &envLoopMsSlider, &envLoopBeatsSlider, &glideSlider, &chorusSlider, &delaySlider, &panSlider,
                                      &filterAttackSlider, &filterDecaySlider, &filterSustainSlider, &filterEnvAmountSlider })
        {
            if (!slider->isShowing())
            {
                slider->getProperties().remove("visualVal");
                slider->getProperties().remove("animActive");
                continue;
            }

            double actualVal = slider->getValue();

            if (slider->isMouseOverOrDragging())
            {
                slider->getProperties().remove("visualVal");
                slider->getProperties().remove("animActive");
            }
            else
            {
                // If randomization animation was triggered, wait for the actual value to change from starting value
                if (slider->getProperties().getWithDefault("animActive", false))
                {
                    double startVal = slider->getProperties().getWithDefault("animStartVal", actualVal);
                    if (actualVal != startVal)
                    {
                        slider->getProperties().set("visualVal", startVal);
                        slider->getProperties().remove("animActive");
                    }
                }
            }

            if (slider->getProperties().contains("visualVal"))
            {
                double visualVal = slider->getProperties()["visualVal"];
                if (std::abs(visualVal - actualVal) > 0.001)
                {
                    visualVal += (actualVal - visualVal) * 0.22; // Snappy oil-damped glide
                    slider->getProperties().set("visualVal", visualVal);
                    slider->repaint();
                }
                else
                {
                    slider->getProperties().remove("visualVal");
                    slider->repaint();
                }
            }
        }
    }

    // Repaint panel to update OSC mode selection bar when randomized
    int currentOscMode = 2;
    if (auto* param = apvts.getParameter("OSC_MODE"))
        currentOscMode = juce::jlimit(0, 3, (int) std::round(param->getValue() * 3.0f));

    if (currentOscMode != lastOscMode)
    {
        lastOscMode = currentOscMode;
        repaint();
    }

    // Real-time LFO indicator. This used to clamp the movement to a fixed step
    // per frame, which made the marker travel at a constant speed and snap at
    // the turning points - the motion read as mechanical rather than as an LFO.
    // A one-pole filter instead eases towards the true value, so the sweep keeps
    // the shape of the waveform and stays smooth at high rates, where the LFO
    // moves faster than the 60 Hz repaint can follow anyway.
    float rawLfoVal = processor.lfoCurrentValue.load();
    const float lfoSmoothing = 0.28f;
    visualLfoVal += lfoSmoothing * (rawLfoVal - visualLfoVal);

    // Lower threshold than before: with smoothing the steps are small, and a
    // coarse threshold is what made the marker stutter between frames.
    bool lfoMovedSignificantly = (std::abs(visualLfoVal - lastRepaintedLfoVal) > 0.0015f);
    if (lfoMovedSignificantly)
        lastRepaintedLfoVal = visualLfoVal;

    struct ModTarget { juce::Slider* s; const char* p; };
    const ModTarget modTargets[] = {
        { static_cast<juce::Slider*>(&cutoffSlider), "LFO_CUTOFF_AMT" },
        { &resonanceSlider, "LFO_RESO_AMT" },
        { &attackSlider, "LFO_ATTACK_AMT" },
        { &decaySlider, "LFO_DECAY_AMT" },
        { &sustainSlider, "LFO_SUSTAIN_AMT" },
        { &releaseSlider, "LFO_RELEASE_AMT" },
        { &detuneSlider, "LFO_DETUNE_AMT" },
        { &glideSlider, "LFO_GLIDE_AMT" },
        { &chorusSlider, "LFO_CHORUS_AMT" },
        { &panSlider, "LFO_PAN_AMT" },
        { &delaySlider, "LFO_DELAY_AMT" },
        { &reverbMixSlider, "LFO_REV_MIX_AMT" },
        { &reverbSizeSlider, "LFO_REV_SIZE_AMT" },
        { &decimatorSlider, "LFO_DECIMATOR_AMT" },
        { &filterAttackSlider, "LFO_FILTER_ATTACK_AMT" },
        { &filterDecaySlider, "LFO_FILTER_DECAY_AMT" },
        { &filterSustainSlider, "LFO_FILTER_SUSTAIN_AMT" },
        { &filterEnvAmountSlider, "LFO_FILTER_ENV_AMT" }
    };

    for (const auto& item : modTargets)
    {
        if (auto* param = apvts.getRawParameterValue(item.p))
        {
            double amt = (double) param->load();
            item.s->getProperties().set("lfoAmount", amt);
            item.s->getProperties().set("lfoCurrentVal", (double) visualLfoVal);
            if ((amt > 0.001 && lfoMovedSignificantly) || isMappingMode)
                item.s->repaint();
        }
    }
    
    if (page2TopLeftSection && page2TopLeftSection->isVisible())
    {
        page2TopLeftSection->repaint();
    }

    // Force panel repaint continuously only when not sliding and a slider is hovered/dragged
    if (!isSliding)
    {
        bool anySliderActive = false;
        for (juce::Slider* slider : std::initializer_list<juce::Slider*>{ &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &resonanceSlider,
                                      static_cast<juce::Slider*>(&cutoffSlider), &decimatorSlider, &reverbSizeSlider, &reverbMixSlider, &detuneSlider,
                                      &envLoopMsSlider, &envLoopBeatsSlider, &glideSlider, &chorusSlider, &delaySlider, &panSlider,
                                      &filterAttackSlider, &filterDecaySlider, &filterSustainSlider, &filterEnvAmountSlider, &lfoRateSlider, &lfoBeatsSlider })
        {
            if (slider->isVisible() && (slider->isMouseOverOrDragging() || slider->hasKeyboardFocus(true)))
            {
                anySliderActive = true;
                break;
            }
        }
        
        if (anySliderActive || wasAnySliderActive)
            repaint();

        wasAnySliderActive = anySliderActive;
    }
}

void UtilityPanel::setupSlider(juce::Slider& slider, const juce::String&)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f,
                               true);
}

juce::String UtilityPanel::getValueText(juce::Slider& slider) const
{
    return slider.getTextFromValue(slider.getValue());
}

void UtilityPanel::drawActiveValue(juce::Graphics& g, juce::Slider& slider) const
{
    const bool isCutoff = (&slider == static_cast<const juce::Slider*>(&cutoffSlider));
    bool showDisplay = (slider.isMouseOverOrDragging() || slider.hasKeyboardFocus(true) ||
                        slider.getProperties().getWithDefault("isDraggingMapping", false));
    
    if (isCutoff && filterTypeChangeTimer > 0 && slider.isMouseOverOrDragging())
        showDisplay = true;
    else if (isCutoff && !slider.isMouseOverOrDragging())
        showDisplay = false;

    if (!showDisplay || !slider.isVisible())
        return;

    auto bounds = slider.getBounds().translated(slider.getParentComponent()->getX(),
                                                slider.getParentComponent()->getY()).toFloat();
    
    // Position display exactly over the knob
    auto bubbleTop = bounds.withY(bounds.getY() - 25.0f).withHeight(22.0f).withWidth(44.0f);
    bubbleTop.setX(bounds.getCentreX() - bubbleTop.getWidth() * 0.5f);

    auto drawBubble = [&](const juce::Rectangle<float>& baseBubble, const juce::String& text, float fontPt = 16.5f)
    {
        juce::Font font = WXFonts::get(fontPt, juce::Font::bold);
        float textW = (float) font.getStringWidth(text);
        float bubbleW = std::max(36.0f, textW + 10.0f);
        auto bubble = baseBubble.withWidth(bubbleW);
        bubble.setX(bounds.getCentreX() - bubbleW * 0.5f);

        // Draw display container (dark cockpit bezel)
        g.setColour(juce::Colour(0xFF0F0F0F));
        g.fillRoundedRectangle(bubble, 2.5f);
        g.setColour(juce::Colour(0xFF333333));
        g.drawRoundedRectangle(bubble, 2.5f, 1.0f);

        // Draw background/inactive segment templates
        g.setFont(font);
        g.setColour(juce::Colour(0x0EFF9100)); // Ultra dim amber
        juce::String placeholder = juce::String::repeatedString("8", juce::jmax(3, text.length()));
        g.drawText(placeholder, bubble.toNearestInt(), juce::Justification::centred);

        // Draw soft amber glow
        g.setColour(juce::Colour(0x22FF9100));
        for (float offset : { -1.5f, -0.7f, 0.7f, 1.5f })
        {
            g.drawText(text, bubble.translated(offset, 0.0f).toNearestInt(), juce::Justification::centred);
            g.drawText(text, bubble.translated(0.0f, offset).toNearestInt(), juce::Justification::centred);
        }

        // Draw active segment text (sharp, bright amber, effortless to read)
        g.setColour(juce::Colour(0xFFFF9E1B));
        g.drawText(text, bubble.toNearestInt(), juce::Justification::centred);
    };

    if (isMappingMode && slider.getProperties().getWithDefault("isMappingFocus", false))
    {
        float amt = (float) slider.getProperties().getWithDefault("lfoAmount", 0.0);
        int displayValue = (int) std::round(amt * 100.0);
        juce::String valStr = "M" + juce::String(displayValue).paddedLeft('0', 2);
        
        juce::Font mFont = WXFonts::get(15.5f, juce::Font::bold);
        float bubbleW = std::max(46.0f, (float) mFont.getStringWidth(valStr) + 12.0f);
        auto bubble = bubbleTop.withWidth(bubbleW);
        bubble.setX(bounds.getCentreX() - bubbleW * 0.5f);

        // Draw sharp rectangular container in Radar Green
        g.setColour(juce::Colour(0xFF0A150D));
        g.fillRect(bubble);
        g.setColour(juce::Colour(0xFF00E600));
        g.drawRect(bubble, 1.0f);
        
        g.setFont(mFont);
        g.setColour(juce::Colour(0xFF00E600));
        g.drawText(valStr, bubble.toNearestInt(), juce::Justification::centred);
        return;
    }

    if (&slider == &lfoRateSlider)
    {
        float rateVal = (float) slider.getValue();
        juce::String valStr;
        if (rateVal >= 10.0f)
            valStr = juce::String((int) std::round(rateVal)) + "Hz";
        else if (rateVal >= 1.0f)
            valStr = juce::String(rateVal, 1) + "Hz";
        else
            valStr = juce::String(rateVal, 2) + "Hz";

        drawBubble(bubbleTop, valStr, (valStr.length() > 4) ? 14.0f : 15.5f);
        return;
    }

    if (&slider == &lfoBeatsSlider)
    {
        static const char* labels[] = { "1/32", "1/16", "1/8", "1/4", "1/2", " 1B", " 2B", " 4B" };
        int idx = juce::jlimit(0, 7, (int) std::round(slider.getValue()));
        drawBubble(bubbleTop, labels[idx], 15.5f);
        return;
    }

    if (isCutoff)
    {
        // 1. Draw top bubble showing value 0-100
        auto normRange = slider.getNormalisableRange();
        int displayValue = (int) std::round(normRange.convertTo0to1(slider.getValue()) * 100.0);
        juce::String valStr = juce::String(displayValue).paddedLeft('0', 3);
        drawBubble(bubbleTop, valStr, 16.5f);

        // 2. Draw bottom bubble showing filter type LP/HP/BP
        auto bubbleBottom = bounds.withY(bounds.getBottom() + 4.0f).withHeight(22.0f).withWidth(42.0f);
        bubbleBottom.setX(bounds.getCentreX() - bubbleBottom.getWidth() * 0.5f);
        
        int activeFilterType = 0;
        if (auto* param = apvts.getParameter("FILTER_TYPE"))
            activeFilterType = juce::jlimit(0, 2, (int) std::round(param->getValue() * 2.0f));
        
        juce::String labels[] = { " LP", " HP", " BP" };
        drawBubble(bubbleBottom, labels[activeFilterType], 15.0f);
    }
    else
    {
        // Draw standard top bubble only
        juce::String valStr;
        if (&slider == &envLoopBeatsSlider)
        {
            int stepIdx = juce::jlimit(0, 8, (int) std::round(slider.getValue()));
            static const char* BeatLabels[] = {
                "1/16", "1/16.", "1/8", "1/8.", "1/4", "1/4.", "1/2", "1/2.", "1/1"
            };
            valStr = BeatLabels[stepIdx];
            drawBubble(bubbleTop, valStr, 15.0f);
        }
        else
        {
            auto normRange = slider.getNormalisableRange();
            int displayValue = (int) std::round(normRange.convertTo0to1(slider.getValue()) * 100.0);
            valStr = juce::String(displayValue).paddedLeft('0', 3);
            drawBubble(bubbleTop, valStr, 16.5f);
        }
    }
}

void UtilityPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(panelBg));

    // Arka plana çok hafif bir noise/grain texture ekle
    drawGrainTexture(g, getLocalBounds());

    // Separator grid lines (crosshair lines)
    g.setColour(juce::Colour(0x188FA0B5)); // Soft slate grid divider
    g.drawVerticalLine(234, 8.0f, 422.0f);
    g.drawHorizontalLine(218, 18.0f, 448.0f);

    // --- OSC MODE selector bar (ZERO BORDER, ZERO BORDER RADIUS) ---
    auto barRect = juce::Rectangle<float>(18.0f, 434.0f, 430.0f, 25.0f);
    
    // Flat matte dark track - sharp rectangle, ZERO border, ZERO border radius
    g.setColour(juce::Colour(0xFF13161C));
    g.fillRect(barRect);

    // Option columns width
    const float optW = barRect.getWidth() / 4.0f;
    juce::StringArray options { "saw", "sine", "random", "pulse" };
    int activeIdx = 2; // Default to random
    if (auto* param = apvts.getParameter("OSC_MODE"))
        activeIdx = juce::jlimit(0, 3, (int) std::round(param->getValue() * 3.0f));

    for (int i = 0; i < 4; ++i)
    {
        auto cellRect = juce::Rectangle<float>(barRect.getX() + i * optW, barRect.getY(), optW, barRect.getHeight());
        const bool isActive = (i == activeIdx);
        
        if (isActive)
        {
            // Active choice: sharp flat rectangle, ZERO border, ZERO border radius
            g.setColour(juce::Colour(0xFF222834));
            g.fillRect(cellRect);
        }

        // Clean typography: crisp bone white when active, muted slate when inactive
        g.setColour(isActive ? juce::Colour(0xFFEAE6DF) : juce::Colour(0xFF555F70));
        g.setFont(getCockpitFont(14.0f, juce::Font::bold));
        g.drawText(options[i].toUpperCase(), cellRect.toNearestInt(), juce::Justification::centred);
    }
}

void UtilityPanel::paintOverChildren(juce::Graphics& g)
{
    if (drawerProgress < 0.5f)
    {
        for (juce::Slider* slider : std::initializer_list<juce::Slider*>{ &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &resonanceSlider,
                                      static_cast<juce::Slider*>(&cutoffSlider), &decimatorSlider, &reverbSizeSlider, &reverbMixSlider, &detuneSlider,
                                      &envLoopMsSlider, &envLoopBeatsSlider, &glideSlider, &chorusSlider, &delaySlider, &panSlider })
            drawActiveValue(g, *slider);
    }
    else
    {
        for (juce::Slider* slider : std::initializer_list<juce::Slider*>{ &lfoRateSlider, &lfoBeatsSlider, &filterAttackSlider, &filterDecaySlider, &filterSustainSlider, &filterEnvAmountSlider })
            drawActiveValue(g, *slider);
    }
}

void UtilityPanel::setDrawerOpen(bool open)
{
    targetDrawerProgress = open ? 1.0f : 0.0f;
    if (open)
    {
        if (filterEnvelopeSection) filterEnvelopeSection->setVisible(true);
        if (page2TopLeftSection) page2TopLeftSection->setVisible(true);
        if (leftDimmerOverlay) leftDimmerOverlay->setVisible(true);
    }
}

void UtilityPanel::toggleDrawer()
{
    setDrawerOpen(!isDrawerOpen());
}

void UtilityPanel::updateDrawerLayout()
{
    // Fixed base sections
    scanFilterSection->setBounds(18, 12, 206, 202);
    motionSection->setBounds(18, 222, 206, 204);
    ampEnvelopeSection->setBounds(244, 12, 204, 202);
    spaceFxSection->setBounds(244, 222, 204, 204);

    // Dimmer overlay covers the entire base layer (left cards, central gap, and right underlying controls)
    leftDimmerOverlay->setBounds(0, 0, 466, 434);

    // Drawer sliding positions:
    // When drawerProgress = 0.0f -> drawerX = 466 (off-screen right)
    // When drawerProgress = 1.0f -> drawerX = 244 (flush over ampEnv and spaceFx)
    float drawerX = 244.0f + (1.0f - drawerProgress) * 222.0f;
    if (filterEnvelopeSection)
        filterEnvelopeSection->setBounds((int)drawerX, 12, 204, 202);
    if (page2TopLeftSection)
        page2TopLeftSection->setBounds((int)drawerX, 222, 204, 204);

    // Notch button tab sticks out from the left edge of the sliding drawer (minimal sleek pull-tab)
    const int notchW = 12;
    const int notchH = 52;
    int notchX = juce::jlimit(232, 454, (int)drawerX - notchW);
    if (notchButton)
        notchButton->setBounds(notchX, 192, notchW, notchH);

    bool isDrawerActive = (drawerProgress > 0.001f);
    if (filterEnvelopeSection) filterEnvelopeSection->setVisible(isDrawerActive);
    if (page2TopLeftSection) page2TopLeftSection->setVisible(isDrawerActive);
    if (leftDimmerOverlay) leftDimmerOverlay->setVisible(isDrawerActive);
    if (notchButton) notchButton->setVisible(true);
}

void UtilityPanel::resized()
{
    updateDrawerLayout();
}

void UtilityPanel::mouseDown(const juce::MouseEvent& event)
{
    auto barRect = juce::Rectangle<float>(18.0f, 434.0f, 430.0f, 25.0f);
    if (barRect.contains(event.position))
    {
        float localX = event.position.x - 18.0f;
        int clickedOption = juce::jlimit(0, 3, (int)(localX / 107.5f));
        if (auto* param = apvts.getParameter("OSC_MODE"))
        {
            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
                *choiceParam = clickedOption;
            else
                param->setValueNotifyingHost(param->convertTo0to1((float)clickedOption));
        }
        
        // Randomize seed whenever clicking the RANDOM waveform option to generate a brand new waveshape!
        if (clickedOption == 2)
        {
            if (auto* seedParam = apvts.getParameter("RANDOM_SEED"))
            {
                juce::Random r (juce::Time::getMillisecondCounter());
                float newSeed = r.nextFloat();
                processor.prepareWavetablesForSeed(newSeed);
                seedParam->setValueNotifyingHost(newSeed);
            }
        }
        repaint();
    }
}

void UtilityPanel::triggerRandomizationAnimation()
{
    for (juce::Slider* slider : std::initializer_list<juce::Slider*>{ &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &resonanceSlider,
                                  static_cast<juce::Slider*>(&cutoffSlider), &decimatorSlider, &reverbSizeSlider, &reverbMixSlider, &detuneSlider,
                                  &envLoopMsSlider, &envLoopBeatsSlider, &glideSlider, &chorusSlider, &delaySlider, &panSlider,
                                  &filterAttackSlider, &filterDecaySlider, &filterSustainSlider, &filterEnvAmountSlider })
    {
        if (slider->isShowing())
        {
            slider->getProperties().set("animStartVal", slider->getValue());
            slider->getProperties().set("animActive", true);
            slider->getProperties().remove("visualVal");
        }
        else
        {
            slider->getProperties().remove("animStartVal");
            slider->getProperties().remove("animActive");
            slider->getProperties().remove("visualVal");
        }
    }
}

void UtilityPanel::startMappingMode()
{
    isMappingMode = true;
    for (juce::Slider* s : std::initializer_list<juce::Slider*>{ &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &resonanceSlider,
                             static_cast<juce::Slider*>(&cutoffSlider), &decimatorSlider, &reverbSizeSlider,
                             &reverbMixSlider, &detuneSlider, &glideSlider, &chorusSlider, &delaySlider, &panSlider,
                             &filterAttackSlider, &filterDecaySlider, &filterSustainSlider, &filterEnvAmountSlider })
    {
        s->getProperties().set("isMappingFocus", true);
        s->repaint();
    }
    setDrawerOpen(false);
    repaint();
}

void UtilityPanel::stopMappingMode()
{
    if (!isMappingMode) return;
    isMappingMode = false;
    for (juce::Slider* s : std::initializer_list<juce::Slider*>{ &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &resonanceSlider,
                             static_cast<juce::Slider*>(&cutoffSlider), &decimatorSlider, &reverbSizeSlider,
                             &reverbMixSlider, &detuneSlider, &glideSlider, &chorusSlider, &delaySlider, &panSlider,
                             &filterAttackSlider, &filterDecaySlider, &filterSustainSlider, &filterEnvAmountSlider })
    {
        s->getProperties().remove("isMappingFocus");
        s->repaint();
    }
    repaint();
}

void UtilityPanel::toggleMappingMode()
{
    if (isMappingMode)
        stopMappingMode();
    else
        startMappingMode();
}
