#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <array>
#include <vector>

class CrystalAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    explicit CrystalAudioProcessorEditor (CrystalAudioProcessor&);
    ~CrystalAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    class MinimalLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        juce::Colour resizerInk { 0xff1f1613 };
        void drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging) override
        {
            const float a = (isMouseOver || isMouseDragging) ? 0.72f : 0.28f;
            g.setColour (resizerInk.withMultipliedAlpha (a));
            g.drawLine (static_cast<float> (w) - 3.5f, static_cast<float> (h) - 9.5f,
                        static_cast<float> (w) - 9.5f, static_cast<float> (h) - 3.5f, 1.15f);
            g.drawLine (static_cast<float> (w) - 3.5f, static_cast<float> (h) - 5.2f,
                        static_cast<float> (w) - 5.2f, static_cast<float> (h) - 3.5f, 1.15f);
        }
    };

    struct Vec3 { float x = 0, y = 0, z = 0; };
    struct Quat { float x = 0, y = 0, z = 0, w = 1; };
    struct PolyMesh
    {
        std::vector<Vec3> verts;
        std::vector<std::array<int, 3>> faces;
    };

    void timerCallback() override;
    void buildMeshes();
    void buildPaperTexture();
    void buildLogoMasks();
    juce::Font makeUiFont (float height, bool semiBold = false) const;

    // Math helpers
    static Vec3 vecNorm (Vec3 v) noexcept;
    static Vec3 vecSub (Vec3 a, Vec3 b) noexcept;
    static float vecDot (Vec3 a, Vec3 b) noexcept;
    static Vec3 vecCross (Vec3 a, Vec3 b) noexcept;
    static Quat qFromAxisAngle (float ax, float ay, float az, float angle) noexcept;
    static Quat qMul (Quat a, Quat b) noexcept;
    static Quat qNorm (Quat q) noexcept;
    static Quat qConj (Quat q) noexcept;
    static Vec3 qRotateVec (Quat q, Vec3 v) noexcept;
    static float hash1 (float n) noexcept;
    static float smoothNoise (float t, float seed) noexcept;
    static float loopNoise (float angle, float freq, float seed) noexcept;
    static float organicScale3D (float x, float y, float z, float amt, float slowT) noexcept;
    static float superellipseRadius (float angle, float halfSize, float p) noexcept;

    // Felt-tip marker primitives
    void drawMarkerRibbon (juce::Graphics& g, const std::vector<juce::Point<float>>& pts,
                           const std::vector<float>& widths, bool closed,
                           juce::Colour color, float alpha = 1.0f) const;
    void drawInkStroke (juce::Graphics& g, const std::vector<juce::Point<float>>& pts,
                        float baseW, juce::Colour color, float alpha, bool closed,
                        float seed, float jitterPx = 0.45f) const;
    void drawRuneGlyph (juce::Graphics& g, int idx, float cx, float cy, float r,
                        juce::Colour color, float alpha = 0.92f) const;
    void draw5RuneCode (juce::Graphics& g, int seed, float cx, float cy, float glyphR,
                        float stepX, juce::Colour color, float alpha = 0.92f) const;
    void drawHandDrawnIcon (juce::Graphics& g, int type, float cx, float cy,
                            const CrystalDSP::Palette& pal, float seed) const;

    // Section renderers
    void renderParallelHatch (juce::Graphics& g, const std::vector<float>& orgScales,
                              const CrystalDSP::Palette& pal, float ballCy, float hatchAlpha,
                              CrystalDSP::HatchStyle style, float slowT, int polyOverride, float orgOverride) const;
    void renderMainShape (juce::Graphics& g, const CrystalDSP::Palette& pal, float slowT,
                          float ballCy, float scaleMult, int polyOverride, float orgOverride) const;
    void renderDistortionScope (juce::Graphics& g, const CrystalDSP::Palette& pal,
                                float yOffset, float alpha) const;
    void renderOrganicKnobs (juce::Graphics& g, const CrystalDSP::Palette& pal,
                             const float yOffsets[3], const float alphas[3],
                             const float sx[3], const float sy[3]) const;
    void renderTopSeedBar (juce::Graphics& g, const CrystalDSP::Palette& pal,
                           float yOffset, float alpha) const;
    void renderBrainLogo (juce::Graphics& g, float cx, float cy, float size,
                          juce::Colour color, float alpha, bool interactive) const;
    void renderPresetExplorer (juce::Graphics& g, const CrystalDSP::Palette& pal) const;
    void renderInfoDrawer (juce::Graphics& g, const CrystalDSP::Palette& pal) const;
    void renderBootLicenseGate (juce::Graphics& g, const CrystalDSP::Palette& pal) const;

    void updateLicenseInputLayout();
    void submitLicenseKey();
    void triggerKnobSnapAnimation (bool fromZero = false);
    float getMaxExplorerScroll() const noexcept;

    // Hit testing & coordinate scaling (Ableton Auto-Scale OFF / HiDPI guvenli!)
    float getUiScale() const noexcept
    {
        const float sx = static_cast<float> (getWidth())  / static_cast<float> (kW);
        const float sy = static_cast<float> (getHeight()) / static_cast<float> (kH);
        return std::max (0.1f, std::min (sx, sy));
    }
    juce::Point<float> getUiOffset() const noexcept
    {
        const float s = getUiScale();
        return { (static_cast<float> (getWidth())  - static_cast<float> (kW) * s) * 0.5f,
                 (static_cast<float> (getHeight()) - static_cast<float> (kH) * s) * 0.5f };
    }
    juce::Point<float> toLogical (juce::Point<float> pt) const noexcept
    {
        const float s = getUiScale();
        const auto off = getUiOffset();
        return { (pt.x - off.x) / s, (pt.y - off.y) / s };
    }
    int hitKnob (juce::Point<float> pt) const noexcept;
    bool hitBallZone (juce::Point<float> pt) const noexcept;
    juce::String hitTopOrDrawer (juce::Point<float> pt) const;
    void setHostParam (const char* id, float val);
    void requestThrottledRepaint();

    CrystalAudioProcessor& processor;

    static constexpr int kW = 340;
    static constexpr int kH = 384;
    static constexpr float kBallCx = 170.0f;
    static constexpr float kBallCy = 135.0f;
    static constexpr float kBallR  = 45.0f;
    static constexpr float kPenUnit = 3.6f;

    PolyMesh mesh32, mesh20, mesh8;
    juce::Image paperTile;
    juce::Image logoLobes[4];
    juce::Typeface::Ptr quicksandMed;
    juce::Typeface::Ptr quicksandSemi;

    Quat q;
    float yawVel = 0.26f, pitchVel = 0.0f;
    float dragYawVel = 0.0f, dragPitchVel = 0.0f;
    juce::int64 lastDragMs = 0;
    float timeSec = 0.0f, barberScroll = 0.0f, snapPop = 0.0f;
    float accumAngle = 0.0f;
    int sketchSeed = 1, hatchSeed = 1;
    float knobSeeds[3] { 11.0f, 23.0f, 37.0f };
    int lastPolyVis = 1;
    int boilTick = 0, hatchTick = 0;
    juce::int64 lastRepaintMs = 0;

    bool holdingBall = false;
    int activeKnob = -1, hoverKnob = -1;
    juce::String hoverUI;
    bool explorerOpen = false;
    float explorerScrollY = 0.0f;
    bool infoOpen = false;
    bool versionChecked = false;
    juce::Point<float> lastMouse;
    juce::Point<float> mouseLogical { -100.0f, -100.0f };
    float lobeHover[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    float knobStartY = 0.0f, knobStartVal = 0.0f;

    // Minimalist resizer LNF & license input
    MinimalLookAndFeel minimalLnf;
    juce::TextEditor licenseInput;
    float licenseErrorTimer = 0.0f;
    float licenseGateAlpha = 0.0f;
    float gateTime = 0.0f;
    int gateShuffleStage = 0;

    // Heavy punchy knob snap state (no squash/stretch on randomize!)
    float knobDispVal[3] { 0.55f, 0.45f, 0.40f };
    int knobStepDelay[3] { 0, 0, 0 };

    // Session-once Disney-principled boot animation state (~3.85s)
    // Waits until editor is actually shown on screen (Windows + macOS safe, fail-safe guarded)
    bool bootWaitingForShow = false;
    bool hasPaintedOnce = false;
    float bootWaitGuardSec = 0.0f;
    bool bootActive = false;
    float bootTime = 0.0f;
    int bootShuffleStage = 0;
    int bootLegoClickStage = 0;
    int bootVisualSeed = 0;
    int bootVisualPoly = 0;
    float bootVisualOrg = 0.0f;
    juce::Random bootRng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrystalAudioProcessorEditor)
};
