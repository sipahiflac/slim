#include "PluginEditor.h"
#include "BrainLogoData.h"
#include "QuicksandFontData.h"
#include <cmath>
#include <cstring>
#include <map>
#include <algorithm>
#include <thread>

namespace
{
    static constexpr float kPiF    = 3.14159265358979323846f;
    static constexpr float kTwoPiF = 6.28318530717958647692f;

    static constexpr float kSeedCx = 170.0f, kSeedCy = 26.0f, kSeedW = 122.0f;
    static constexpr float kLogoCx = 28.0f,  kLogoCy = 26.0f, kLogoSize = 24.0f;
    static constexpr float kSaveX  = 254.0f, kSaveY  = 26.0f, kBtnR  = 12.0f;

    static constexpr float kWaveX  = 116.0f, kWaveCy = 242.0f, kWaveW = 108.0f, kWaveHalfH = 13.0f;
    static constexpr float kKnobX[3] = { 74.0f, 170.0f, 266.0f };
    static constexpr float kKnobY    = 318.0f;
    static constexpr float kKnobR    = 22.0f;

    static constexpr float kDrawerX = 48.0f, kDrawerY = 48.0f, kDrawerW = 244.0f, kDrawerH = 296.0f, kRowH = 34.0f;
    // Sol ustte acilan kucuk, hyperminimalist versiyon & lisans modulu
    static constexpr float kInfoX = 16.0f, kInfoY = 46.0f, kInfoW = 176.0f, kInfoH = 118.0f;

    inline float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }
    inline float easeOutCubic (float t) noexcept { const float u = 1.0f - clamp01 (t); return 1.0f - u * u * u; }
    inline float easeInOutCubic (float t) noexcept
    {
        t = clamp01 (t);
        return t < 0.5f ? (4.0f * t * t * t) : (1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f);
    }
    inline float easeOutBack (float t, float overshoot = 1.70158f) noexcept
    {
        t = clamp01 (t);
        const float c3 = overshoot + 1.0f;
        const float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + overshoot * u * u;
    }
}

CrystalAudioProcessorEditor::CrystalAudioProcessorEditor (CrystalAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&minimalLnf);
    // JUCE VST3 / Ableton Auto-Scale kurali: once resizable & limits & aspect ratio, EN SON setSize!
    setResizable (true, true);
    setResizeLimits (238, 269, 1360, 1536);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kW) / static_cast<double> (kH));

    const int initW = std::clamp (processor.lastEditorW, 238, 1360);
    const int initH = juce::roundToInt (static_cast<double> (initW) * static_cast<double> (kH) / static_cast<double> (kW));
    setSize (initW, std::clamp (initH, 269, 1536));

    quicksandMed  = juce::Typeface::createSystemTypefaceFor (CrystalFonts::kQuicksandMediumData,
                                                             static_cast<size_t> (CrystalFonts::kQuicksandMediumSize));
    quicksandSemi = juce::Typeface::createSystemTypefaceFor (CrystalFonts::kQuicksandSemiBoldData,
                                                             static_cast<size_t> (CrystalFonts::kQuicksandSemiBoldSize));

    // Hyperminimalist borderless Quicksand license input (tam dikey & yatay ortali)
    licenseInput.setMultiLine (false);
    licenseInput.setReturnKeyStartsNewLine (false);
    licenseInput.setScrollbarsShown (false);
    licenseInput.setBorder (juce::BorderSize<int> (0));
    licenseInput.setIndents (0, 0);
    licenseInput.setJustification (juce::Justification::centred);
    licenseInput.setInputRestrictions (24, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789- ");
    licenseInput.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    licenseInput.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    licenseInput.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    licenseInput.onReturnKey = [this] () { submitLicenseKey(); };
    addChildComponent (licenseInput);

    q = qNorm (qMul (qFromAxisAngle (1.0f, 0.0f, 0.0f, 0.22f),
                     qFromAxisAngle (0.0f, 1.0f, 0.0f, -0.32f)));
    lastPolyVis = std::min (3, static_cast<int> ((*processor.apvts.getRawParameterValue ("knob1")) * 3.999f));
    knobDispVal[0] = *processor.apvts.getRawParameterValue ("knob0");
    knobDispVal[1] = *processor.apvts.getRawParameterValue ("knob1");
    knobDispVal[2] = *processor.apvts.getRawParameterValue ("knob2");

    if (processor.isLicensed() && ! processor.hasPlayedBootAnim)
    {
        processor.hasPlayedBootAnim = true;
        bootWaitingForShow = true;
        hasPaintedOnce = false;
        bootWaitGuardSec = 0.0f;
        bootActive = true;
        bootTime = 0.0f;
        bootShuffleStage = 0;
        bootLegoClickStage = 0;
        bootVisualSeed = processor.getCurrentSeed();
        bootVisualPoly = 0;
        bootVisualOrg = 0.0f;
        knobDispVal[0] = knobDispVal[1] = knobDispVal[2] = 0.0f;
    }
    else if (! processor.isLicensed())
    {
        bootWaitingForShow = true;
        hasPaintedOnce = false;
        bootWaitGuardSec = 0.0f;
        gateTime = 0.0f;
        gateShuffleStage = 0;
        licenseGateAlpha = 0.0f;
        bootVisualSeed = processor.getCurrentSeed();
        bootVisualPoly = 0;
        bootVisualOrg = 0.10f;
    }

    buildMeshes();
    buildPaperTexture();
    buildLogoMasks();
    updateLicenseInputLayout();
    startTimerHz (13); // 13 FPS kilitli stop-motion akisi
}

CrystalAudioProcessorEditor::~CrystalAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void CrystalAudioProcessorEditor::resized()
{
    // Sadece pencere ekrana oturup acilis DPI/host el sıkısmasi bittikten sonra boyutu kaydet!
    // Boylece Ableton ac/kapa sirasindaki gecici onSize/scaleFactor kucultmeleri boyutu asla bozmaz.
    if (openStableTicks >= 5 && isShowing() && hasPaintedOnce
        && getWidth() >= 238 && getHeight() >= 269)
    {
        if (std::abs (getWidth() - processor.lastEditorW) >= 4)
            processor.saveEditorSizePreference (getWidth(), getHeight());
    }
    updateLicenseInputLayout();
    repaint();
}

juce::Font CrystalAudioProcessorEditor::makeUiFont (float height, bool semiBold) const
{
    auto tf = semiBold ? quicksandSemi : quicksandMed;
    if (tf != nullptr)
        return juce::Font (juce::FontOptions (tf).withHeight (height));
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height, semiBold ? juce::Font::bold : juce::Font::plain));
}

void CrystalAudioProcessorEditor::updateLicenseInputLayout()
{
    const bool show = (! processor.isLicensed()) && (licenseGateAlpha > 0.15f);
    licenseInput.setVisible (show);
    if (! show) return;

    const float s = getUiScale();
    const auto off = getUiOffset();
    const auto pal = CrystalDSP::makeProceduralPalette (bootVisualSeed);
    licenseInput.setColour (juce::TextEditor::textColourId, pal.ink.withMultipliedAlpha (0.92f));
    licenseInput.setColour (juce::CaretComponent::caretColourId, pal.contrast);
    licenseInput.setTextToShowWhenEmpty ("WLDN-XXXX-XXXX-XXXX", pal.ink.withMultipliedAlpha (0.34f));
    licenseInput.setFont (makeUiFont (13.0f * s, true));
    licenseInput.applyFontToAllText (makeUiFont (13.0f * s, true));
    licenseInput.setBorder (juce::BorderSize<int> (0));
    licenseInput.setIndents (0, 0);

    const float slideY = (1.0f - easeOutCubic (licenseGateAlpha)) * 10.0f;
    const float shake = (licenseErrorTimer > 0.0f) ? std::sin (licenseErrorTimer * 52.0f) * 4.5f * (licenseErrorTimer / 0.45f) : 0.0f;
    const float lx = 74.0f + shake;
    const float ly = 242.0f + slideY;
    const float lw = 192.0f;
    const float lh = 32.0f;
    licenseInput.setBounds (juce::roundToInt (off.x + lx * s), juce::roundToInt (off.y + ly * s),
                            juce::roundToInt (lw * s), juce::roundToInt (lh * s));
}

void CrystalAudioProcessorEditor::submitLicenseKey()
{
    if (processor.tryActivateLicense (licenseInput.getText()))
    {
        licenseInput.clear();
        licenseErrorTimer = 0.0f;
        infoOpen = false;
        // Basarili aktivasyonda direkt Lego-snap acilis animasyonuna gec!
        processor.hasPlayedBootAnim = true;
        bootActive = true;
        bootTime = 2.15f;
        bootShuffleStage = 4;
        bootLegoClickStage = 0;
        bootVisualSeed = processor.getCurrentSeed();
        snapPop = 1.0f;
        yawVel = 2.6f;
        updateLicenseInputLayout();
        repaint();
    }
    else
    {
        licenseErrorTimer = 0.45f;
        processor.triggerMechanicalClick (-1);
        repaint();
    }
}

void CrystalAudioProcessorEditor::startVersionCheck()
{
    if (updateState == UpdateState::Checking) return;
    updateState = UpdateState::Checking;
    repaint();

    juce::Component::SafePointer<CrystalAudioProcessorEditor> safeThis (this);
    const juce::String curVer (CrystalAudioProcessor::kPluginVersion);
    std::thread ([safeThis, curVer] ()
    {
        const auto res = CrystalLicense::checkForUpdateSync (curVer);
        juce::MessageManager::callAsync ([safeThis, res] ()
        {
            if (safeThis == nullptr) return;
            if (! res.ok)
            {
                safeThis->updateState = UpdateState::Error;
            }
            else if (res.newer)
            {
                safeThis->updateState = UpdateState::UpdateAvailable;
                safeThis->latestVersionStr = res.latest;
                safeThis->updateDownloadUrl = res.downloadUrl;
            }
            else
            {
                safeThis->updateState = UpdateState::UpToDate;
                safeThis->latestVersionStr = res.latest;
                safeThis->updateDownloadUrl.clear();
            }
            safeThis->repaint();
        });
    }).detach();
}

void CrystalAudioProcessorEditor::triggerKnobSnapAnimation (bool fromZero)
{
    for (int i = 0; i < 3; ++i)
    {
        if (fromZero) knobDispVal[i] = 0.0f;
        // 1'er kare arayla agir, keskin mekanik tik sirasi!
        knobStepDelay[i] = i;
    }
}

// =============================================================================
// MATH HELPERS
// =============================================================================
CrystalAudioProcessorEditor::Vec3 CrystalAudioProcessorEditor::vecNorm (Vec3 v) noexcept
{
    const float l = std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z);
    if (l < 1e-6f) return { 0.0f, 0.0f, 1.0f };
    return { v.x / l, v.y / l, v.z / l };
}

CrystalAudioProcessorEditor::Vec3 CrystalAudioProcessorEditor::vecSub (Vec3 a, Vec3 b) noexcept
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

float CrystalAudioProcessorEditor::vecDot (Vec3 a, Vec3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

CrystalAudioProcessorEditor::Vec3 CrystalAudioProcessorEditor::vecCross (Vec3 a, Vec3 b) noexcept
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

CrystalAudioProcessorEditor::Quat CrystalAudioProcessorEditor::qFromAxisAngle (float ax, float ay, float az, float angle) noexcept
{
    const float h = angle * 0.5f, s = std::sin (h);
    return { ax * s, ay * s, az * s, std::cos (h) };
}

CrystalAudioProcessorEditor::Quat CrystalAudioProcessorEditor::qMul (Quat a, Quat b) noexcept
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

CrystalAudioProcessorEditor::Quat CrystalAudioProcessorEditor::qNorm (Quat qv) noexcept
{
    const float l = std::sqrt (qv.x * qv.x + qv.y * qv.y + qv.z * qv.z + qv.w * qv.w);
    if (l < 1e-6f) return { 0, 0, 0, 1 };
    return { qv.x / l, qv.y / l, qv.z / l, qv.w / l };
}

CrystalAudioProcessorEditor::Quat CrystalAudioProcessorEditor::qConj (Quat qv) noexcept
{
    return { -qv.x, -qv.y, -qv.z, qv.w };
}

CrystalAudioProcessorEditor::Vec3 CrystalAudioProcessorEditor::qRotateVec (Quat qv, Vec3 v) noexcept
{
    const float tx = 2.0f * (qv.y * v.z - qv.z * v.y);
    const float ty = 2.0f * (qv.z * v.x - qv.x * v.z);
    const float tz = 2.0f * (qv.x * v.y - qv.y * v.x);
    return {
        v.x + qv.w * tx + (qv.y * tz - qv.z * ty),
        v.y + qv.w * ty + (qv.z * tx - qv.x * tz),
        v.z + qv.w * tz + (qv.x * ty - qv.y * tx)
    };
}

float CrystalAudioProcessorEditor::hash1 (float n) noexcept
{
    const float x = std::sin (n * 127.1f + 311.7f) * 43758.5453123f;
    return x - std::floor (x);
}

float CrystalAudioProcessorEditor::smoothNoise (float t, float seed) noexcept
{
    const float i = std::floor (t), f = t - i, u = f * f * (3.0f - 2.0f * f);
    const float a = hash1 (i * 1.13f + seed * 19.37f) * 2.0f - 1.0f;
    const float b = hash1 ((i + 1.0f) * 1.13f + seed * 19.37f) * 2.0f - 1.0f;
    return a * (1.0f - u) + b * u;
}

float CrystalAudioProcessorEditor::loopNoise (float angle, float freq, float seed) noexcept
{
    return 0.55f * std::sin (angle * freq + hash1 (seed * 7.13f + freq) * kTwoPiF)
         + 0.30f * std::sin (angle * (freq + 2.0f) + hash1 (seed * 13.71f + freq * 2.1f) * kTwoPiF)
         + 0.15f * std::sin (angle * (freq * 2.0f + 1.0f) + hash1 (seed * 29.17f + freq * 3.7f) * kTwoPiF);
}

float CrystalAudioProcessorEditor::organicScale3D (float x, float y, float z, float amt, float slowT) noexcept
{
    if (amt <= 0.002f) return 1.0f;
    const float l1 = std::sin (x * 2.5f + slowT * 0.9f) * std::cos (y * 2.2f - slowT * 0.6f);
    const float l2 = std::cos (z * 2.6f + x * 1.6f - slowT * 0.8f) * std::sin (y * 2.0f + z * 1.4f);
    const float l3 = std::sin ((x - y + z) * 2.1f + slowT * 0.5f);
    return 1.0f + (l1 * 0.13f + l2 * 0.10f + l3 * 0.065f) * amt;
}

float CrystalAudioProcessorEditor::superellipseRadius (float angle, float halfSize, float p) noexcept
{
    const float ca = std::pow (std::abs (std::cos (angle)), p);
    const float sa = std::pow (std::abs (std::sin (angle)), p);
    return halfSize / std::pow (ca + sa, 1.0f / p);
}

void CrystalAudioProcessorEditor::buildMeshes()
{
    auto buildOcta = [] (int subdiv)
    {
        PolyMesh m;
        m.verts = { {0,1,0}, {0,-1,0}, {0,0,1}, {1,0,0}, {0,0,-1}, {-1,0,0} };
        m.faces = { {0,5,2}, {0,2,3}, {0,3,4}, {0,4,5}, {1,2,5}, {1,3,2}, {1,4,3}, {1,5,4} };
        for (int s = 0; s < subdiv; ++s)
        {
            std::map<std::pair<int, int>, int> cache;
            auto mid = [&] (int a, int b)
            {
                auto key = std::make_pair (std::min (a, b), std::max (a, b));
                auto it = cache.find (key);
                if (it != cache.end()) return it->second;
                const auto& va = m.verts[static_cast<size_t> (a)];
                const auto& vb = m.verts[static_cast<size_t> (b)];
                const int idx = static_cast<int> (m.verts.size());
                m.verts.push_back (vecNorm ({ (va.x + vb.x) * 0.5f, (va.y + vb.y) * 0.5f, (va.z + vb.z) * 0.5f }));
                cache[key] = idx;
                return idx;
            };
            std::vector<std::array<int, 3>> nf;
            for (const auto& f : m.faces)
            {
                const int ab = mid (f[0], f[1]), bc = mid (f[1], f[2]), ca = mid (f[2], f[0]);
                nf.push_back ({ f[0], ab, ca });
                nf.push_back ({ f[1], bc, ab });
                nf.push_back ({ f[2], ca, bc });
                nf.push_back ({ ab, bc, ca });
            }
            m.faces = std::move (nf);
        }
        return m;
    };

    mesh8  = buildOcta (0);
    mesh32 = buildOcta (1);

    const float t = (1.0f + std::sqrt (5.0f)) * 0.5f;
    const Vec3 rawV[12] = {
        {-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},
        {0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},
        {t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}
    };
    for (const auto& v : rawV) mesh20.verts.push_back (vecNorm (v));
    mesh20.faces = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };
}

void CrystalAudioProcessorEditor::buildPaperTexture()
{
    constexpr int sz = 160;
    paperTile = juce::Image (juce::Image::ARGB, sz, sz, true);
    uint32_t s = 1337u;
    auto randf = [&s] ()
    {
        s = (s * 16807u) % 2147483647u;
        return static_cast<float> (s - 1u) / 2147483646.0f;
    };
    for (int y = 0; y < sz; ++y)
    {
        for (int x = 0; x < sz; ++x)
        {
            const float r = randf();
            const uint8_t a = static_cast<uint8_t> (r * 14.0f);
            paperTile.setPixelAt (x, y, juce::Colour::fromRGBA (55, 38, 22, a));
        }
    }
    juce::Graphics g (paperTile);
    g.setColour (juce::Colour::fromRGBA (80, 58, 34, 18));
    for (int i = 0; i < 42; ++i)
    {
        const float x = randf() * sz, y = randf() * sz;
        const float len = 3.0f + randf() * 8.0f, ang = randf() * kTwoPiF;
        g.drawLine (x, y, x + std::cos (ang) * len, y + std::sin (ang) * len, 0.7f);
    }
}

void CrystalAudioProcessorEditor::buildLogoMasks()
{
    constexpr int N = CrystalLogo::kSize;
    for (int lobe = 0; lobe < CrystalLogo::kNumLobes; ++lobe)
    {
        logoLobes[lobe] = juce::Image (juce::Image::SingleChannel, N, N, true, juce::SoftwareImageType());
        juce::Image::BitmapData bd (logoLobes[lobe], juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < N; ++y)
        {
            if (bd.pixelStride == 1)
            {
                std::memcpy (bd.getLinePointer (y), &CrystalLogo::kLobeAlpha[lobe][y * N], static_cast<size_t> (N));
            }
            else
            {
                for (int x = 0; x < N; ++x)
                    bd.setPixelColour (x, y, juce::Colour::fromRGBA (255, 255, 255, CrystalLogo::kLobeAlpha[lobe][y * N + x]));
            }
        }
    }
}

// =============================================================================
// FELT-TIP MARKER PRIMITIVES
// =============================================================================
void CrystalAudioProcessorEditor::drawMarkerRibbon (juce::Graphics& g,
                                                    const std::vector<juce::Point<float>>& pts,
                                                    const std::vector<float>& widths,
                                                    bool closed, juce::Colour color, float alpha) const
{
    const int n = static_cast<int> (pts.size());
    if (n < 2) return;

    std::vector<juce::Point<float>> left (static_cast<size_t> (n)), right (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const auto prev = closed ? pts[static_cast<size_t> ((i - 1 + n) % n)] : pts[static_cast<size_t> (std::max (0, i - 1))];
        const auto next = closed ? pts[static_cast<size_t> ((i + 1) % n)]     : pts[static_cast<size_t> (std::min (n - 1, i + 1))];
        float dx = next.x - prev.x, dy = next.y - prev.y;
        const float len = std::hypot (dx, dy);
        if (len > 1e-5f) { dx /= len; dy /= len; } else { dx = 1.0f; dy = 0.0f; }
        const float nx = -dy, ny = dx, hw = widths[static_cast<size_t> (i)] * 0.5f;
        left[static_cast<size_t> (i)]  = { pts[static_cast<size_t> (i)].x + nx * hw, pts[static_cast<size_t> (i)].y + ny * hw };
        right[static_cast<size_t> (i)] = { pts[static_cast<size_t> (i)].x - nx * hw, pts[static_cast<size_t> (i)].y - ny * hw };
    }

    juce::Path path;
    if (closed)
    {
        path.setUsingNonZeroWinding (false);
        path.startNewSubPath (left[0]);
        for (int i = 1; i < n; ++i) path.lineTo (left[static_cast<size_t> (i)]);
        path.closeSubPath();
        path.startNewSubPath (right[static_cast<size_t> (n - 1)]);
        for (int i = n - 2; i >= 0; --i) path.lineTo (right[static_cast<size_t> (i)]);
        path.closeSubPath();
    }
    else
    {
        path.startNewSubPath (left[0]);
        for (int i = 1; i < n; ++i) path.lineTo (left[static_cast<size_t> (i)]);
        for (int i = n - 1; i >= 0; --i) path.lineTo (right[static_cast<size_t> (i)]);
        path.closeSubPath();
    }

    g.setColour (color.withMultipliedAlpha (alpha));
    g.fillPath (path);

    if (! closed)
    {
        const float r0 = widths[0] * 0.46f, r1 = widths[static_cast<size_t> (n - 1)] * 0.46f;
        g.fillEllipse (pts[0].x - r0, pts[0].y - r0, r0 * 2.0f, r0 * 2.0f);
        g.fillEllipse (pts[static_cast<size_t> (n - 1)].x - r1, pts[static_cast<size_t> (n - 1)].y - r1, r1 * 2.0f, r1 * 2.0f);
    }
}

void CrystalAudioProcessorEditor::drawInkStroke (juce::Graphics& g,
                                                 const std::vector<juce::Point<float>>& pts,
                                                 float baseW, juce::Colour color, float alpha,
                                                 bool closed, float seed, float jitterPx) const
{
    const int n = static_cast<int> (pts.size());
    if (n < 2) return;
    std::vector<juce::Point<float>> jPts (static_cast<size_t> (n));
    std::vector<float> ws (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        jPts[static_cast<size_t> (i)] = {
            pts[static_cast<size_t> (i)].x + smoothNoise (i * 0.6f, seed) * jitterPx,
            pts[static_cast<size_t> (i)].y + smoothNoise (i * 0.6f + 17.0f, seed) * jitterPx
        };
        ws[static_cast<size_t> (i)] = baseW * (0.90f + 0.18f * hash1 (i * 1.3f + seed));
    }
    drawMarkerRibbon (g, jPts, ws, closed, color, alpha);
}

// =============================================================================
// 8-SYMBOL HAND-DRAWN ALIEN RUNE ALPHABET (ZERO LETTERS / ZERO DIGITS!)
// =============================================================================
void CrystalAudioProcessorEditor::drawRuneGlyph (juce::Graphics& g, int idx, float cx, float cy,
                                                 float r, juce::Colour color, float alpha) const
{
    g.setColour (color.withMultipliedAlpha (alpha));
    const float sw = std::max (1.35f, r * 0.28f);
    const juce::PathStrokeType stroke (sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;

    switch (idx & 7)
    {
        case 0: // Open ring + center dot
            p.addEllipse (cx - r * 0.85f, cy - r * 0.85f, r * 1.7f, r * 1.7f);
            g.strokePath (p, stroke);
            g.fillEllipse (cx - r * 0.24f, cy - r * 0.24f, r * 0.48f, r * 0.48f);
            break;
        case 1: // Vertical dagger + crossbar
            p.startNewSubPath (cx, cy - r); p.lineTo (cx, cy + r);
            p.startNewSubPath (cx - r * 0.72f, cy - r * 0.18f); p.lineTo (cx + r * 0.72f, cy - r * 0.18f);
            g.strokePath (p, stroke);
            break;
        case 2: // Hollow chevron + base dot
            p.startNewSubPath (cx - r * 0.85f, cy + r * 0.68f);
            p.lineTo (cx, cy - r * 0.88f);
            p.lineTo (cx + r * 0.85f, cy + r * 0.68f);
            g.strokePath (p, stroke);
            g.fillEllipse (cx - r * 0.22f, cy + r * 0.68f - r * 0.22f, r * 0.44f, r * 0.44f);
            break;
        case 3: // Twin tilted bars
            p.startNewSubPath (cx - r * 0.55f, cy + r * 0.85f); p.lineTo (cx + r * 0.05f, cy - r * 0.85f);
            p.startNewSubPath (cx - r * 0.05f, cy + r * 0.85f); p.lineTo (cx + r * 0.55f, cy - r * 0.85f);
            g.strokePath (p, stroke);
            break;
        case 4: // Rhombus / Diamond
            p.startNewSubPath (cx, cy - r * 0.95f);
            p.lineTo (cx + r * 0.82f, cy);
            p.lineTo (cx, cy + r * 0.95f);
            p.lineTo (cx - r * 0.82f, cy);
            p.closeSubPath();
            g.strokePath (p, stroke);
            break;
        case 5: // Crescent arc + horizontal bar
            p.addCentredArc (cx + r * 0.15f, cy, r * 0.82f, r * 0.82f, 0.0f, kPiF * 0.92f, kPiF * 2.08f, true);
            p.startNewSubPath (cx - r * 0.65f, cy); p.lineTo (cx + r * 0.72f, cy);
            g.strokePath (p, stroke);
            break;
        case 6: // Split square frame
            p.addRectangle (cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f);
            p.startNewSubPath (cx, cy - r * 0.78f); p.lineTo (cx, cy + r * 0.78f);
            g.strokePath (p, stroke);
            break;
        case 7: // Minimalist trident (Psi)
            p.startNewSubPath (cx, cy - r * 0.92f); p.lineTo (cx, cy + r * 0.95f);
            p.startNewSubPath (cx - r * 0.78f, cy - r * 0.55f);
            p.quadraticTo (cx - r * 0.72f, cy + r * 0.22f, cx, cy + r * 0.22f);
            p.quadraticTo (cx + r * 0.72f, cy + r * 0.22f, cx + r * 0.78f, cy - r * 0.55f);
            g.strokePath (p, stroke);
            break;
    }
}

void CrystalAudioProcessorEditor::draw5RuneCode (juce::Graphics& g, int seed, float cx, float cy,
                                                 float glyphR, float stepX, juce::Colour color, float alpha) const
{
    const auto runes = CrystalDSP::seedToRunes (seed);
    const float startX = cx - stepX * 2.0f;
    for (int i = 0; i < 5; ++i)
        drawRuneGlyph (g, runes[static_cast<size_t> (i)], startX + i * stepX, cy, glyphR, color, alpha);
}

void CrystalAudioProcessorEditor::drawHandDrawnIcon (juce::Graphics& g, int type, float cx, float cy,
                                                     const CrystalDSP::Palette& pal, float seed) const
{
    const float r = 6.2f, w = 1.75f;
    auto subEdge = [&] (juce::Point<float> p0, juce::Point<float> p1, int steps, float sSeed, std::vector<juce::Point<float>>& out)
    {
        for (int i = 0; i < steps; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (steps);
            out.push_back ({
                p0.x + (p1.x - p0.x) * t + smoothNoise (t * 3.5f, sSeed) * 0.4f,
                p0.y + (p1.y - p0.y) * t + smoothNoise (t * 3.5f + 11.0f, sSeed) * 0.4f
            });
        }
    };

    std::vector<juce::Point<float>> pts;
    if (type == 0) // Square
    {
        juce::Point<float> c[4] = { {cx-r,cy-r}, {cx+r,cy-r}, {cx+r,cy+r}, {cx-r,cy+r} };
        subEdge (c[0], c[1], 4, seed + 1.0f, pts);
        subEdge (c[1], c[2], 4, seed + 2.0f, pts);
        subEdge (c[2], c[3], 4, seed + 3.0f, pts);
        subEdge (c[3], c[0], 4, seed + 4.0f, pts);
        std::vector<float> ws (pts.size());
        for (size_t i = 0; i < pts.size(); ++i) ws[i] = w * (0.88f + 0.22f * hash1 (static_cast<float> (i) + seed));
        drawMarkerRibbon (g, pts, ws, true, pal.ink, 0.88f);
        drawInkStroke (g, { {cx - r * 0.55f, cy + r * 0.55f}, {cx + r * 0.55f, cy - r * 0.55f} },
                       1.2f, pal.contrast, 0.78f, false, seed + 9.0f, 0.25f);
    }
    else if (type == 1) // Triangle
    {
        juce::Point<float> c[3] = { {cx, cy - r * 1.14f}, {cx + r * 1.16f, cy + r * 0.92f}, {cx - r * 1.16f, cy + r * 0.92f} };
        subEdge (c[0], c[1], 5, seed + 11.0f, pts);
        subEdge (c[1], c[2], 5, seed + 12.0f, pts);
        subEdge (c[2], c[0], 5, seed + 13.0f, pts);
        std::vector<float> ws (pts.size());
        for (size_t i = 0; i < pts.size(); ++i) ws[i] = w * (0.88f + 0.22f * hash1 (static_cast<float> (i) + seed));
        drawMarkerRibbon (g, pts, ws, true, pal.ink, 0.88f);
    }
    else // Circle
    {
        for (int i = 0; i < 20; ++i)
        {
            const float a = (static_cast<float> (i) / 20.0f) * kTwoPiF;
            const float rr = r * 1.04f + loopNoise (a, 2.0f, seed + 21.0f) * 0.6f;
            pts.push_back ({ cx + std::cos (a) * rr, cy + std::sin (a) * rr });
        }
        std::vector<float> ws (pts.size());
        for (size_t i = 0; i < pts.size(); ++i) ws[i] = w * (0.88f + 0.22f * hash1 (static_cast<float> (i) + seed));
        drawMarkerRibbon (g, pts, ws, true, pal.ink, 0.88f);
    }
}

// =============================================================================
// SECTION RENDERERS
// =============================================================================
void CrystalAudioProcessorEditor::renderParallelHatch (juce::Graphics& g,
                                                       const std::vector<float>& orgScales,
                                                       const CrystalDSP::Palette& pal,
                                                       float ballCy, float hatchAlpha,
                                                       CrystalDSP::HatchStyle style,
                                                       float slowT, int polyOverride, float orgOverride) const
{
    if (hatchAlpha <= 0.01f) return;
    const float k0 = *processor.apvts.getRawParameterValue ("knob0");
    const float k1 = *processor.apvts.getRawParameterValue ("knob1");
    const float k2 = (orgOverride >= 0.0f) ? orgOverride : processor.apvts.getRawParameterValue ("knob2")->load();
    const float dr = *processor.apvts.getRawParameterValue ("drive");
    const float amt = std::min (1.0f, k0 * 0.78f + dr * 0.28f);
    if (amt <= 0.02f) return;

    const int polyStep = (polyOverride >= 0) ? polyOverride : std::min (3, static_cast<int> (k1 * 3.999f));
    const float hSeed = static_cast<float> (hatchSeed);
    const float gap = 9.0f, bandW = 5.0f + amt * 22.0f;
    const float squareness = 2.0f + std::pow (amt, 2.55f) * 22.0f;
    const float jaggedPx = amt * 3.5f;
    const float outerOrgW = k2 > 0.002f ? (1.0f - std::pow (amt, 2.4f) * 0.35f) : 0.0f;

    auto buildRing = [&] (float pad, float sq, float oW, float jag, float sd)
    {
        const int n = static_cast<int> (orgScales.size());
        std::vector<juce::Point<float>> ring (static_cast<size_t> (n));
        const float halfBase = kBallR + pad;
        for (int i = 0; i < n; ++i)
        {
            const float a = (static_cast<float> (i) / static_cast<float> (n)) * kTwoPiF;
            float r = superellipseRadius (a, halfBase, sq) + (orgScales[static_cast<size_t> (i)] - 1.0f) * kBallR * oW;
            if (jag > 0.0f)
                r += ((std::abs (loopNoise (a, 6.0f, sd + 91.0f)) * 2.0f - 1.0f) * 0.55f + loopNoise (a, 11.0f, sd + 203.0f) * 0.45f) * jag;
            ring[static_cast<size_t> (i)] = { kBallCx + std::cos (a) * r, ballCy + std::sin (a) * r };
        }
        return ring;
    };

    // Ic kesim (innerRing): Kurede yuvarlak, poligonda donen 3D poligonun gercek siluetine oturur!
    std::vector<juce::Point<float>> innerRing;
    if (polyStep == 0)
    {
        innerRing = buildRing (gap, 2.0f, 1.0f, 0.75f, hSeed + 13.0f);
    }
    else
    {
        const auto& mesh = (polyStep == 1) ? mesh32 : (polyStep == 2 ? mesh20 : mesh8);
        const float rad = kBallR * (1.0f + snapPop * 0.08f);
        std::vector<juce::Point<float>> proj;
        proj.reserve (mesh.verts.size());
        for (const auto& v : mesh.verts)
        {
            const float s = organicScale3D (v.x, v.y, v.z, k2, slowT);
            const Vec3 rv = qRotateVec (q, { v.x * s, v.y * s, v.z * s });
            proj.push_back ({ kBallCx + rv.x * rad, ballCy - rv.y * rad });
        }

        // 2D Convex Hull (Andrew's monotone chain)
        std::sort (proj.begin(), proj.end(), [] (const auto& a, const auto& b)
        {
            return (a.x != b.x) ? (a.x < b.x) : (a.y < b.y);
        });
        auto cross2 = [] (juce::Point<float> o, juce::Point<float> a, juce::Point<float> b)
        {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<juce::Point<float>> hull;
        for (const auto& p : proj)
        {
            while (hull.size() >= 2 && cross2 (hull[hull.size() - 2], hull.back(), p) <= 0.0f) hull.pop_back();
            hull.push_back (p);
        }
        const size_t lowerSz = hull.size() + 1;
        for (int i = static_cast<int> (proj.size()) - 2; i >= 0; --i)
        {
            while (hull.size() >= lowerSz && cross2 (hull[hull.size() - 2], hull.back(), proj[static_cast<size_t> (i)]) <= 0.0f) hull.pop_back();
            hull.push_back (proj[static_cast<size_t> (i)]);
        }
        if (hull.size() > 1) hull.pop_back();

        // Hull'i dis kenar normalleri boyunca 'gap + 2.5' px genislet + el-cizimi hafif dalga
        const int hn = static_cast<int> (hull.size());
        if (hn >= 3)
        {
            const float pad = gap + 2.5f;
            std::vector<juce::Point<float>> expHull (static_cast<size_t> (hn));
            for (int i = 0; i < hn; ++i)
            {
                const auto pPrev = hull[static_cast<size_t> ((i + hn - 1) % hn)];
                const auto pCur  = hull[static_cast<size_t> (i)];
                const auto pNext = hull[static_cast<size_t> ((i + 1) % hn)];

                auto outNorm = [] (juce::Point<float> a, juce::Point<float> b) -> juce::Point<float>
                {
                    const float dx = b.x - a.x, dy = b.y - a.y;
                    const float l = std::hypot (dx, dy);
                    if (l < 1e-4f) return { 0.0f, 0.0f };
                    // CCW hull in screen-Y-down: outward normal is (+dy, -dx)
                    return { dy / l, -dx / l };
                };

                const auto n1 = outNorm (pPrev, pCur);
                const auto n2 = outNorm (pCur, pNext);
                float bx = n1.x + n2.x, by = n1.y + n2.y;
                const float bl = std::hypot (bx, by);
                if (bl > 1e-4f) { bx /= bl; by /= bl; }
                else { bx = n1.x; by = n1.y; }
                const float cosHalf = std::max (0.55f, bx * n1.x + by * n1.y);
                const float dist = pad / cosHalf;
                expHull[static_cast<size_t> (i)] = { pCur.x + bx * dist, pCur.y + by * dist };
            }

            // Her kenari 4 alt noktaya bolup cok hafif el-cizimi titreme ver
            for (int i = 0; i < hn; ++i)
            {
                const auto a = expHull[static_cast<size_t> (i)];
                const auto b = expHull[static_cast<size_t> ((i + 1) % hn)];
                for (int s = 0; s < 4; ++s)
                {
                    const float t = static_cast<float> (s) * 0.25f;
                    const float jx = smoothNoise (static_cast<float> (i * 4 + s) * 0.9f, hSeed + 13.0f) * 0.65f;
                    const float jy = smoothNoise (static_cast<float> (i * 4 + s) * 0.9f + 7.0f, hSeed + 19.0f) * 0.65f;
                    innerRing.push_back ({ a.x + (b.x - a.x) * t + jx, a.y + (b.y - a.y) * t + jy });
                }
            }
        }
        else
        {
            innerRing = buildRing (gap, 2.0f, 1.0f, 0.75f, hSeed + 13.0f);
        }
    }

    const auto outerRing = buildRing (gap + bandW, squareness, outerOrgW, jaggedPx, hSeed);

    juce::Path clipPath;
    clipPath.setUsingNonZeroWinding (false);
    clipPath.startNewSubPath (outerRing[0]);
    for (size_t i = 1; i < outerRing.size(); ++i) clipPath.lineTo (outerRing[i]);
    clipPath.closeSubPath();
    clipPath.startNewSubPath (innerRing.back());
    for (int i = static_cast<int> (innerRing.size()) - 2; i >= 0; --i) clipPath.lineTo (innerRing[static_cast<size_t> (i)]);
    clipPath.closeSubPath();

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (clipPath);

    // 4 YAMUK (DIYAGONAL) KATEGORIK HATCH STILI — Hepsi el cizimi keceli kalem ribbon!
    float ang = -kPiF * 0.24f;
    float scrollDir = 1.0f;
    bool isZigzag = false;

    switch (style)
    {
        case CrystalDSP::HatchStyle::DiagUpRight:     ang = -kPiF * 0.24f; scrollDir =  1.0f; isZigzag = false; break;
        case CrystalDSP::HatchStyle::DiagDownRight:   ang =  kPiF * 0.24f; scrollDir = -1.0f; isZigzag = false; break;
        case CrystalDSP::HatchStyle::ZigzagUpRight:   ang = -kPiF * 0.24f; scrollDir =  1.0f; isZigzag = true;  break;
        case CrystalDSP::HatchStyle::ZigzagDownRight: ang =  kPiF * 0.24f; scrollDir = -1.0f; isZigzag = true;  break;
        default: break;
    }

    const float maxR = (kBallR * 1.32f + gap + bandW) * 1.52f + jaggedPx + 14.0f;
    const float spacing = isZigzag ? std::max (11.5f, 15.5f - amt * 3.2f) : std::max (7.2f, 12.0f - amt * 4.2f);
    const float strokeW = 1.95f + amt * 0.85f;
    const float ux = std::cos (ang), uy = std::sin (ang), nx = -uy, ny = ux;
    float off = std::fmod (barberScroll * scrollDir, spacing);
    if (off < 0.0f) off += spacing;
    const int cnt = static_cast<int> (std::ceil ((maxR * 2.0f) / spacing)) + 2;

    if (isZigzag)
    {
        // El cizimi keceli kalem zikzak (drawMarkerRibbon + organik kalinlik & hafif titreme!)
        constexpr float zzPitch = 12.5f;
        constexpr float zzAmp   = 3.9f;

        for (int i = -cnt; i <= cnt; ++i)
        {
            const float d = i * spacing + off;
            if (std::abs (d) > maxR) continue;
            const float hc = std::sqrt (std::max (0.0f, maxR * maxR - d * d));
            const int teeth = std::max (6, static_cast<int> (std::ceil ((hc * 2.0f) / zzPitch)));
            const float ls = hSeed + 503.0f + i * 7.31f;

            std::vector<juce::Point<float>> pts;
            std::vector<float> ws;
            pts.reserve (static_cast<size_t> (teeth * 2 + 1));
            ws.reserve  (static_cast<size_t> (teeth * 2 + 1));

            for (int j = 0; j <= teeth; ++j)
            {
                const float along = -hc + (static_cast<float> (j) / static_cast<float> (teeth)) * (hc * 2.0f);
                const float tooth = ((j & 1) == 0 ? 1.0f : -1.0f) * zzAmp + smoothNoise (j * 0.85f, ls) * 0.95f;
                const juce::Point<float> vPt { kBallCx + nx * (d + tooth) + ux * along,
                                               ballCy  + ny * (d + tooth) + uy * along };
                if (j > 0)
                {
                    // Her dis bacak ortasina organik el-cizimi orta nokta ekle -> sert vektor degil, keceli kalem!
                    const auto prev = pts.back();
                    const float midWob = smoothNoise (j * 1.45f + 9.0f, ls) * 0.75f;
                    pts.push_back ({ (prev.x + vPt.x) * 0.5f + nx * midWob, (prev.y + vPt.y) * 0.5f + ny * midWob });
                    ws.push_back (strokeW * (0.82f + 0.30f * hash1 (j * 2.3f + ls)));
                }
                pts.push_back (vPt);
                ws.push_back (strokeW * (0.88f + 0.28f * hash1 (j * 1.7f + ls)));
            }
            drawMarkerRibbon (g, pts, ws, false, pal.contrast, 0.90f * hatchAlpha);
        }
    }
    else
    {
        std::vector<juce::Point<float>> pts (10);
        std::vector<float> ws (10);
        for (int i = -cnt; i <= cnt; ++i)
        {
            const float d = i * spacing + off;
            if (std::abs (d) > maxR) continue;
            const float hc = std::sqrt (std::max (0.0f, maxR * maxR - d * d));
            const float ls = hSeed + 401.0f + i * 7.31f;
            for (int j = 0; j <= 9; ++j)
            {
                const float along = (-1.0f + (static_cast<float> (j) / 9.0f) * 2.0f) * hc;
                const float kink = smoothNoise (j * 0.85f, ls) * (1.75f * amt);
                pts[static_cast<size_t> (j)] = { kBallCx + nx * (d + kink) + ux * along, ballCy + ny * (d + kink) + uy * along };
                ws[static_cast<size_t> (j)]  = strokeW * (0.85f + 0.28f * hash1 (j * 1.9f + ls));
            }
            drawMarkerRibbon (g, pts, ws, false, pal.contrast, 0.90f * hatchAlpha);
        }
    }
}

void CrystalAudioProcessorEditor::renderMainShape (juce::Graphics& g, const CrystalDSP::Palette& pal, float slowT,
                                                   float ballCy, float scaleMult, int polyOverride, float orgOverride) const
{
    if (scaleMult <= 0.01f) return;
    const float k1 = *processor.apvts.getRawParameterValue ("knob1");
    const float org = orgOverride >= 0.0f ? orgOverride : processor.apvts.getRawParameterValue ("knob2")->load();
    const float tilt = (bootActive && bootTime < 2.25f) ? 0.0f : processor.apvts.getRawParameterValue ("tilt")->load();
    const float filterAmt = clamp01 ((std::abs (tilt) - 0.05f) / 0.95f); // 0 = acik, 1 = tam kapali filtre
    const float fillAlpha = 1.0f - 0.90f * filterAmt;                     // Tam kapali filtrede %90 seffaf (%10 dolgu)!

    const int polyStep = polyOverride >= 0 ? polyOverride : std::min (3, static_cast<int> (k1 * 3.999f));
    const float R = kBallR * scaleMult * (1.0f + snapPop * 0.075f);
    const float baseW = kPenUnit * 1.05f * std::clamp (scaleMult, 0.45f, 1.05f);
    const float seed = static_cast<float> (sketchSeed);
    const Quat qInv = qConj (q);
    const Vec3 L = vecNorm (qRotateVec (q, vecNorm ({ -0.72f, 0.54f, 0.43f })));

    if (polyStep == 0)
    {
        // Filtre kapandikca kurenin arka yuzey meridyen/ekvator el-cizimi hatlari gorunur!
        if (filterAmt > 0.03f)
        {
            const float backAlpha = filterAmt * 0.36f;
            for (int ringIdx = 0; ringIdx < 3; ++ringIdx)
            {
                std::vector<juce::Point<float>> seg;
                constexpr int steps = 40;
                for (int i = 0; i <= steps; ++i)
                {
                    const float a = (static_cast<float> (i) / steps) * kTwoPiF;
                    Vec3 pObj;
                    if (ringIdx == 0)      pObj = { std::cos (a), std::sin (a), 0.0f };
                    else if (ringIdx == 1) pObj = { std::cos (a), 0.0f, std::sin (a) };
                    else                   pObj = { 0.0f, std::cos (a), std::sin (a) };
                    const float sc = organicScale3D (pObj.x, pObj.y, pObj.z, org, slowT);
                    const Vec3 rot = qRotateVec (q, { pObj.x * sc, pObj.y * sc, pObj.z * sc });
                    if (rot.z <= 0.06f)
                    {
                        seg.push_back ({ kBallCx + rot.x * R, ballCy - rot.y * R });
                    }
                    else if (seg.size() >= 2)
                    {
                        drawInkStroke (g, seg, baseW * 0.36f, pal.ink, backAlpha, false, seed + 610.0f + ringIdx * 17.0f, 0.42f);
                        seg.clear();
                    }
                    else
                    {
                        seg.clear();
                    }
                }
                if (seg.size() >= 2)
                    drawInkStroke (g, seg, baseW * 0.36f, pal.ink, backAlpha, false, seed + 610.0f + ringIdx * 17.0f, 0.42f);
            }
        }

        constexpr int n = 72;
        std::vector<juce::Point<float>> silPts (n);
        std::vector<float> silW (n);
        juce::Path trace;
        for (int i = 0; i < n; ++i)
        {
            const float a = (static_cast<float> (i) / n) * kTwoPiF;
            const float nx = std::cos (a), ny = std::sin (a);
            const Vec3 objDir = qRotateVec (qInv, { nx, -ny, 0.0f });
            const float rr = R * organicScale3D (objDir.x, objDir.y, objDir.z, org, slowT)
                           + loopNoise (a, 2.0f, seed) * 1.4f + loopNoise (a, 5.0f, seed + 19.0f) * 0.5f;
            silPts[static_cast<size_t> (i)] = { kBallCx + nx * rr, ballCy + ny * rr };
            silW[static_cast<size_t> (i)]   = baseW * (1.0f - (nx * L.x + (-ny) * L.y) * 0.16f) * (1.0f + loopNoise (a, 3.0f, seed + 77.0f) * 0.13f);
            if (i == 0) trace.startNewSubPath (silPts[0]); else trace.lineTo (silPts[static_cast<size_t> (i)]);
        }
        trace.closeSubPath();
        g.setColour (pal.base.withMultipliedAlpha (fillAlpha));
        g.fillPath (trace);

        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (trace);
            const float s = std::hypot (L.x, L.y);
            if (s < 1e-4f)
            {
                if (L.z < 0.0f) { g.setColour (pal.shadow.withMultipliedAlpha (fillAlpha)); g.fillRect (kBallCx - R * 1.5f, ballCy - R * 1.5f, R * 3.0f, R * 3.0f); }
            }
            else
            {
                const float ux = -L.y / s, uy = L.x / s, vx = (-L.x * L.z) / s, vy = (-L.y * L.z) / s, oR = R * 1.5f;
                juce::Path shad;
                shad.startNewSubPath (kBallCx + ux * oR, ballCy - uy * oR);
                for (int i = 0; i <= 38; ++i)
                {
                    const float t = static_cast<float> (i) / 38.0f, th = t * kPiF;
                    const float px = std::cos (th) * ux + std::sin (th) * vx;
                    const float py = std::cos (th) * uy + std::sin (th) * vy;
                    const float pz = std::sin (th) * s;
                    const Vec3 objP = qRotateVec (qInv, { px, py, pz });
                    const float orgS = organicScale3D (objP.x, objP.y, objP.z, org, slowT);
                    const float wob = (smoothNoise (t * 6.5f, seed + 211.0f) * 1.35f + (orgS - 1.0f) * R * 0.65f) * (0.25f + 0.75f * std::sin (th));
                    shad.lineTo (kBallCx + px * R * orgS + (L.x / s) * wob, ballCy - py * R * orgS - (L.y / s) * wob);
                }
                const float aStart = std::atan2 (-L.x, L.y);
                for (int i = 0; i <= 22; ++i)
                {
                    const float a = aStart - (static_cast<float> (i) / 22.0f) * kPiF;
                    shad.lineTo (kBallCx + std::cos (a) * oR, ballCy - std::sin (a) * oR);
                }
                shad.closeSubPath();
                g.setColour (pal.shadow.withMultipliedAlpha (fillAlpha));
                g.fillPath (shad);
            }
        }

        std::vector<juce::Point<float>> softPts (n);
        std::vector<float> softW (n);
        for (int i = 0; i < n; ++i)
        {
            softPts[static_cast<size_t> (i)] = { silPts[static_cast<size_t> (i)].x + (hash1 (i + seed * 5.3f) - 0.5f),
                                                 silPts[static_cast<size_t> (i)].y + (hash1 (i + seed * 9.7f) - 0.5f) };
            softW[static_cast<size_t> (i)]   = silW[static_cast<size_t> (i)] * 1.14f;
        }
        drawMarkerRibbon (g, softPts, softW, true, pal.ink, 0.14f);
        drawMarkerRibbon (g, silPts, silW, true, pal.ink, 0.98f);
        return;
    }

    const PolyMesh& mesh = (polyStep == 1 ? mesh32 : (polyStep == 2 ? mesh20 : mesh8));
    static constexpr float kWMin[4] = { 0.0f, 0.22f, 0.40f, 0.62f };
    static constexpr float kWMax[4] = { 0.0f, 0.36f, 0.56f, 0.76f };
    static constexpr float kAMin[4] = { 0.0f, 0.20f, 0.45f, 0.78f };
    static constexpr float kAMax[4] = { 0.0f, 0.46f, 0.75f, 0.96f };

    const float camDist = 4.8f;
    const float sb = polyStep == 3 ? 1.08f : (polyStep == 2 ? 1.04f : 1.01f);

    std::vector<Vec3> rotV (mesh.verts.size());
    std::vector<juce::Point<float>> projV (mesh.verts.size());
    for (size_t i = 0; i < mesh.verts.size(); ++i)
    {
        const auto& v = mesh.verts[i];
        const float sc = organicScale3D (v.x, v.y, v.z, org, slowT);
        rotV[i] = qRotateVec (q, { v.x * sc, v.y * sc, v.z * sc });
        const float p = camDist / std::max (1.4f, camDist - rotV[i].z);
        projV[i] = { kBallCx + rotV[i].x * R * sb * p, ballCy - rotV[i].y * R * sb * p };
    }

    struct VisFace { std::array<int, 3> f; float mz; bool isShadow; };
    struct EdgeInfo { int a = 0, b = 0, visCount = 0, lit = 0, shad = 0; };
    std::vector<VisFace> visFaces;
    std::map<int, EdgeInfo> edgeMap;

    for (const auto& f : mesh.faces)
    {
        const Vec3& v0 = rotV[static_cast<size_t> (f[0])];
        const Vec3& v1 = rotV[static_cast<size_t> (f[1])];
        const Vec3& v2 = rotV[static_cast<size_t> (f[2])];
        const Vec3 n = vecNorm (vecCross (vecSub (v1, v0), vecSub (v2, v0)));
        const float mx = (v0.x + v1.x + v2.x) / 3.0f, my = (v0.y + v1.y + v2.y) / 3.0f, mz = (v0.z + v1.z + v2.z) / 3.0f;
        const bool isFront = vecDot (n, { -mx, -my, camDist - mz }) > 0.0f;
        const bool isShad  = vecDot (n, L) < 0.08f;

        if (isFront) visFaces.push_back ({ f, mz, isShad });

        for (int i = 0; i < 3; ++i)
        {
            const int a = f[static_cast<size_t> (i)], b = f[static_cast<size_t> ((i + 1) % 3)];
            const int minV = std::min (a, b), maxV = std::max (a, b), key = minV * 1024 + maxV;
            auto& info = edgeMap[key];
            info.a = minV; info.b = maxV;
            if (isFront)
            {
                info.visCount++;
                if (isShad) info.shad++; else info.lit++;
            }
        }
    }
    std::sort (visFaces.begin(), visFaces.end(), [] (const VisFace& a, const VisFace& b) { return a.mz < b.mz; });

    std::map<int, std::vector<juce::Point<float>>> edgeCache;
    auto getEdge = [&] (int a, int b)
    {
        const bool fwd = a < b;
        const int minV = fwd ? a : b, maxV = fwd ? b : a, key = minV * 1024 + maxV;
        auto it = edgeCache.find (key);
        if (it == edgeCache.end())
        {
            const auto p0 = projV[static_cast<size_t> (minV)], p1 = projV[static_cast<size_t> (maxV)];
            const float dx = p1.x - p0.x, dy = p1.y - p0.y, len = std::max (1e-4f, std::hypot (dx, dy));
            const float nx = -dy / len, ny = dx / len;
            const int steps = polyStep >= 2 ? 7 : 4;
            const float es = seed + static_cast<float> (key % 997) * 1.7f;
            std::vector<juce::Point<float>> pts (static_cast<size_t> (steps + 1));
            for (int i = 0; i <= steps; ++i)
            {
                const float t = static_cast<float> (i) / static_cast<float> (steps);
                const float h = smoothNoise (t * 4.5f, es) * 0.85f * std::sin (t * kPiF);
                pts[static_cast<size_t> (i)] = { p0.x + dx * t + nx * h, p0.y + dy * t + ny * h };
            }
            it = edgeCache.emplace (key, std::move (pts)).first;
        }
        auto res = it->second;
        if (! fwd) std::reverse (res.begin(), res.end());
        return res;
    };

    // 1. ARKA YUZEY HATTI (Filtre kapandikca seffaf kristalin arkasindaki el-cizimi 3D kenarlar belirir!)
    if (filterAmt > 0.03f)
    {
        const float backAlpha = filterAmt * (polyStep == 1 ? 0.24f : 0.42f);
        const float backW = baseW * (polyStep == 1 ? 0.28f : 0.42f);
        for (const auto& [key, info] : edgeMap)
        {
            if (info.visCount != 0) continue;
            const auto pts = getEdge (info.a, info.b);
            const int n = static_cast<int> (pts.size());
            std::vector<float> ws (static_cast<size_t> (n));
            for (int i = 0; i < n; ++i)
                ws[static_cast<size_t> (i)] = backW * (0.78f + 0.30f * std::sin ((static_cast<float> (i) / static_cast<float> (n - 1)) * kPiF));
            drawMarkerRibbon (g, pts, ws, false, pal.ink, backAlpha);
        }
    }

    // 2. ON YUZEY DOLGULARI (filtre yogunluguna gore %100 -> %10 opaklik!)
    for (const auto& face : visFaces)
    {
        juce::Path fp;
        for (int i = 0; i < 3; ++i)
        {
            const auto pts = getEdge (face.f[static_cast<size_t> (i)], face.f[static_cast<size_t> ((i + 1) % 3)]);
            for (size_t j = 0; j < pts.size(); ++j)
            {
                if (i == 0 && j == 0) fp.startNewSubPath (pts[0]); else fp.lineTo (pts[j]);
            }
        }
        fp.closeSubPath();
        const auto col = (face.isShadow ? pal.shadow : pal.base).withMultipliedAlpha (fillAlpha);
        g.setColour (col);
        g.fillPath (fp);
        if (fillAlpha > 0.45f)
            g.strokePath (fp, juce::PathStrokeType (0.6f));
    }

    // 3. ON & SILUET KECELI KALEM KENARLARI (her zaman net ve belirgin!)
    for (const auto& [key, info] : edgeMap)
    {
        if (info.visCount == 0) continue;
        const bool isSil = info.visCount == 1, isTrans = info.lit > 0 && info.shad > 0;
        const auto pts = getEdge (info.a, info.b);
        const int n = static_cast<int> (pts.size());
        std::vector<float> ws (static_cast<size_t> (n));
        const float ev = hash1 (static_cast<float> (key) * 0.731f + 19.4f);
        const float ew = kWMin[polyStep] + (kWMax[polyStep] - kWMin[polyStep]) * ev;
        const float ea = kAMin[polyStep] + (kAMax[polyStep] - kAMin[polyStep]) * (0.35f * ev + 0.65f * hash1 (static_cast<float> (key) * 1.91f + 7.2f));
        const float tw = isSil ? baseW : baseW * (isTrans ? std::min (0.84f, ew + 0.12f) : ew);
        const float alpha = isSil ? 0.98f : (isTrans ? std::min (0.92f, ea + 0.16f) : ea);
        for (int i = 0; i < n; ++i)
        {
            const float s = std::sin ((static_cast<float> (i) / static_cast<float> (n - 1)) * kPiF);
            ws[static_cast<size_t> (i)] = tw * (polyStep == 1 && ! isSil ? (0.55f + 0.55f * s) : (0.86f + 0.24f * s));
        }
        if (isSil)
        {
            std::vector<juce::Point<float>> sPts (static_cast<size_t> (n));
            std::vector<float> sW (static_cast<size_t> (n));
            for (int i = 0; i < n; ++i)
            {
                sPts[static_cast<size_t> (i)] = { pts[static_cast<size_t> (i)].x + (hash1 (i + seed * 5.0f + key) - 0.5f),
                                                  pts[static_cast<size_t> (i)].y + (hash1 (i + seed * 9.0f + key) - 0.5f) };
                sW[static_cast<size_t> (i)]   = ws[static_cast<size_t> (i)] * 1.14f;
            }
            drawMarkerRibbon (g, sPts, sW, false, pal.ink, 0.14f);
        }
        drawMarkerRibbon (g, pts, ws, false, pal.ink, alpha);
    }
}

void CrystalAudioProcessorEditor::renderDistortionScope (juce::Graphics& g, const CrystalDSP::Palette& pal,
                                                        float yOffset, float alpha) const
{
    if (alpha <= 0.01f) return;
    constexpr int N = 44;
    const float wy = kWaveCy + yOffset;

    const bool hasAudio = processor.lastInputLevel.load (std::memory_order_relaxed) > 0.0005f;
    std::array<juce::Point<float>, N> pts;

    if (hasAudio)
    {
        constexpr int len = CrystalAudioProcessor::kScopeSize, mask = len - 1;
        int trig = (processor.scopeWritePos.load (std::memory_order_acquire) - 512 + len) & mask;
        for (int s = 0; s < 256; ++s)
        {
            const int i0 = (trig - s + len) & mask, i1 = (i0 - 1 + len) & mask;
            if (processor.scopeRing[i1] <= 0.0f && processor.scopeRing[i0] > 0.0f) { trig = i0; break; }
        }
        for (int i = 0; i < N; ++i)
        {
            const int idx = (trig + static_cast<int> (i * 6.5f)) & mask;
            const float v = std::clamp (processor.scopeRing[idx] * 1.25f, -1.0f, 1.0f);
            pts[static_cast<size_t> (i)] = { kWaveX + (static_cast<float> (i) / (N - 1)) * kWaveW, wy - v * kWaveHalfH };
        }
    }
    else
    {
        for (int i = 0; i < N; ++i)
        {
            const float u = static_cast<float> (i) / static_cast<float> (N - 1);
            const float v = std::sin (u * kTwoPiF + timeSec * 2.2f) * 0.22f * std::sin (u * kPiF);
            pts[static_cast<size_t> (i)] = { kWaveX + u * kWaveW, wy - v * kWaveHalfH };
        }
    }

    juce::Path fillP, lineP;
    fillP.startNewSubPath (kWaveX, wy);
    lineP.startNewSubPath (pts[0]);
    for (int i = 0; i < N; ++i)
    {
        fillP.lineTo (pts[static_cast<size_t> (i)]);
        if (i > 0) lineP.lineTo (pts[static_cast<size_t> (i)]);
    }
    fillP.lineTo (kWaveX + kWaveW, wy);
    fillP.closeSubPath();

    g.setColour (pal.contrast.withMultipliedAlpha (0.18f * alpha));
    g.fillPath (fillP);
    g.setColour (pal.contrast.withMultipliedAlpha (0.92f * alpha));
    g.strokePath (lineP, juce::PathStrokeType (1.85f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float dr = *processor.apvts.getRawParameterValue ("drive");
    const float dx = kWaveX + dr * kWaveW, dy = wy + kWaveHalfH + 5.5f;
    g.setColour (pal.ink.withMultipliedAlpha (0.22f * alpha));
    g.drawLine (kWaveX, dy, kWaveX + kWaveW, dy, 1.0f);
    g.setColour (pal.ink.withMultipliedAlpha (0.88f * alpha));
    g.fillEllipse (dx - 1.9f, dy - 1.9f, 3.8f, 3.8f);
}

void CrystalAudioProcessorEditor::renderOrganicKnobs (juce::Graphics& g, const CrystalDSP::Palette& pal,
                                                      const float yOffsets[3], const float alphas[3],
                                                      const float sx[3], const float sy[3]) const
{
    const float startA = kPiF * 0.75f, sweep = kPiF * 1.5f;

    for (int idx = 0; idx < 3; ++idx)
    {
        const float ka = alphas[idx];
        if (ka <= 0.01f) continue;

        const float val = std::clamp (knobDispVal[idx], 0.0f, 1.0f);
        const float kx = kKnobX[idx], ky = kKnobY + yOffsets[idx], r = kKnobR, arcR = r + 6.4f;
        const float kSeed = std::floor (knobSeeds[idx]);

        // Sadece boot inisindeki Disney Squash & Stretch (normal kullanim & snap'te agir ve sert!)
        const float curSx = sx[idx];
        const float curSy = sy[idx];

        juce::Graphics::ScopedSaveState knobSS (g);
        if (std::abs (curSx - 1.0f) > 0.002f || std::abs (curSy - 1.0f) > 0.002f)
            g.addTransform (juce::AffineTransform::scale (curSx, curSy, kx, ky));

        std::vector<juce::Point<float>> bgArc (19);
        for (int i = 0; i <= 18; ++i)
        {
            const float a = startA + (static_cast<float> (i) / 18.0f) * sweep;
            bgArc[static_cast<size_t> (i)] = { kx + std::cos (a) * arcR, ky + std::sin (a) * arcR };
        }
        drawInkStroke (g, bgArc, 1.45f, pal.ink, 0.16f * ka, false, kSeed + 1.0f, 0.28f);

        if (val > 0.015f)
        {
            const int st = std::max (3, static_cast<int> (std::round (val * 18.0f)));
            std::vector<juce::Point<float>> vArc (static_cast<size_t> (st + 1));
            for (int i = 0; i <= st; ++i)
            {
                const float a = startA + ((static_cast<float> (i) / static_cast<float> (st)) * val) * sweep;
                vArc[static_cast<size_t> (i)] = { kx + std::cos (a) * arcR, ky + std::sin (a) * arcR };
            }
            drawInkStroke (g, vArc, 2.25f, idx == 0 ? pal.contrast : pal.ink, 0.92f * ka, false, kSeed + 2.0f, 0.30f);
        }

        constexpr int segs = 28;
        std::vector<juce::Point<float>> bodyPts (segs);
        juce::Path traceB;
        for (int i = 0; i < segs; ++i)
        {
            const float a = (static_cast<float> (i) / segs) * kTwoPiF;
            const float rr = r + loopNoise (a, 2.0f, kSeed + 5.0f) * 0.85f + loopNoise (a, 5.0f, kSeed + 19.0f) * 0.35f;
            bodyPts[static_cast<size_t> (i)] = { kx + std::cos (a) * rr, ky + std::sin (a) * rr };
            if (i == 0) traceB.startNewSubPath (bodyPts[0]); else traceB.lineTo (bodyPts[static_cast<size_t> (i)]);
        }
        traceB.closeSubPath();
        g.setColour (pal.base.withMultipliedAlpha (ka));
        g.fillPath (traceB);

        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (traceB);
            g.setColour (pal.shadow.withMultipliedAlpha (ka));
            g.fillEllipse (kx + r * 0.36f - r * 0.98f, ky + r * 0.32f - r * 0.98f, r * 1.96f, r * 1.96f);
            g.setColour (pal.base.withMultipliedAlpha (ka));
            g.fillEllipse (kx - r * 0.16f - r * 0.94f, ky - r * 0.14f - r * 0.94f, r * 1.88f, r * 1.88f);
        }

        const float bw = (activeKnob == idx) ? 2.8f : ((hoverKnob == idx) ? 2.5f : 2.25f);
        std::vector<float> ws (segs);
        for (int i = 0; i < segs; ++i)
        {
            const float a = (static_cast<float> (i) / segs) * kTwoPiF;
            ws[static_cast<size_t> (i)] = bw * (1.0f + std::sin (a - 0.8f) * 0.14f + loopNoise (a, 3.0f, kSeed + 31.0f) * 0.09f);
        }
        drawMarkerRibbon (g, bodyPts, ws, true, pal.ink, 0.96f * ka);

        const float ptrA = startA + val * sweep;
        drawInkStroke (g, { { kx + std::cos (ptrA) * (r * 0.15f), ky + std::sin (ptrA) * (r * 0.15f) },
                            { kx + std::cos (ptrA) * (r * 0.78f), ky + std::sin (ptrA) * (r * 0.78f) } },
                       2.4f, pal.ink, 0.98f * ka, false, kSeed + 9.0f, 0.24f);

        juce::Graphics::ScopedSaveState iconSS (g);
        g.setOpacity (ka);
        drawHandDrawnIcon (g, idx, kx, ky + r + 20.0f, pal, kSeed + 41.0f);
    }
}

// =============================================================================
// SCRAP BRAIN AUDIO PIXEL-PERFECT MONOKROM LOGO (4 Ayri Lob, Yumusak Ton Hover)
// =============================================================================
void CrystalAudioProcessorEditor::renderBrainLogo (juce::Graphics& g, float cx, float cy, float size,
                                                   juce::Colour color, float alpha, bool interactive) const
{
    if (alpha <= 0.01f) return;
    const juce::Rectangle<float> dest (cx - size * 0.5f, cy - size * 0.5f, size, size);
    const bool darkBase = color.getPerceivedBrightness() < 0.52f;

    juce::Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    for (int i = 0; i < CrystalLogo::kNumLobes; ++i)
    {
        if (! logoLobes[i].isValid()) continue;
        juce::Colour c = color;
        if (interactive && lobeHover[i] > 0.005f)
        {
            const float h = lobeHover[i];
            // Wiggle yok! Sadece uzerine gelinen lobun renk tonu hafifce degisir
            c = darkBase ? color.brighter (0.48f * h) : color.darker (0.34f * h);
        }
        g.setColour (c.withMultipliedAlpha (alpha));
        g.drawImage (logoLobes[i], dest, juce::RectanglePlacement::stretchToFit, true);
    }
}

void CrystalAudioProcessorEditor::renderTopSeedBar (juce::Graphics& g, const CrystalDSP::Palette& pal,
                                                    float yOffset, float alpha) const
{
    if (alpha <= 0.01f) return;
    const int curSeed = processor.getCurrentSeed();
    const bool saved = processor.isCurrentSeedSaved();
    const float sy = kSeedCy + yOffset;

    // Sol ust kose: Pixel-perfect monokrom Scrap Brain logosu (vurgu rengi pal.contrast)
    renderBrainLogo (g, kLogoCx, sy, kLogoSize, pal.contrast, 0.94f * alpha, true);

    draw5RuneCode (g, curSeed, kSeedCx, sy, 5.2f, 18.5f,
                   explorerOpen ? pal.contrast : pal.ink,
                   ((hoverUI == "seed" || explorerOpen) ? 1.0f : 0.86f) * alpha);

    g.setColour ((explorerOpen ? pal.contrast : pal.ink).withMultipliedAlpha (((hoverUI == "seed" || explorerOpen) ? 0.55f : 0.16f) * alpha));
    g.drawLine (kSeedCx - 46.0f, sy + 11.5f, kSeedCx + 46.0f, sy + 11.5f, 1.1f);

    juce::Path dia;
    constexpr float sr = 4.6f;
    dia.startNewSubPath (kSaveX, sy - sr);
    dia.lineTo (kSaveX + sr, sy);
    dia.lineTo (kSaveX, sy + sr);
    dia.lineTo (kSaveX - sr, sy);
    dia.closeSubPath();
    if (saved)
    {
        g.setColour (pal.contrast.withMultipliedAlpha (0.95f * alpha));
        g.fillPath (dia);
    }
    else
    {
        g.setColour (pal.ink.withMultipliedAlpha ((hoverUI == "save" ? 0.95f : 0.48f) * alpha));
        g.strokePath (dia, juce::PathStrokeType (1.45f));
    }
}

void CrystalAudioProcessorEditor::renderInfoDrawer (juce::Graphics& g, const CrystalDSP::Palette& pal) const
{
    if (! infoOpen) return;

    // Sol ustte kucuk, hyperminimalist yuzen modul (tum ekrani kaplamaz!)
    const juce::Rectangle<float> card (kInfoX, kInfoY, kInfoW, kInfoH);

    g.setColour (pal.ink.withMultipliedAlpha (0.07f));
    g.fillRoundedRectangle (card.translated (0.0f, 2.5f), 11.0f);

    g.setColour (pal.paper.withMultipliedAlpha (0.97f));
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (pal.ink.withMultipliedAlpha (0.14f));
    g.drawRoundedRectangle (card, 10.0f, 1.0f);

    const float padX = kInfoX + 12.0f;
    const float innerW = kInfoW - 24.0f;

    // Baslik satiri: Wildin' + v1.0.0 (Quicksand)
    g.setColour (pal.ink.withMultipliedAlpha (0.92f));
    g.setFont (makeUiFont (13.0f, true));
    g.drawText ("Wildin'", juce::Rectangle<float> (padX, kInfoY + 10.0f, innerW * 0.6f, 18.0f), juce::Justification::centredLeft);

    g.setColour (pal.ink.withMultipliedAlpha (0.52f));
    g.setFont (makeUiFont (11.0f, false));
    g.drawText ("v" + juce::String (CrystalAudioProcessor::kPluginVersion),
                juce::Rectangle<float> (padX + innerW * 0.5f, kInfoY + 10.0f, innerW * 0.5f, 18.0f), juce::Justification::centredRight);

    g.setColour (pal.ink.withMultipliedAlpha (0.10f));
    g.drawLine (padX, kInfoY + 33.0f, padX + innerW, kInfoY + 33.0f, 1.0f);

    // Versiyon kontrol pill dugmesi (gercek admin-sba-8f92 / Supabase kontrolu)
    const juce::Rectangle<float> updRect (padX, kInfoY + 40.0f, innerW, 23.0f);
    const bool updHov = (hoverUI == "info-update");
    const bool accentPill = (updateState == UpdateState::UpToDate || updateState == UpdateState::UpdateAvailable);
    const bool solidAccent = (updateState == UpdateState::UpdateAvailable);

    if (solidAccent)
    {
        g.setColour (pal.contrast.withMultipliedAlpha (updHov ? 0.98f : 0.88f));
        g.fillRoundedRectangle (updRect, 11.5f);
        g.setColour (pal.paper.withMultipliedAlpha (0.98f));
    }
    else
    {
        g.setColour ((accentPill ? pal.contrast : pal.ink).withMultipliedAlpha (updHov ? 0.15f : 0.07f));
        g.fillRoundedRectangle (updRect, 11.5f);
        g.setColour ((accentPill ? pal.contrast : pal.ink).withMultipliedAlpha (accentPill ? 0.94f : 0.82f));
    }

    juce::String updLabel = "Check version";
    switch (updateState)
    {
        case UpdateState::Checking:        updLabel = "Checking..."; break;
        case UpdateState::UpToDate:        updLabel = "Up to date"; break;
        case UpdateState::UpdateAvailable: updLabel = "Update v" + latestVersionStr; break;
        case UpdateState::Error:           updLabel = "Check failed"; break;
        default: break;
    }
    g.setFont (makeUiFont (11.0f, true));
    g.drawText (updLabel, updRect, juce::Justification::centred);

    if (processor.isLicensed())
    {
        g.setColour (pal.contrast.withMultipliedAlpha (0.92f));
        g.fillEllipse (padX + 1.0f, kInfoY + 73.5f, 5.0f, 5.0f);

        const auto fullKey = processor.getLicenseKey();
        const auto tail = fullKey.length() >= 19 ? fullKey.substring (15) : fullKey;
        g.setColour (pal.ink.withMultipliedAlpha (0.68f));
        g.setFont (makeUiFont (10.5f, false));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Licensed  \xc2\xb7  ")) + tail,
                    juce::Rectangle<float> (padX + 10.0f, kInfoY + 67.0f, innerW - 10.0f, 18.0f),
                    juce::Justification::centredLeft);

        const juce::Rectangle<float> deactRect (padX, kInfoY + 87.0f, innerW, 21.0f);
        const bool dHov = (hoverUI == "info-deact");
        g.setColour (pal.ink.withMultipliedAlpha (dHov ? 0.14f : 0.06f));
        g.fillRoundedRectangle (deactRect, 10.5f);
        g.setColour (pal.ink.withMultipliedAlpha (dHov ? 0.88f : 0.62f));
        g.setFont (makeUiFont (10.5f, true));
        g.drawText ("Deactivate", deactRect, juce::Justification::centred);
    }
    else
    {
        g.setColour (pal.ink.withMultipliedAlpha (0.35f));
        g.fillEllipse (padX + 1.0f, kInfoY + 78.5f, 5.0f, 5.0f);

        g.setColour (pal.ink.withMultipliedAlpha (0.62f));
        g.setFont (makeUiFont (11.0f, false));
        g.drawText ("Unlicensed",
                    juce::Rectangle<float> (padX + 10.0f, kInfoY + 72.0f, innerW - 10.0f, 18.0f),
                    juce::Justification::centredLeft);
    }
}

void CrystalAudioProcessorEditor::renderBootLicenseGate (juce::Graphics& g, const CrystalDSP::Palette& pal) const
{
    const float a = clamp01 (licenseGateAlpha);
    if (a <= 0.01f) return;

    const float slideY = (1.0f - easeOutCubic (a)) * 10.0f;
    const float shake = (licenseErrorTimer > 0.0f) ? std::sin (licenseErrorTimer * 52.0f) * 4.5f * (licenseErrorTimer / 0.45f) : 0.0f;
    const bool err = licenseErrorTimer > 0.0f;
    const bool focused = licenseInput.hasKeyboardFocus (true);

    // Modern hyperminimalist kapsul (pill) lisans giris alani — Quicksand tam ortali!
    const juce::Rectangle<float> inputPill (62.0f + shake, 242.0f + slideY, 216.0f, 32.0f);
    g.setColour (pal.paper.interpolatedWith (pal.ink, focused ? 0.055f : 0.035f).withMultipliedAlpha (0.94f * a));
    g.fillRoundedRectangle (inputPill, 16.0f);

    const juce::Colour borderCol = err ? pal.contrast : pal.ink;
    const float borderAlpha = err ? 0.90f : (focused ? 0.48f : 0.20f);
    g.setColour (borderCol.withMultipliedAlpha (borderAlpha * a));
    g.drawRoundedRectangle (inputPill, 16.0f, err ? 1.4f : 1.05f);

    // Tam kapsul (neredeyse yuvarlak) Activate dugmesi — Quicksand SemiBold
    const juce::Rectangle<float> btnRect (118.0f, 286.0f + slideY, 104.0f, 30.0f);
    const bool hov = (hoverUI == "gate-act");
    const juce::Colour btnFill = hov ? pal.contrast : pal.ink;
    g.setColour (btnFill.withMultipliedAlpha ((hov ? 0.96f : 0.88f) * a));
    g.fillRoundedRectangle (btnRect, 15.0f);

    g.setColour (pal.paper.withMultipliedAlpha (0.98f * a));
    g.setFont (makeUiFont (12.5f, true));
    g.drawText ("Activate", btnRect, juce::Justification::centred);
}

void CrystalAudioProcessorEditor::renderPresetExplorer (juce::Graphics& g, const CrystalDSP::Palette& pal) const
{
    if (! explorerOpen) return;

    g.setColour (pal.paper.withMultipliedAlpha (0.96f));
    g.fillRect (kDrawerX, kDrawerY, kDrawerW, kDrawerH);
    drawInkStroke (g, { {kDrawerX, kDrawerY}, {kDrawerX + kDrawerW, kDrawerY},
                        {kDrawerX + kDrawerW, kDrawerY + kDrawerH}, {kDrawerX, kDrawerY + kDrawerH} },
                   1.9f, pal.ink, 0.85f, true, 777.0f, 0.35f);

    const auto& list = processor.getSavedPresets();
    const int curSeed = processor.getCurrentSeed();
    const int totalRows = static_cast<int> (list.size());
    const float maxScroll = getMaxExplorerScroll();
    const float scrollY = std::clamp (explorerScrollY, 0.0f, maxScroll);

    const float viewTop = kDrawerY + 8.0f;
    const float viewH   = kDrawerH - 16.0f;
    const float listPadR = (maxScroll > 1.0f) ? 8.0f : 0.0f;

    {
        juce::Graphics::ScopedSaveState clipSS (g);
        g.reduceClipRegion (juce::Rectangle<float> (kDrawerX + 4.0f, viewTop, kDrawerW - 8.0f, viewH).toNearestInt());

        for (int i = 0; i < totalRows; ++i)
        {
            const float ry = viewTop + 2.0f + i * kRowH - scrollY;
            if (ry + kRowH < viewTop - 2.0f || ry > viewTop + viewH + 2.0f) continue;

            const auto& p = list[static_cast<size_t> (i)];
            const float rcy = ry + kRowH * 0.5f;
            const bool isCur = (p.seed == curSeed);
            const bool isHov = (hoverUI == ("row-" + juce::String (i)));

            if (isCur || isHov)
            {
                g.setColour ((isCur ? pal.contrast : pal.ink).withMultipliedAlpha (isCur ? 0.13f : 0.06f));
                g.fillRect (kDrawerX + 8.0f, ry + 2.0f, kDrawerW - 16.0f - listPadR, kRowH - 4.0f);
            }

            // Her preset'in kendine bagli prosedurel paletinden minik renk noktasi!
            const auto pPal = CrystalDSP::makeProceduralPalette (p.seed);
            g.setColour (pPal.base);
            g.fillEllipse (kDrawerX + 18.0f, rcy - 3.5f, 7.0f, 7.0f);
            g.setColour (pPal.contrast);
            g.drawEllipse (kDrawerX + 18.0f, rcy - 3.5f, 7.0f, 7.0f, isCur ? 1.5f : 0.9f);

            draw5RuneCode (g, p.seed, kDrawerX + (kDrawerW - listPadR) * 0.5f - 2.0f, rcy, 5.1f, 19.5f,
                           isCur ? pal.contrast : pal.ink, isCur ? 1.0f : 0.85f);

            if (list.size() > 1)
            {
                const float dx = kDrawerX + kDrawerW - 22.0f - listPadR;
                g.setColour (pal.ink.withMultipliedAlpha (hoverUI == ("del-" + juce::String (i)) ? 0.95f : 0.28f));
                g.drawLine (dx - 3.2f, rcy - 3.2f, dx + 3.2f, rcy + 3.2f, 1.3f);
                g.drawLine (dx + 3.2f, rcy - 3.2f, dx - 3.2f, rcy + 3.2f, 1.3f);
            }

            if (i < totalRows - 1)
            {
                g.setColour (pal.ink.withMultipliedAlpha (0.10f));
                g.drawLine (kDrawerX + 16.0f, ry + kRowH, kDrawerX + kDrawerW - 16.0f - listPadR, ry + kRowH, 1.0f);
            }
        }
    }

    // Sinirsiz liste icin hyperminimalist ince scrollbar cubugu
    if (maxScroll > 1.0f)
    {
        const float contentH = static_cast<float> (totalRows) * kRowH + 4.0f;
        const float trackH = viewH - 6.0f;
        const float thumbH = std::max (22.0f, trackH * (viewH / contentH));
        const float thumbY = viewTop + 3.0f + (scrollY / maxScroll) * (trackH - thumbH);
        g.setColour (pal.ink.withMultipliedAlpha (0.28f));
        g.fillRoundedRectangle (kDrawerX + kDrawerW - 7.5f, thumbY, 2.5f, thumbH, 1.25f);
    }
}

void CrystalAudioProcessorEditor::paint (juce::Graphics& g)
{
    hasPaintedOnce = true;
    const bool unlicensed = ! processor.isLicensed();
    const int activeSeed = (unlicensed || (bootActive && bootTime < 2.14f)) ? bootVisualSeed : processor.getCurrentSeed();
    const auto pal = CrystalDSP::makeProceduralPalette (activeSeed);
    const auto hatchStyle = CrystalDSP::makeProceduralHatchStyle (activeSeed);
    minimalLnf.resizerInk = pal.ink;
    g.fillAll (pal.paper);

    // Pencere olceklemesi (her boyutta & Ableton Auto-Scale OFF durumunda ortali ve kesilmeden!)
    const float uiScale = getUiScale();
    const auto uiOff = getUiOffset();
    g.addTransform (juce::AffineTransform::scale (uiScale).translated (uiOff.x, uiOff.y));

    const float slowT = timeSec * 0.085f;
    const Quat qInv = qConj (q);

    // LISANSSIZ BOOT EKRANI: Once kristal ortada rastgele karilir, sonra altta modern lisans satiri gelir!
    if (unlicensed)
    {
        const float popIn = gateTime < 0.50f ? easeOutBack (clamp01 (gateTime / 0.50f), 1.55f) : 1.0f;
        renderMainShape (g, pal, slowT, 148.0f, popIn, bootVisualPoly, bootVisualOrg);
        if (licenseGateAlpha > 0.01f)
            renderBrainLogo (g, kLogoCx, kLogoCy, kLogoSize, pal.contrast, 0.94f * licenseGateAlpha, true);
        renderBootLicenseGate (g, pal);
        if (infoOpen) renderInfoDrawer (g, pal);

        if (paperTile.isValid())
        {
            g.setTiledImageFill (paperTile, 0, 0, 1.0f);
            g.fillAll();
        }
        return;
    }

    const bool inSoloPhase = bootActive && bootTime < 2.14f;
    const float org = inSoloPhase ? bootVisualOrg : processor.apvts.getRawParameterValue ("knob2")->load();
    const int polyOv = inSoloPhase ? bootVisualPoly : -1;
    const float orgOv = inSoloPhase ? bootVisualOrg : -1.0f;

    // Disney 12 Principles Boot Choreography (~3.85s):
    // 1. Squash & Stretch  2. Anticipation Dip  3. Arc & Overshoot  4. Overlapping Action & Follow-Through
    float ballCy = kBallCy;
    float ballScale = 1.0f;
    float ballSx = 1.0f, ballSy = 1.0f;
    float hatchAlpha = 1.0f;
    float seedYOff = 0.0f, seedAlpha = 1.0f;
    float scopeYOff = 0.0f, scopeAlpha = 1.0f;
    float knobYOff[3] { 0.0f, 0.0f, 0.0f };
    float knobAlpha[3] { 1.0f, 1.0f, 1.0f };
    float knobSx[3] { 1.0f, 1.0f, 1.0f };
    float knobSy[3] { 1.0f, 1.0f, 1.0f };

    if (bootActive)
    {
        const float bt = bootTime;
        const float centerY = kH * 0.5f;

        if (bt < 0.48f)
        {
            // Pop-in: hacim koruyan stretch -> impact squash -> settle
            const float u = clamp01 (bt / 0.48f);
            ballScale = easeOutBack (u, 1.65f);
            const float ss = std::sin (u * kPiF * 2.0f) * (1.0f - u) * 0.16f;
            ballSx = 1.0f - ss;
            ballSy = 1.0f + ss;
            ballCy = centerY;
        }
        else if (bt < 1.92f)
        {
            // 4 solo shuffle sirasinda her snapPop'ta hafif squash & stretch
            ballCy = centerY;
            ballSx = 1.0f + snapPop * 0.09f;
            ballSy = 1.0f - snapPop * 0.08f;
        }
        else if (bt < 2.14f)
        {
            // DISNEY ANTICIPATION: Yukari firlamadan once asagi +10px cokup yatayda ezilir!
            const float u = easeInOutCubic ((bt - 1.92f) / 0.22f);
            ballCy = centerY + u * 10.0f;
            ballSx = 1.0f + u * 0.12f;
            ballSy = 1.0f - u * 0.11f;
        }
        else if (bt < 2.66f)
        {
            // LAUNCH + OVERSHOOT + IMPACT SQUASH: Dikey esneyerek yukari cikar, hafif asip yerine oturur!
            const float u = clamp01 ((bt - 2.14f) / 0.52f);
            const float yCurve = easeOutBack (u, 1.45f);
            ballCy = (centerY + 10.0f) + (kBallCy - (centerY + 10.0f)) * yCurve;
            if (u < 0.55f)
            {
                const float st = std::sin ((u / 0.55f) * kPiF);
                ballSx = 1.0f - st * 0.11f;
                ballSy = 1.0f + st * 0.13f;
            }
            else
            {
                const float sq = std::sin (((u - 0.55f) / 0.45f) * kPiF);
                ballSx = 1.0f + sq * 0.08f;
                ballSy = 1.0f - sq * 0.07f;
            }
        }
        else
        {
            ballCy = kBallCy;
        }

        // Overlapping Action: Hatch, kristal yerine otururken disari dogru acilir
        hatchAlpha = easeOutCubic ((bt - 2.48f) / 0.44f);

        auto disneyDrop = [] (float tNow, float tStart, float dur, float dropPx,
                              float& outY, float& outA, float& outSx, float& outSy)
        {
            const float u = clamp01 ((tNow - tStart) / dur);
            outA = easeOutCubic (u * 1.35f);
            outY = (1.0f - easeOutBack (u, 1.75f)) * (-dropPx);
            if (u > 0.01f && u < 0.52f)
            {
                // Duserken dikeyde esner (Stretch)
                const float st = std::sin ((u / 0.52f) * kPiF);
                outSx = 1.0f - st * 0.09f;
                outSy = 1.0f + st * 0.11f;
            }
            else if (u >= 0.52f && u < 0.98f)
            {
                // Yere/slotuna carptiginda yatayda ezilip toplanir (Squash & Settle)
                const float sq = std::sin (((u - 0.52f) / 0.46f) * kPiF);
                outSx = 1.0f + sq * 0.11f;
                outSy = 1.0f - sq * 0.10f;
            }
        };

        float dummySx = 1.0f, dummySy = 1.0f;
        disneyDrop (bt, 2.62f, 0.42f, 20.0f, seedYOff,    seedAlpha,    dummySx,   dummySy);
        disneyDrop (bt, 2.86f, 0.42f, 22.0f, scopeYOff,   scopeAlpha,   dummySx,   dummySy);
        disneyDrop (bt, 3.08f, 0.44f, 28.0f, knobYOff[0], knobAlpha[0], knobSx[0], knobSy[0]);
        disneyDrop (bt, 3.20f, 0.44f, 28.0f, knobYOff[1], knobAlpha[1], knobSx[1], knobSy[1]);
        disneyDrop (bt, 3.32f, 0.44f, 28.0f, knobYOff[2], knobAlpha[2], knobSx[2], knobSy[2]);
    }

    std::vector<float> orgScales (84, 1.0f);
    if (org > 0.002f)
    {
        for (int i = 0; i < 84; ++i)
        {
            const float a = (static_cast<float> (i) / 84.0f) * kTwoPiF;
            const Vec3 d = qRotateVec (qInv, { std::cos (a), -std::sin (a), 0.0f });
            orgScales[static_cast<size_t> (i)] = organicScale3D (d.x, d.y, d.z, org, slowT);
        }
    }

    const auto visualFx = CrystalDSP::makeProceduralVisualFx (activeSeed);

    // -------------------------------------------------------------------------
    // HATCH KATMANI: 1/6 olasilikla hareketli cizgilerin uzerine PIXELATE SHADER!
    // -------------------------------------------------------------------------
    if (visualFx == CrystalDSP::VisualFx::HatchPixelate)
    {
        constexpr int kBuf = 48;
        constexpr float kSpan = 192.0f; // 4x4 logical px keskin piksel shader bloklari
        const float ox = kBallCx - kSpan * 0.5f;
        const float oy = ballCy  - kSpan * 0.5f;

        juce::Image hatchBuf (juce::Image::ARGB, kBuf, kBuf, true, juce::SoftwareImageType());
        {
            juce::Graphics hg (hatchBuf);
            hg.addTransform (juce::AffineTransform::translation (-ox, -oy).scaled (static_cast<float> (kBuf) / kSpan));
            renderParallelHatch (hg, orgScales, pal, ballCy, hatchAlpha, hatchStyle, slowT, polyOv, orgOv);
        }

        // Keskin piksel shader kuantalama gecisi (Windows + macOS CoreGraphics %100 guvenli)
        {
            juce::Image::BitmapData bd (hatchBuf, juce::Image::BitmapData::readWrite);
            const juce::Colour colFull = pal.contrast.withMultipliedAlpha (0.92f * hatchAlpha);
            const juce::Colour colMid  = pal.contrast.withMultipliedAlpha (0.50f * hatchAlpha);
            const juce::Colour colZero (0x00000000u);

            for (int y = 0; y < kBuf; ++y)
            {
                for (int x = 0; x < kBuf; ++x)
                {
                    const uint8_t a = bd.getPixelColour (x, y).getAlpha();
                    if (a < 42)       bd.setPixelColour (x, y, colZero);
                    else if (a < 138) bd.setPixelColour (x, y, colMid);
                    else              bd.setPixelColour (x, y, colFull);
                }
            }
        }

        juce::Graphics::ScopedSaveState ss (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (hatchBuf, juce::Rectangle<float> (ox, oy, kSpan, kSpan));
    }
    else
    {
        renderParallelHatch (g, orgScales, pal, ballCy, hatchAlpha, hatchStyle, slowT, polyOv, orgOv);
    }

    // -------------------------------------------------------------------------
    // KRISTAL KATMANI: 1/18 Bayer Dither shader + Disney Squash & Stretch!
    // -------------------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState ballSS (g);
        if (std::abs (ballSx - 1.0f) > 0.002f || std::abs (ballSy - 1.0f) > 0.002f)
            g.addTransform (juce::AffineTransform::scale (ballSx, ballSy, kBallCx, ballCy));

        if (visualFx == CrystalDSP::VisualFx::CrystalDither)
        {
            static constexpr uint8_t kBayer8[8][8] = {
                {  0, 32,  8, 40,  2, 34, 10, 42 },
                { 48, 16, 56, 24, 50, 18, 58, 26 },
                { 12, 44,  4, 36, 14, 46,  6, 38 },
                { 60, 28, 52, 20, 62, 30, 54, 22 },
                {  3, 35, 11, 43,  1, 33,  9, 41 },
                { 51, 19, 59, 27, 49, 17, 57, 25 },
                { 15, 47,  7, 39, 13, 45,  5, 37 },
                { 63, 31, 55, 23, 61, 29, 53, 21 }
            };

            constexpr int kBuf = 76;
            constexpr float kSpan = 152.0f;
            const float ox = kBallCx - kSpan * 0.5f;
            const float oy = ballCy  - kSpan * 0.5f;

            juce::Image crysBuf (juce::Image::ARGB, kBuf, kBuf, true, juce::SoftwareImageType());
            {
                juce::Graphics cg (crysBuf);
                cg.addTransform (juce::AffineTransform::translation (-ox, -oy).scaled (static_cast<float> (kBuf) / kSpan));
                renderMainShape (cg, pal, slowT, ballCy, ballScale, polyOv, orgOv);
            }

            {
                juce::Image::BitmapData bd (crysBuf, juce::Image::BitmapData::readWrite);
                const juce::Colour colZero (0x00000000u);
                const float lBase = pal.base.getPerceivedBrightness();
                const float lShad = pal.shadow.getPerceivedBrightness();
                const float lo = std::min (lShad, lBase) * 0.62f;
                const float hi = std::max (lShad, lBase) * 1.05f;
                const float spanL = std::max (0.18f, hi - lo);

                for (int y = 0; y < kBuf; ++y)
                {
                    for (int x = 0; x < kBuf; ++x)
                    {
                        const auto col = bd.getPixelColour (x, y);
                        const uint8_t a = col.getAlpha();
                        if (a < 28) { bd.setPixelColour (x, y, colZero); continue; }

                        const float af = static_cast<float> (a) / 255.0f;
                        const float lum = (0.299f * static_cast<float> (col.getRed())
                                         + 0.587f * static_cast<float> (col.getGreen())
                                         + 0.114f * static_cast<float> (col.getBlue())) / 255.0f;

                        if (lum < 0.22f)
                        {
                            bd.setPixelColour (x, y, pal.ink.withMultipliedAlpha (af));
                        }
                        else
                        {
                            const float nx = (static_cast<float> (x) - kBuf * 0.5f) / (kBuf * 0.34f);
                            const float ny = (static_cast<float> (y) - kBuf * 0.5f) / (kBuf * 0.34f);
                            const float sphereGrad = std::clamp (0.76f - nx * 0.22f - ny * 0.24f, 0.18f, 1.0f);
                            const float normTone = std::clamp (((lum - lo) / spanL) * 0.62f + sphereGrad * 0.42f, 0.0f, 1.0f);
                            const float thr = (static_cast<float> (kBayer8[y & 7][x & 7]) + 0.5f) / 64.0f;
                            const juce::Colour outCol = (normTone > thr) ? pal.base : pal.ink;
                            bd.setPixelColour (x, y, outCol.withMultipliedAlpha (af));
                        }
                    }
                }
            }

            juce::Graphics::ScopedSaveState ss (g);
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImage (crysBuf, juce::Rectangle<float> (ox, oy, kSpan, kSpan));
        }
        else
        {
            renderMainShape (g, pal, slowT, ballCy, ballScale, polyOv, orgOv);
        }
    }
    renderDistortionScope (g, pal, scopeYOff, scopeAlpha);
    renderOrganicKnobs (g, pal, knobYOff, knobAlpha, knobSx, knobSy);
    renderTopSeedBar (g, pal, seedYOff, seedAlpha);
    if (! bootActive)
    {
        renderPresetExplorer (g, pal);
        renderInfoDrawer (g, pal);
    }

    if (paperTile.isValid())
    {
        g.setTiledImageFill (paperTile, 0, 0, 1.0f);
        g.fillAll();
    }
}

// =============================================================================
// INTERACTION & ANIMATION TIMER
// =============================================================================
int CrystalAudioProcessorEditor::hitKnob (juce::Point<float> pt) const noexcept
{
    for (int i = 0; i < 3; ++i)
        if (std::hypot (pt.x - kKnobX[i], pt.y - kKnobY) <= kKnobR + 9.0f) return i;
    return -1;
}

bool CrystalAudioProcessorEditor::hitBallZone (juce::Point<float> pt) const noexcept
{
    return pt.y > 46.0f && pt.y < (kWaveCy - kWaveHalfH - 10.0f);
}

juce::String CrystalAudioProcessorEditor::hitTopOrDrawer (juce::Point<float> pt) const
{
    if (processor.isLicensed() || licenseGateAlpha > 0.25f)
        if (std::hypot (pt.x - kLogoCx, pt.y - kLogoCy) <= 16.0f) return "logo";

    if (infoOpen)
    {
        if (pt.x >= kInfoX && pt.x <= kInfoX + kInfoW && pt.y >= kInfoY && pt.y <= kInfoY + kInfoH)
        {
            const float padX = kInfoX + 12.0f;
            const float innerW = kInfoW - 24.0f;
            if (juce::Rectangle<float> (padX, kInfoY + 39.0f, innerW, 22.0f).contains (pt)) return "info-update";
            if (processor.isLicensed() && juce::Rectangle<float> (padX, kInfoY + 85.0f, innerW, 20.0f).contains (pt)) return "info-deact";
            return "drawer-bg";
        }
        return "drawer-out";
    }

    if (! processor.isLicensed())
    {
        if (licenseGateAlpha > 0.25f && juce::Rectangle<float> (118.0f, 286.0f, 104.0f, 30.0f).contains (pt))
            return "gate-act";
        return {};
    }

    if (std::hypot (pt.x - kSaveX, pt.y - kSaveY) <= kBtnR) return "save";
    if (std::abs (pt.x - kSeedCx) <= kSeedW * 0.5f && std::abs (pt.y - kSeedCy) <= 15.0f) return "seed";
    if (explorerOpen)
    {
        if (pt.x >= kDrawerX && pt.x <= kDrawerX + kDrawerW && pt.y >= kDrawerY && pt.y <= kDrawerY + kDrawerH)
        {
            const float viewTop = kDrawerY + 8.0f;
            const float viewBot = kDrawerY + kDrawerH - 8.0f;
            if (pt.y >= viewTop && pt.y <= viewBot)
            {
                const int totalRows = static_cast<int> (processor.getSavedPresets().size());
                const float maxScroll = getMaxExplorerScroll();
                const float scrollY = std::clamp (explorerScrollY, 0.0f, maxScroll);
                const float listPadR = (maxScroll > 1.0f) ? 8.0f : 0.0f;
                for (int i = 0; i < totalRows; ++i)
                {
                    const float ry = viewTop + 2.0f + i * kRowH - scrollY;
                    if (pt.y >= ry && pt.y <= ry + kRowH)
                    {
                        if (processor.getSavedPresets().size() > 1 && pt.x >= kDrawerX + kDrawerW - 34.0f - listPadR)
                            return "del-" + juce::String (i);
                        return "row-" + juce::String (i);
                    }
                }
            }
            return "drawer-bg";
        }
        return "drawer-out";
    }
    return {};
}

float CrystalAudioProcessorEditor::getMaxExplorerScroll() const noexcept
{
    const float contentH = static_cast<float> (processor.getSavedPresets().size()) * kRowH + 4.0f;
    const float viewH = kDrawerH - 16.0f;
    return std::max (0.0f, contentH - viewH);
}

void CrystalAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! explorerOpen) return;
    const auto pt = toLogical (e.position);
    if (pt.x >= kDrawerX && pt.x <= kDrawerX + kDrawerW && pt.y >= kDrawerY && pt.y <= kDrawerY + kDrawerH)
    {
        const float maxScroll = getMaxExplorerScroll();
        if (maxScroll > 0.0f)
        {
            explorerScrollY = std::clamp (explorerScrollY - wheel.deltaY * 96.0f, 0.0f, maxScroll);
            hoverUI = hitTopOrDrawer (pt);
            repaint();
        }
    }
}

void CrystalAudioProcessorEditor::setHostParam (const char* id, float val)
{
    if (auto* p = processor.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (val));
}

void CrystalAudioProcessorEditor::requestThrottledRepaint()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastRepaintMs >= 76) // <= 13 FPS kesin sinir!
    {
        lastRepaintMs = now;
        repaint();
    }
}

void CrystalAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    bootWaitingForShow = false;
    if (bootActive) { bootActive = false; repaint(); return; }
    const auto pt = toLogical (e.position);
    const auto ui = hitTopOrDrawer (pt);
    if (ui.isNotEmpty())
    {
        if (ui == "logo")
        {
            infoOpen = ! infoOpen;
            if (infoOpen) explorerOpen = false;
            processor.triggerMechanicalClick (1);
            updateLicenseInputLayout();
        }
        else if (ui == "info-update")
        {
            processor.triggerMechanicalClick (1);
            if (updateState == UpdateState::UpdateAvailable && CrystalLicense::isOwnDownloadPage (updateDownloadUrl))
                juce::URL (updateDownloadUrl).launchInDefaultBrowser();
            else
                startVersionCheck();
        }
        else if (ui == "info-deact")
        {
            processor.deactivateLicense();
            infoOpen = false;
            gateTime = 2.05f;
            gateShuffleStage = 4;
            licenseGateAlpha = 1.0f;
            bootVisualSeed = processor.getCurrentSeed();
            bootVisualPoly = 0;
            bootVisualOrg = 0.12f;
            updateLicenseInputLayout();
        }
        else if (ui == "gate-act") { submitLicenseKey(); return; }
        else if (ui == "save") { processor.toggleSaveCurrentPreset(); }
        else if (ui == "seed")
        {
            explorerOpen = ! explorerOpen;
            if (explorerOpen) { infoOpen = false; explorerScrollY = std::clamp (explorerScrollY, 0.0f, getMaxExplorerScroll()); }
            processor.triggerMechanicalClick (1);
        }
        else if (ui == "drawer-out") { explorerOpen = false; infoOpen = false; updateLicenseInputLayout(); }
        else if (ui.startsWith ("del-"))
        {
            processor.deletePresetAt (ui.substring (4).getIntValue());
            explorerScrollY = std::clamp (explorerScrollY, 0.0f, getMaxExplorerScroll());
        }
        else if (ui.startsWith ("row-"))
        {
            const int idx = ui.substring (4).getIntValue();
            const auto& list = processor.getSavedPresets();
            if (idx >= 0 && idx < static_cast<int> (list.size()))
            {
                processor.applyPreset (list[static_cast<size_t> (idx)], true);
                triggerKnobSnapAnimation (false);
                processor.triggerMechanicalClick (1);
                snapPop = 0.85f; sketchSeed++;
                explorerOpen = false;
            }
        }
        repaint();
        return;
    }

    if (! processor.isLicensed()) return;

    const int kIdx = hitKnob (pt);
    if (kIdx != -1)
    {
        static constexpr const char* kIds[3] = { "knob0", "knob1", "knob2" };
        activeKnob = kIdx;
        knobStartY = pt.y;
        knobStartVal = *processor.apvts.getRawParameterValue (kIds[kIdx]);
        repaint();
        return;
    }

    if (hitBallZone (pt))
    {
        holdingBall = true;
        lastMouse = pt;
        lastDragMs = juce::Time::getMillisecondCounter();
        // Eliyle agir donen kureyi yakalama hissi: mevcut hizi yumusakca durdur
        yawVel *= 0.20f;
        pitchVel *= 0.20f;
        dragYawVel = 0.0f;
        dragPitchVel = 0.0f;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
}

void CrystalAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto pt = toLogical (e.position);
    if (activeKnob != -1)
    {
        static constexpr const char* kIds[3] = { "knob0", "knob1", "knob2" };
        const float nextVal = std::clamp (knobStartVal + (knobStartY - pt.y) / 110.0f, 0.0f, 1.0f);
        setHostParam (kIds[activeKnob], nextVal);
        knobDispVal[activeKnob] = nextVal;
        knobStepDelay[activeKnob] = 0;
        knobSeeds[activeKnob] += 0.22f;
        if (activeKnob == 1)
        {
            const int pv = std::min (3, static_cast<int> (nextVal * 3.999f));
            if (pv != lastPolyVis)
            {
                const float dir = (pv > lastPolyVis) ? 1.0f : -1.0f;
                processor.triggerMechanicalClick (pv > lastPolyVis ? 1 : -1);
                lastPolyVis = pv; snapPop = 0.85f; sketchSeed++;
                yawVel += dir * 2.1f;
                pitchVel += dir * 0.65f;
            }
        }
        requestThrottledRepaint();
        return;
    }

    if (holdingBall)
    {
        const auto nowMs = juce::Time::getMillisecondCounter();
        const float dragDt = std::clamp (static_cast<float> (nowMs - lastDragMs) * 0.001f, 0.008f, 0.12f);
        lastDragMs = nowMs;

        const float dx = pt.x - lastMouse.x, dy = pt.y - lastMouse.y;
        lastMouse = pt;
        const float curDrive = *processor.apvts.getRawParameterValue ("drive");
        const float curTilt  = *processor.apvts.getRawParameterValue ("tilt");
        setHostParam ("drive", std::clamp (curDrive + dx * 0.0055f, 0.0f, 1.0f));
        setHostParam ("tilt",  std::clamp (curTilt  - dy * 0.0075f, -1.0f, 1.0f));

        // Agir tas / monolit kutle hissi: anlik hareket + biriken atalet momentumu!
        const float ay = dx * 0.0135f, ax = dy * 0.0110f;
        const float instYaw   = std::clamp (ay / dragDt, -6.5f, 6.5f);
        const float instPitch = std::clamp (ax / dragDt, -5.5f, 5.5f);
        dragYawVel   += (instYaw   - dragYawVel)   * 0.42f;
        dragPitchVel += (instPitch - dragPitchVel) * 0.42f;

        q = qNorm (qMul (qMul (qFromAxisAngle (0, 1, 0, ay), qFromAxisAngle (1, 0, 0, ax)), q));
        accumAngle += std::hypot (ax, ay);
        if (accumAngle >= 0.075f) { accumAngle = 0.0f; sketchSeed++; }
        requestThrottledRepaint();
    }
}

void CrystalAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    if (holdingBall)
    {
        // Birakinca biriken agir atalet momentumunu kristale aktar (son hareket eskiyse durulmus olur)
        const auto ageMs = juce::Time::getMillisecondCounter() - lastDragMs;
        const float carry = (ageMs < 90) ? 0.85f : (ageMs < 180 ? 0.35f : 0.0f);
        yawVel   = std::clamp (dragYawVel * carry,   -5.2f, 5.2f);
        pitchVel = std::clamp (dragPitchVel * carry, -4.2f, 4.2f);
    }
    holdingBall = false;
    activeKnob = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    requestThrottledRepaint();
}

void CrystalAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto pt = toLogical (e.position);
    mouseLogical = pt;
    const auto ui = hitTopOrDrawer (pt);
    const int kh = (processor.isLicensed() && ! explorerOpen && ! infoOpen && ui.isEmpty()) ? hitKnob (pt) : -1;

    if (kh != -1) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (ui.isNotEmpty() && ui != "drawer-bg" && ui != "drawer-out") setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (processor.isLicensed() && ! explorerOpen && ! infoOpen && hitBallZone (pt)) setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else setMouseCursor (juce::MouseCursor::NormalCursor);

    if (kh != hoverKnob || ui != hoverUI || std::hypot (pt.x - kLogoCx, pt.y - kLogoCy) <= 34.0f)
    {
        hoverKnob = kh;
        hoverUI = ui;
        requestThrottledRepaint();
    }
}

void CrystalAudioProcessorEditor::mouseExit (const juce::MouseEvent&)
{
    mouseLogical = { -100.0f, -100.0f };
    hoverKnob = -1;
    hoverUI.clear();
    requestThrottledRepaint();
}

void CrystalAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! processor.isLicensed() || explorerOpen || infoOpen) return;
    const auto pt = toLogical (e.position);
    if (hitKnob (pt) != -1 || ! hitBallZone (pt)) return;
    if (std::hypot (pt.x - kBallCx, pt.y - kBallCy) <= kBallR * 1.9f)
    {
        processor.mutateChain();
        triggerKnobSnapAnimation (false);
        lastPolyVis = std::min (3, static_cast<int> ((*processor.apvts.getRawParameterValue ("knob1")) * 3.999f));
        yawVel = (yawVel >= 0.0f ? 1.0f : -1.0f) * 3.4f;
        pitchVel = (bootRng.nextFloat() - 0.5f) * 2.0f;
        snapPop = 1.0f; sketchSeed++;
        for (auto& ks : knobSeeds) ks += 1.0f;
        repaint();
    }
}

void CrystalAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 13.0f; // 13 FPS sabit adim

    // Boot animasyonu, pencere gercekten ekrana gelmeden (Windows + macOS) baslamasin!
    // Fail-safe: isShowing() aktifken 1.5s icinde her kosulda baslat (asla takili kalmaz).
    if (bootWaitingForShow)
    {
        const bool showing = isShowing() && getWidth() > 0 && getHeight() > 0;
        bool notMinimised = true;
        if (auto* peer = getPeer())
            notMinimised = ! peer->isMinimised();

        if (showing)
        {
            bootWaitGuardSec += dt;
            if ((notMinimised && hasPaintedOnce) || bootWaitGuardSec >= 1.5f)
            {
                bootWaitingForShow = false;
            }
            else
            {
                repaint();
                return;
            }
        }
        else
        {
            return;
        }
    }

    timeSec += dt;

    // Pencere ekranda gorunur olduktan sonra kisa surede Ableton'in DPI attach kucultmesini duzelt!
    if (isShowing() && hasPaintedOnce)
    {
        if (openStableTicks < 10)
            ++openStableTicks;

        if (! restoredSizeAfterAttach && openStableTicks == 2)
        {
            restoredSizeAfterAttach = true;
            const int wantW = std::clamp (processor.lastEditorW, 238, 1360);
            const int wantH = std::clamp (juce::roundToInt (static_cast<double> (wantW) * static_cast<double> (kH) / static_cast<double> (kW)), 269, 1536);
            if (std::abs (getWidth() - wantW) >= 4)
                setSize (wantW, wantH);
        }
    }

    // Pixel-perfect 4-lob logo hover tonu (wiggle yok, sadece uzerine gelinen lobun tonu degisir!)
    float targetLobe[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    const float lu = (mouseLogical.x - (kLogoCx - kLogoSize * 0.5f)) / kLogoSize;
    const float lv = (mouseLogical.y - (kLogoCy - kLogoSize * 0.5f)) / kLogoSize;
    if (lu >= 0.0f && lu <= 1.0f && lv >= 0.0f && lv <= 1.0f)
    {
        constexpr int N = CrystalLogo::kSize;
        const int gx = std::clamp (static_cast<int> (lu * N), 0, N - 1);
        const int gy = std::clamp (static_cast<int> (lv * N), 0, N - 1);
        for (int lobe = 0; lobe < CrystalLogo::kNumLobes; ++lobe)
        {
            uint8_t maxA = 0;
            for (int dy = -2; dy <= 2; ++dy)
            {
                const int sy = std::clamp (gy + dy, 0, N - 1);
                for (int dx = -2; dx <= 2; ++dx)
                {
                    const int sx = std::clamp (gx + dx, 0, N - 1);
                    maxA = std::max (maxA, CrystalLogo::kLobeAlpha[lobe][sy * N + sx]);
                }
            }
            targetLobe[lobe] = (maxA > 24) ? 1.0f : 0.22f;
        }
    }
    for (int i = 0; i < 4; ++i)
        lobeHover[i] += (targetLobe[i] - lobeHover[i]) * 0.38f;

    if (! processor.isLicensed())
    {
        gateTime += dt;
        // Once kristal ortada 4 kez rastgele karilir, sonra altta lisans satiri acilir!
        static constexpr float kGateShuffleTimes[4] = { 0.55f, 0.92f, 1.30f, 1.68f };
        if (gateShuffleStage < 4 && gateTime >= kGateShuffleTimes[gateShuffleStage])
        {
            if (gateShuffleStage < 3)
            {
                bootVisualSeed = bootRng.nextInt (32768);
                bootVisualPoly = (gateShuffleStage + 1) % 4;
                bootVisualOrg  = 0.14f + bootRng.nextFloat() * 0.65f;
            }
            else
            {
                bootVisualSeed = processor.getCurrentSeed();
                bootVisualPoly = 0;
                bootVisualOrg  = 0.12f;
            }
            snapPop = 1.0f;
            yawVel  = (gateShuffleStage % 2 == 0 ? 1.0f : -1.0f) * 2.8f;
            pitchVel = (bootRng.nextFloat() - 0.5f) * 2.0f;
            sketchSeed++;
            processor.triggerMechanicalClick (1);
            ++gateShuffleStage;
        }

        if (gateTime >= 2.00f)
        {
            const float prevA = licenseGateAlpha;
            licenseGateAlpha = clamp01 (licenseGateAlpha + dt * 2.4f);
            if (std::abs (licenseGateAlpha - prevA) > 0.001f || licenseErrorTimer > 0.0f)
                updateLicenseInputLayout();
        }
        if (licenseErrorTimer > 0.0f)
        {
            licenseErrorTimer = std::max (0.0f, licenseErrorTimer - dt);
            updateLicenseInputLayout();
        }
    }
    else if (licenseErrorTimer > 0.0f)
    {
        licenseErrorTimer = std::max (0.0f, licenseErrorTimer - dt);
    }

    if (bootActive)
    {
        bootTime += dt;

        // 4 snappy solo crystal shuffles while centered (0.58s, 0.94s, 1.30s, 1.66s)
        static constexpr float kShuffleTimes[4] = { 0.58f, 0.94f, 1.30f, 1.66f };
        if (bootShuffleStage < 4 && bootTime >= kShuffleTimes[bootShuffleStage])
        {
            if (bootShuffleStage < 3)
            {
                bootVisualSeed = bootRng.nextInt (32768);
                bootVisualPoly = (bootShuffleStage + 1) % 4;
                bootVisualOrg  = 0.15f + bootRng.nextFloat() * 0.70f;
            }
            else
            {
                bootVisualSeed = processor.getCurrentSeed();
                bootVisualPoly = std::min (3, static_cast<int> ((*processor.apvts.getRawParameterValue ("knob1")) * 3.999f));
                bootVisualOrg  = *processor.apvts.getRawParameterValue ("knob2");
            }
            snapPop = 1.0f;
            yawVel  = (bootShuffleStage % 2 == 0 ? 1.0f : -1.0f) * 2.8f;
            pitchVel = (bootRng.nextFloat() - 0.5f) * 2.2f;
            sketchSeed++;
            processor.triggerMechanicalClick (1);
            ++bootShuffleStage;
        }

        // Overlapping Action: Knoblar inerken follow-through %15 snap baslasin!
        if (bootLegoClickStage == 0 && bootTime >= 3.12f)
        {
            triggerKnobSnapAnimation (true);
            processor.triggerMechanicalClick (1);
            bootLegoClickStage = 1;
        }
        if (bootLegoClickStage == 1 && bootTime >= 3.44f)
        {
            processor.triggerMechanicalClick (1);
            bootLegoClickStage = 2;
        }

        if (bootTime >= 3.92f)
            bootActive = false;
    }

    if (snapPop > 0.005f) snapPop *= std::pow (0.76f, dt * 60.0f);
    else snapPop = 0.0f;

    // Agir, sert ve snappy mekanik knob gecisi (squash yok — 1-2 net detent tik!)
    static constexpr const char* kKnobIds[3] = { "knob0", "knob1", "knob2" };
    for (int i = 0; i < 3; ++i)
    {
        if (activeKnob == i) continue;
        if (bootActive && bootTime < 3.08f) continue;

        const float target = *processor.apvts.getRawParameterValue (kKnobIds[i]);
        if (knobStepDelay[i] > 0)
        {
            --knobStepDelay[i];
            continue;
        }

        const float diff = target - knobDispVal[i];
        const float adiff = std::abs (diff);
        if (adiff > 0.004f)
        {
            if (adiff > 0.28f)
            {
                // Buyuk mesafede once tek sert darbede %62 yol al -> sonraki karede hedefe kilitlen!
                const float jump = std::max (0.34f, adiff * 0.62f);
                knobDispVal[i] += (diff > 0.0f ? jump : -jump);
                knobSeeds[i] += 0.55f;
            }
            else
            {
                knobDispVal[i] = target;
                knobSeeds[i] += 0.45f;
            }
        }
    }

    if (! holdingBall)
    {
        // Agir, oturakli volan (flywheel) ataleti: sakin 0.26 rad/s rölanti + yavas viskoz sönüm!
        float targetYaw = 0.26f;
        float targetPitch = 0.0f;

        // Imlec kristalin ustundeyken cok ince, oturakli manyetik tepki (interaktif varlik hissi)
        if (processor.isLicensed() && ! explorerOpen && ! infoOpen && ! bootActive && hitBallZone (mouseLogical))
        {
            const float mdx = (mouseLogical.x - kBallCx) / 95.0f;
            const float mdy = (mouseLogical.y - kBallCy) / 95.0f;
            const float r2 = mdx * mdx + mdy * mdy;
            if (r2 < 1.1f)
            {
                const float w = 1.0f - std::min (1.0f, r2);
                targetYaw   += mdx * 0.55f * w;
                targetPitch += mdy * 0.42f * w;
            }
        }

        // Agir kutle sönümü: itilen momentum 2-2.5 saniye boyunca oturakli sekilde suzulur
        yawVel   += (targetYaw   - yawVel)   * std::min (1.0f, dt * 1.05f);
        pitchVel += (targetPitch - pitchVel) * std::min (1.0f, dt * 1.35f);

        const float ay = yawVel * dt, ax = pitchVel * dt;
        q = qNorm (qMul (qMul (qFromAxisAngle (0, 1, 0, ay), qFromAxisAngle (1, 0, 0, ax)), q));
        accumAngle += std::hypot (ax, ay);
        if (accumAngle >= 0.085f) { accumAngle = 0.0f; sketchSeed++; }
    }

    // ~6.5 fps hand-drawn marker boil + sakin ~2.2 fps hatch boil (13 FPS saatine gore)
    if (++boilTick >= 2)
    {
        boilTick = 0;
        const float k0 = *processor.apvts.getRawParameterValue ("knob0");
        barberScroll += (1.0f / 6.5f) * (12.0f + k0 * 18.0f);
        sketchSeed++;
    }
    if (++hatchTick >= 6)
    {
        hatchTick = 0;
        hatchSeed++;
    }

    lastRepaintMs = juce::Time::getMillisecondCounter();
    repaint();
}
