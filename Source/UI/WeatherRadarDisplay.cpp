#include "WeatherRadarDisplay.h"
#include "Fonts.h"
#include "../PluginProcessor.h"

namespace
{
constexpr auto radarBg       = 0xFF0A0A0A;
constexpr auto radarBezel    = 0xFF3A3A38;
constexpr auto ringColour    = 0xFF578879;
constexpr auto sweepColour   = 0x3300FF88;
constexpr auto textCyan      = 0xFF00FF66;
constexpr auto textDimGreen  = 0xFF00CC44;
constexpr auto textWhite     = 0xFFCCCCCC;
constexpr auto waypointColor = 0xFFCCCCCC;

constexpr float radarArcDeg  = 240.0f;   // total arc width in degrees
constexpr float radarArcRad  = radarArcDeg * juce::MathConstants<float>::pi / 180.0f;

static juce::Font getCockpitFont(float size, int style = juce::Font::plain)
{
    return WXFonts::get(size, style);
}

// 2D Perlin Noise helpers
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerp(float t, float a, float b) { return a + t * (b - a); }
inline float grad(int hash, float x, float y)
{
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

inline int hash2D(int x, int y, juce::uint32 seed)
{
    juce::uint32 h = seed ^ (x * 374761393U) ^ (y * 668265263U);
    h = (h ^ (h >> 13)) * 1274126177U;
    return (int)(h ^ (h >> 15));
}

float perlinNoise2D(float x, float y, juce::uint32 seed)
{
    int X = (int) std::floor(x);
    int Y = (int) std::floor(y);

    float fx = x - (float) X;
    float fy = y - (float) Y;

    float u = fade(fx);
    float v = fade(fy);

    int g00 = hash2D(X,     Y,     seed);
    int g10 = hash2D(X + 1, Y,     seed);
    int g01 = hash2D(X,     Y + 1, seed);
    int g11 = hash2D(X + 1, Y + 1, seed);

    float n00 = grad(g00, fx,        fy);
    float n10 = grad(g10, fx - 1.0f, fy);
    float n01 = grad(g01, fx,        fy - 1.0f);
    float n11 = grad(g11, fx - 1.0f, fy - 1.0f);

    float r1 = lerp(u, n00, n10);
    float r2 = lerp(u, n01, n11);
    return lerp(v, r1, r2);
}
}

static bool hasBootedInSession = false;

WeatherRadarDisplay::WeatherRadarDisplay(juce::AudioProcessorValueTreeState& apvtsIn, StableAliveAudioProcessor& processorIn, std::function<void()> doubleClickHandler)
    : apvts(apvtsIn), processor(processorIn), onDoubleClick(std::move(doubleClickHandler))
{
    if (hasBootedInSession)
        bootTimeSeconds = 3.5f; // Start immediately if already booted in this session
    else
        bootTimeSeconds = 0.0f;

    startTimerHz(40);
}

WeatherRadarDisplay::~WeatherRadarDisplay()
{
    stopTimer();
}

void WeatherRadarDisplay::timerCallback()
{
    const float dt = 0.025f; // 40 Hz frame step
    if (randomizeBlinkTime > 0.0f)
    {
        randomizeBlinkTime -= dt;
    }

    float lastBootTime = bootTimeSeconds;
    bootTimeSeconds += dt;

    if (bootTimeSeconds >= 3.2f)
    {
        hasBootedInSession = true;
        if (lastBootTime < 3.2f)
        {
            weatherImage.clear(weatherImage.getBounds(), juce::Colours::transparentBlack);
            sweepTime = -juce::MathConstants<float>::halfPi; // Start sweep at far-left!
            
            // Set initial angles to the far-left to avoid sweeping the entire left half on frame 1
            const float halfArc = radarArcRad * 0.5f;
            sweepAngle = -halfArc;
            lastSweepAngle = -halfArc;
        }

        lastSweepAngle = sweepAngle;

        // back-and-forth antenna sweep simulation (smooth 40 fps)
        sweepTime += 0.0075f;
        const float halfArc = radarArcRad * 0.5f;
        sweepAngle = std::sin(sweepTime) * halfArc;

        // Aircraft moving forward across field (smooth 40 fps)
        planeProgress += 0.0002f;

        // Handle note & ENVLOOP reactive pulse - subtle weather redistribution
        // No full-screen rebuild: the sweep beam draws perturbations smoothly as it sweeps!
        if (noteReactionPulsePtr != nullptr)
        {
            float p = noteReactionPulsePtr->load();
            if (p > 0.05f)
            {
                noteReactionPulsePtr->store(0.0f);
                float r = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
                noteIntensityOffset = r * 0.04f * p;
                planeProgress += (r * 0.0025f * p);

                // The offset decays to nothing in about a second, but the sweep
                // beam needs roughly twenty to cross the whole field - so on its
                // own it only ever tints the narrow slice the beam happens to be
                // on, which is invisible. Redraw the field so the perturbation
                // actually shows, throttled to a few times a second so holding
                // down a chord cannot flood the UI thread.
                juce::uint32 nowMs = juce::Time::getMillisecondCounter();
                if (nowMs - lastCutoffRebuildMs > 180)
                {
                    lastCutoffRebuildMs = nowMs;
                    rebuildWeatherImage();
                }
            }
        }

        // Cutoff updates: sweep beam naturally incorporates Cutoff smoothly
        float currentCutoff = 0.5f;
        if (auto* param = apvts.getParameter("CUTOFF"))
            currentCutoff = param->getValue();

        juce::uint32 nowMs = juce::Time::getMillisecondCounter();
        if (std::abs(currentCutoff - lastCutoffVal) > 0.06f && (nowMs - lastCutoffRebuildMs > 200))
        {
            lastCutoffVal = currentCutoff;
            lastCutoffRebuildMs = nowMs;
        }

        if (std::abs(noteIntensityOffset) > 0.001f)
        {
            noteIntensityOffset *= 0.92f;
            if (std::abs(noteIntensityOffset) <= 0.001f)
            {
                noteIntensityOffset = 0.0f;
            }
        }

        // Draw only the sector swept over in this step
        updateSweptWeather();
    }

    repaint();
}

void WeatherRadarDisplay::mouseDown(const juce::MouseEvent& event)
{
    float h = (float) getHeight();
    
    // Hit test for Unison Mode (2VU/5VU)
    juce::Rectangle<int> unisonBtnBounds(12, (int)(h - 26), 55, 20);
    if (unisonBtnBounds.contains(event.getPosition()))
    {
        if (auto* param = apvts.getParameter("UNISON_MODE"))
        {
            float cur = param->getValue();
            float nextVal = (cur > 0.5f) ? 0.0f : 1.0f;
            param->setValueNotifyingHost(nextVal);
            repaint();
        }
    }

    // Hit test for Octave Transpose (MONE / NORM / PONE)
    juce::Rectangle<int> transposeBtnBounds(12, (int)(h - 44), 55, 18);
    if (transposeBtnBounds.contains(event.getPosition()))
    {
        if (auto* param = apvts.getParameter("OCTAVE_TRANSPOSE"))
        {
            float cur = param->getValue(); // 0.0 (MONE), 0.5 (NORM), 1.0 (PONE)
            float nextVal = cur + 0.5f;
            if (nextVal > 1.05f) nextVal = 0.0f;
            param->setValueNotifyingHost(nextVal);
            repaint();
        }
    }
}

void WeatherRadarDisplay::mouseDoubleClick(const juce::MouseEvent&)
{
    hasRandomized = true;
    if (onDoubleClick)
        onDoubleClick();
}

void WeatherRadarDisplay::setSeedCode(const juce::String& code)
{
    seedCode = code;
    repaint();
}

void WeatherRadarDisplay::regenerateWeather(juce::uint32 seed, bool isPresetChange)
{
    isPresetRecallBlink = isPresetChange;

    // Check if the processor already holds the fully computed weather image for this seed
    if (processor.cachedWeatherSeedHash == seed && processor.cachedWeatherImage.isValid() &&
        processor.cachedWeatherImage.getWidth() == weatherImage.getWidth() &&
        processor.cachedWeatherImage.getHeight() == weatherImage.getHeight())
    {
        weatherImage = processor.cachedWeatherImage;
        seedHash = seed;
        isClearSkies = false;
        generateWaypoints(seed);
        repaint();
        return;
    }

    // Trigger black screen blink with status indicator if we are past the initial startup
    if (bootTimeSeconds > 3.5f)
    {
        randomizeBlinkTime = 0.12f; // 120ms CRT blink
    }

    seedHash = seed;
    isClearSkies = false;
    juce::Random rng((juce::int64) seed);

    // Fill noise grid
    for (int y = 0; y <= gridSize; ++y)
        for (int x = 0; x <= gridSize; ++x)
            noiseGrid[y][x] = rng.nextFloat();

    // Derive HUD values from seed
    heading = 1 + (int)(seed % 360);
    gainValue = 10 + (int)((seed >> 8) % 40);
    tiltValue = -2.0f + (float)((seed >> 16) % 50) * 0.1f;

    generateWaypoints(seed);
    rebuildWeatherImage();
    repaint();
}

float WeatherRadarDisplay::interpolatedNoise(float x, float y) const
{
    int x0 = (int) x;
    int y0 = (int) y;
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    x0 = juce::jlimit(0, gridSize, x0);
    y0 = juce::jlimit(0, gridSize, y0);
    x1 = juce::jlimit(0, gridSize, x1);
    y1 = juce::jlimit(0, gridSize, y1);

    float fx = x - (float)(int) x;
    float fy = y - (float)(int) y;

    // Smooth interpolation
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);

    float v00 = noiseGrid[y0][x0];
    float v10 = noiseGrid[y0][x1];
    float v01 = noiseGrid[y1][x0];
    float v11 = noiseGrid[y1][x1];

    float v0 = v00 + (v10 - v00) * fx;
    float v1 = v01 + (v11 - v01) * fx;
    return v0 + (v1 - v0) * fy;
}

float WeatherRadarDisplay::sampleNoise(float x, float y) const
{
    float total = 0.0f;
    float maxVal = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;

    // 3 octaves gives identical radar contour bands with 40% faster execution
    for (int octave = 0; octave < 3; ++octave)
    {
        total += perlinNoise2D(x * frequency, y * frequency, seedHash) * amplitude;
        maxVal += amplitude;
        amplitude *= 0.44f;
        frequency *= 2.22f;
    }
    
    // Normalize from ~[-0.7, 0.7] to [0.0, 1.0]
    return (total / maxVal) * 0.5f + 0.5f;
}

juce::Colour WeatherRadarDisplay::getWeatherColour(float intensity) const
{
    // Threshold set higher to make storm cells sparse, jagged and authentic
    if (intensity < 0.20f) return juce::Colour(0x00000000); 
    
    // Outer transition fringe: Solid dark forest green
    if (intensity < 0.28f)
        return juce::Colour(0xFF008418);

    // Standard precipitation: Solid bright neon green
    if (intensity < 0.38f)
        return juce::Colour(0xFF00E600);
        
    // Moderate warning yellow / amber
    if (intensity < 0.50f)
        return juce::Colour(0xFFFFD500);
        
    // Danger red
    if (intensity < 0.60f)
        return juce::Colour(0xFFE60000);
        
    // Extreme core magenta
    return juce::Colour(0xFFFF00CC);
}

void WeatherRadarDisplay::generateWaypoints(juce::uint32 seed)
{
    waypoints.clear();
    juce::Random wpRng((juce::int64)(seed ^ 0xDEADBEEF));
    int count = 3 + (int)(seed % 4);

    for (int i = 0; i < count; ++i)
    {
        Waypoint wp;
        wp.x = 0.15f + wpRng.nextFloat() * 0.7f;
        wp.y = 0.1f + wpRng.nextFloat() * 0.65f;

        // Generate 5-char aviation waypoint name
        juce::String name;
        for (int c = 0; c < 5; ++c)
        {
            char ch = 'A' + (char)(wpRng.nextInt(26));
            name += ch;
        }
        wp.name = name;
        waypoints.push_back(wp);
    }
}

void WeatherRadarDisplay::rebuildWeatherImage()
{
    int w = juce::jmax(1, getWidth());
    int h = juce::jmax(1, getHeight());
    
    if (weatherImage.getWidth() != w || weatherImage.getHeight() != h)
        weatherImage = juce::Image(juce::Image::ARGB, w, h, true);
    else
        weatherImage.clear(weatherImage.getBounds(), juce::Colours::transparentBlack);

    // During boot, leave the image blank — the sweep will fill it progressively
    if (bootTimeSeconds < 3.2f)
        return;

    const float centreX = (float) w * 0.5f;
    const float centreY = (float) h * 0.92f;  // aircraft position near bottom
    const float maxRadius = (float) h * 0.85f;
    const float maxRadiusSq = maxRadius * maxRadius;
    const float halfArc = radarArcRad * 0.5f;

    // Use seedHash to generate seed-dependent coordinate offsets
    float seedOffsetX = (float)(seedHash % 1000);
    float seedOffsetY = (float)((seedHash >> 10) % 1000);

    juce::Image::BitmapData destData (weatherImage, juce::Image::BitmapData::writeOnly);

    int minY = juce::jmax(0, (int) std::floor(centreY - maxRadius));
    int maxY = h;

    for (int py = minY; py < maxY; ++py)
    {
        float dy = (float) py - centreY;
        float dySq = dy * dy;
        const float apexY = centreY - 6.0f;
        bool inApex = (py >= apexY);
        float depthY = (float) py - apexY;
        float wedgeHalfWidth = 5.0f + depthY * 0.90f;

        float maxDx = std::sqrt(std::max(0.0f, maxRadiusSq - dySq));
        int minX = juce::jlimit(0, w - 1, (int) std::floor(centreX - maxDx));
        int maxX = juce::jlimit(0, w - 1, (int) std::ceil(centreX + maxDx));

        for (int px = minX; px <= maxX; ++px)
        {
            float dx = (float) px - centreX;
            float distSq = dx * dx + dySq;
            if (distSq > maxRadiusSq)
                continue;

            // Blind spot: Triangular wedge directly behind aircraft symbol
            if (inApex && std::abs(dx) <= wedgeHalfWidth)
                continue;

            // Angle arc cutoff only applies in front of aircraft
            if (dy < 0.0f)
            {
                float angle = std::atan2(dx, -dy); // 0 = up
                if (std::abs(angle) > halfArc)
                    continue;
            }

            float dist = std::sqrt(distSq);

            // Cartesian noise space (pure fixed position, ZERO coordinate slide)
            float noiseX = (dx / maxRadius) * 8.0f + seedOffsetX;
            float noiseY = (dy / maxRadius) * 8.0f + seedOffsetY - planeProgress;

            // Sparse modulation to create realistic island-like storm clusters
            float baseMask = perlinNoise2D(noiseX * 0.45f, noiseY * 0.45f, seedHash) * 0.5f + 0.5f;
            baseMask = juce::jlimit(0.0f, 1.0f, (baseMask - 0.28f) / 0.72f);

            float detail = sampleNoise(noiseX, noiseY);
            
            // Fetch cutoff parameter to control weather density, with subtle note intensity breathing
            float normalizedCutoff = 0.5f;
            if (auto* param = apvts.getParameter("CUTOFF"))
                normalizedCutoff = param->getValue();

            float effectiveCutoff = juce::jlimit(0.08f, 1.0f, normalizedCutoff + noteIntensityOffset);
            float intensity = baseMask * detail * effectiveCutoff * 1.5f;

            // Add 5% chance black hole attenuation zone in highly turbulent (red) storm cells
            float attenMask = perlinNoise2D(noiseX * 0.8f + 50.0f, noiseY * 0.8f + 50.0f, seedHash) * 0.5f + 0.5f;
            if (intensity > 0.45f && attenMask > 0.82f)
            {
                intensity = 0.0f; // Attenuated radar shadow (invisible / black hole)
            }

            // Attenuate at borders
            intensity *= (1.0f - (dist / maxRadius) * 0.25f);
            if (noteReactiveJitter > 0.005f)
                intensity *= (1.0f + noteReactiveJitter * 0.18f);

            auto col = getWeatherColour(intensity);

            // Rare Doppler Turbulence Pocket (in green zones, matching green tone, 100% sharp)
            if (intensity >= 0.20f && intensity < 0.38f)
            {
                float turbField = perlinNoise2D(noiseX * 0.7f + 160.0f, noiseY * 0.7f + 240.0f, seedHash ^ 0x5C5C) * 0.5f + 0.5f;
                if (turbField > 0.88f) // Highly rare, isolated pocket
                {
                    int diag = px + py; // Sağ çaprazdan
                    if ((diag % 6) < 3) // 3-pixel solid stripe, 3-pixel clean gap
                    {
                        if (intensity < 0.28f)
                            col = juce::Colour(0xFF008418); // Dark Forest Green stripe
                        else
                            col = juce::Colour(0xFF00E600); // Bright Neon Green stripe
                    }
                    else
                    {
                        col = juce::Colour(0x00000000); // Clean transparent gap
                    }
                }
            }

            if (col.getAlpha() > 0)
                destData.setPixelColour(px, py, col);
        }
    }

    processor.cachedWeatherImage = weatherImage;
    processor.cachedWeatherSeedHash = seedHash;
}

void WeatherRadarDisplay::updateSweptWeather()
{
    int w = weatherImage.getWidth();
    int h = weatherImage.getHeight();
    if (w <= 0 || h <= 0) return;

    const float centreX = (float) w * 0.5f;
    const float centreY = (float) h * 0.92f;  // aircraft position near bottom
    const float maxRadius = (float) h * 0.85f;
    const float maxRadiusSq = maxRadius * maxRadius;
    const float halfArc = radarArcRad * 0.5f;

    // Find the angular slice between sweep steps
    float a1 = lastSweepAngle;
    float a2 = sweepAngle;
    if (a1 > a2) std::swap(a1, a2);

    // Expand the slice slightly to cover pixel rounding/subpixel gaps
    a1 -= 0.05f;
    a2 += 0.05f;

    float seedOffsetX = (float)(seedHash % 1000);
    float seedOffsetY = (float)((seedHash >> 10) % 1000);

    juce::Image::BitmapData destData (weatherImage, juce::Image::BitmapData::writeOnly);

    // Precompute tangent bounds to avoid testing all 164,000 pixels on every 40 Hz frame
    float tanA1 = std::tan(juce::jlimit(-halfArc, halfArc, a1));
    float tanA2 = std::tan(juce::jlimit(-halfArc, halfArc, a2));
    if (tanA1 > tanA2) std::swap(tanA1, tanA2);

    int minY = juce::jmax(0, (int) std::floor(centreY - maxRadius));
    int maxY = h;

    for (int py = minY; py < maxY; ++py)
    {
        float dy = (float) py - centreY;
        float dySq = dy * dy;
        const float apexY = centreY - 6.0f;
        bool inApex = (py >= apexY);
        float depthY = (float) py - apexY;
        float wedgeHalfWidth = 5.0f + depthY * 0.90f;

        float hDist = -dy;
        int minX = 0;
        int maxX = w - 1;

        if (hDist > 0.001f)
        {
            float dxFromA1 = hDist * tanA1;
            float dxFromA2 = hDist * tanA2;
            float sliceMinDx = std::min(dxFromA1, dxFromA2) - 3.0f;
            float sliceMaxDx = std::max(dxFromA1, dxFromA2) + 3.0f;

            minX = juce::jlimit(0, w - 1, (int) std::floor(centreX + sliceMinDx));
            maxX = juce::jlimit(0, w - 1, (int) std::ceil(centreX + sliceMaxDx));
        }
        else
        {
            minX = juce::jlimit(0, w - 1, (int) std::floor(centreX - maxRadius));
            maxX = juce::jlimit(0, w - 1, (int) std::ceil(centreX + maxRadius));
        }

        for (int px = minX; px <= maxX; ++px)
        {
            float dx = (float) px - centreX;
            float distSq = dx * dx + dySq;
            if (distSq > maxRadiusSq)
                continue;

            // Blind spot: Triangular wedge directly behind aircraft symbol
            if (inApex && std::abs(dx) <= wedgeHalfWidth)
                continue;

            float angle = std::atan2(dx, -dy); // 0 = up
            if (dy < 0.0f && std::abs(angle) > halfArc)
                continue;

            // Only update pixels currently crossed by the sweep line
            if (angle >= a1 && angle <= a2)
            {
                float dist = std::sqrt(distSq);

                // Cartesian noise space (pure fixed position, ZERO coordinate slide)
                float noiseX = (dx / maxRadius) * 8.0f + seedOffsetX;
                float noiseY = (dy / maxRadius) * 8.0f + seedOffsetY - planeProgress;

                float baseMask = perlinNoise2D(noiseX * 0.45f, noiseY * 0.45f, seedHash) * 0.5f + 0.5f;
                baseMask = juce::jlimit(0.0f, 1.0f, (baseMask - 0.28f) / 0.72f);

                float detail = sampleNoise(noiseX, noiseY);
                
                // Fetch cutoff parameter to control weather density, with subtle note intensity breathing
                float normalizedCutoff = 0.5f;
                if (auto* param = apvts.getParameter("CUTOFF"))
                    normalizedCutoff = param->getValue();

                float effectiveCutoff = juce::jlimit(0.08f, 1.0f, normalizedCutoff + noteIntensityOffset);
                float intensity = baseMask * detail * effectiveCutoff * 1.5f;

                // Add 5% chance black hole attenuation zone in highly turbulent (red) storm cells
                float attenMask = perlinNoise2D(noiseX * 0.8f + 50.0f, noiseY * 0.8f + 50.0f, seedHash) * 0.5f + 0.5f;
                if (intensity > 0.45f && attenMask > 0.82f)
                {
                    intensity = 0.0f; // Attenuated radar shadow (invisible / black hole)
                }

                // Attenuate at borders
                intensity *= (1.0f - (dist / maxRadius) * 0.25f);
                if (noteReactiveJitter > 0.005f)
                    intensity *= (1.0f + noteReactiveJitter * 0.18f);

                auto col = getWeatherColour(intensity);

                // Rare Doppler Turbulence Pocket (in green zones, matching green tone, 100% sharp)
                if (intensity >= 0.20f && intensity < 0.38f)
                {
                    float turbField = perlinNoise2D(noiseX * 0.7f + 160.0f, noiseY * 0.7f + 240.0f, seedHash ^ 0x5C5C) * 0.5f + 0.5f;
                    if (turbField > 0.88f) // Highly rare, isolated pocket
                    {
                        int diag = px + py; // Sağ çaprazdan
                        if ((diag % 6) < 3) // 3-pixel solid stripe, 3-pixel clean gap
                        {
                            if (intensity < 0.28f)
                                col = juce::Colour(0xFF008418); // Dark Forest Green stripe
                            else
                                col = juce::Colour(0xFF00E600); // Bright Neon Green stripe
                        }
                        else
                        {
                            col = juce::Colour(0x00000000); // Clean transparent gap
                        }
                    }
                }

                destData.setPixelColour(px, py, col);
            }
        }
    }
}

void WeatherRadarDisplay::resized()
{
    if (weatherImage.getWidth() == getWidth() && weatherImage.getHeight() == getHeight() && weatherImage.isValid())
        return;

    rebuildWeatherImage();
}

void WeatherRadarDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();

    // ------------------ RANDOMIZING BLINK SCREEN ------------------
    if (randomizeBlinkTime > 0.0f)
    {
        g.setColour(juce::Colour(radarBg));
        g.fillRect(bounds.toNearestInt());
        g.setColour(juce::Colour(radarBezel));
        g.drawRect(bounds.reduced(1.0f).toNearestInt(), 2);
        
        g.setFont(getCockpitFont(14.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xFFFF9100)); // Amber
        juce::String blinkText = isPresetRecallBlink ? "RECONFIGURING..." : "RANDOMIZING...";
        g.drawText(blinkText, 20, (int)(h - 32), 200, 20, juce::Justification::left);
        return;
    }

    // ------------------ BOOT SCREEN SEQUENCE ------------------
    if (bootTimeSeconds < 3.2f)
    {
        // Solid black background with sharp bezel frame
        g.setColour(juce::Colour(radarBg));
        g.fillRect(bounds.toNearestInt());
        g.setColour(juce::Colour(radarBezel));
        g.drawRect(bounds.reduced(1.0f).toNearestInt(), 2);

        auto getBootFont = [](float size, int style = juce::Font::plain) {
            return WXFonts::get(size, style);
        };

        if (bootTimeSeconds < 0.3f)
        {
            // Phase 1: Boot (0.0s)
            g.setFont(getBootFont(24.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xFFFF9100)); // Amber
            g.drawText("WX", bounds.withTrimmedLeft(40.0f).withTrimmedTop(h * 0.35f), juce::Justification::left);
            g.setFont(getBootFont(14.0f, juce::Font::bold));
            g.drawText("WEATHER RADAR SYNTH", bounds.withTrimmedLeft(40.0f).withTrimmedTop(h * 0.45f), juce::Justification::left);
        }
        else if (bootTimeSeconds >= 0.3f && bootTimeSeconds < 1.8f)
        {
            // Phase 2: Self Test (0.3s)
            g.setFont(getBootFont(22.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xFFFF9100)); // Amber
            g.drawText("WX", bounds.withTrimmedLeft(40.0f).withTrimmedTop(h * 0.12f), juce::Justification::left);
            
            g.setFont(getBootFont(14.0f, juce::Font::bold));
            g.drawText("POWER ON SELF TEST", bounds.withTrimmedLeft(40.0f).withTrimmedTop(h * 0.25f), juce::Justification::left);

            // Display results line-by-line using time offset
            float testProgress = (bootTimeSeconds - 0.3f) / 1.5f; // 0.0 to 1.0
            
            juce::StringArray testLines;
            testLines.add("CPU ............. OK");
            testLines.add("DSP ............. OK");
            testLines.add("MEMORY .......... OK");
            testLines.add("DISPLAY ......... OK");
            testLines.add("AUDIO ENGINE .... OK");

            int linesToShow = juce::jlimit(0, 5, (int)(testProgress * 6.5f));
            
            g.setFont(getBootFont(13.5f, juce::Font::bold));
            for (int i = 0; i < linesToShow; ++i)
            {
                float lineY = h * 0.38f + (float) i * 20.0f;
                g.drawText(testLines[i], bounds.withTrimmedLeft(40.0f).withTrimmedTop(lineY), juce::Justification::left);
            }
        }
        else
        {
            // Phase 3: Standby (1.8s to 3.2s)
            g.setFont(getBootFont(18.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xFFFF9100)); // Amber
            g.drawText("PLEASE STANDBY", bounds.withTrimmedLeft(40.0f).withTrimmedTop(h * 0.44f), juce::Justification::left);
        }
        return;
    }

    // Fetch detune value to drive the TRK activeHeading HUD output
    int activeHeading = heading;
    if (auto* param = apvts.getParameter("DETUNE"))
    {
        float detuneVal = param->getValue(); // Normalized 0..1
        activeHeading = (int) std::round(detuneVal * 359.0f);
    }

    // Fetch resonance value to drive the TILT HUD output
    float activeTilt = tiltValue;
    if (auto* param = apvts.getParameter("RESONANCE"))
    {
        float resoVal = param->getValue(); // Normalized 0..1
        activeTilt = -15.0f + resoVal * 30.0f; // Map to -15.0 to +15.0 degrees
    }

    // Black radar background with sharp square edges
    g.setColour(juce::Colour(radarBg));
    g.fillRect(bounds.toNearestInt());

    // Bezel frame
    g.setColour(juce::Colour(radarBezel));
    g.drawRect(bounds.reduced(1.0f).toNearestInt(), 2);

    // Save state and reduce clip region to keep all radar elements strictly inside the bezel borders
    g.saveState();
    g.reduceClipRegion(bounds.reduced(2.0f).toNearestInt());

    const float centreX = w * 0.5f;
    const float centreY = h * 0.92f;
    const float maxRadius = h * 0.85f;
    const float halfArc = radarArcRad * 0.5f;

    // --- Range rings ---
    g.setColour(juce::Colour(ringColour).brighter(0.15f));
    for (int ring = 1; ring <= 3; ++ring)
    {
        float r = maxRadius * (float) ring / 3.0f;
        juce::Path arc;
        arc.addCentredArc(centreX, centreY, r, r, 0.0f,
                          -halfArc,
                          halfArc,
                          true);
        g.strokePath(arc, juce::PathStrokeType(1.8f));
    }

    // --- Compass ticks ---
    g.setColour(juce::Colour(ringColour).brighter(0.75f));
    for (int deg = -110; deg <= 110; deg += 10)
    {
        float angle = (float) deg * juce::MathConstants<float>::pi / 180.0f;
        float innerR = maxRadius - 8.0f;
        float outerR = maxRadius;
        if (deg % 30 == 0)
        {
            innerR = maxRadius - 14.0f;
            // Draw degree label
            float labelR = maxRadius - 22.0f;
            float lx = centreX + labelR * std::sin(angle);
            float ly = centreY - labelR * std::cos(angle);
            g.setFont(getCockpitFont(12.0f, juce::Font::bold));
            g.setColour(juce::Colour(ringColour).brighter(0.55f));
            int displayDeg = ((activeHeading + deg) % 360 + 360) % 360;
            g.drawText(juce::String(displayDeg / 10), (int)(lx - 12), (int)(ly - 8), 24, 16, juce::Justification::centred);
            g.setColour(juce::Colour(ringColour).brighter(0.35f));
        }

        float x1 = centreX + innerR * std::sin(angle);
        float y1 = centreY - innerR * std::cos(angle);
        float x2 = centreX + outerR * std::sin(angle);
        float y2 = centreY - outerR * std::cos(angle);
        g.drawLine(x1, y1, x2, y2, (deg % 30 == 0) ? 2.1f : 1.5f);
    }

    // --- Center line (heading) ---
    g.setColour(juce::Colour(0xFFF2EBD7).withAlpha(0.9f));
    g.drawLine(centreX, centreY, centreX, centreY - maxRadius, 1.6f);

    // --- Weather returns ---
    if (weatherImage.isValid())
    {
        g.drawImageAt(weatherImage, 0, 0);
    }

    // --- Aircraft symbol (small triangle at bottom center) ---
    {
        juce::Path aircraft;
        aircraft.addTriangle(centreX - 6.0f, centreY + 4.0f,
                             centreX + 6.0f, centreY + 4.0f,
                             centreX, centreY - 10.0f);
        g.setColour(juce::Colour(textWhite));
        g.strokePath(aircraft, juce::PathStrokeType(1.2f));
    }

    // --- Waypoint markers ---
    g.setFont(getCockpitFont(14.0f, juce::Font::bold));
    for (auto& wp : waypoints)
    {
        float wx = wp.x * w;
        float wy = wp.y * h;
        // Small cross
        g.setColour(juce::Colour(waypointColor));
        g.drawLine(wx - 4, wy, wx + 4, wy, 1.0f);
        g.drawLine(wx, wy - 4, wx, wy + 4, 1.0f);
        // Label
        g.setColour(juce::Colour(textCyan));
        g.drawText(wp.name, (int)(wx + 5), (int)(wy - 9), 75, 18, juce::Justification::left);
    }

    // Restore state before drawing HUD text overlays to avoid clipping text
    g.restoreState();

    // --- HUD Text Overlays ---
    g.setFont(getCockpitFont(13.5f, juce::Font::bold));

    // Fetch master gain value to drive GAIN HUD readout
    int activeGain = gainValue;
    if (auto* param = apvts.getParameter("GAIN"))
    {
        activeGain = (int) std::round(param->getValue() * 100.0f);
    }

    // Top-left: GAIN / TILT (generous 16px left margin, 12px top margin)
    g.setColour(juce::Colour(textDimGreen));
    g.drawText("GAIN  " + juce::String(activeGain), 16, 12, 110, 18, juce::Justification::left);
    g.drawText("TILT  " + juce::String(activeTilt, 1) + juce::String(juce::CharPointer_UTF8("\xc2\xb0")),
               16, 30, 110, 18, juce::Justification::left);

    // Top-center: TRK heading MAG (compact, sleek cockpit HUD)
    juce::String headingStr = juce::String(activeHeading).paddedLeft('0', 3);
    float boxW = 38.0f;
    float boxH = 19.0f;
    float boxX = (w - boxW) * 0.5f;
    float boxY = 11.0f;

    // TRK label directly to the left of the box
    g.setColour(juce::Colour(textCyan));
    g.setFont(getCockpitFont(13.5f, juce::Font::bold));
    g.drawText("TRK", (int)(boxX - 38.0f), (int) boxY, 32, (int) boxH, juce::Justification::right);

    // Sleek, compact dark box with subtle green border (no huge bright brick)
    g.setColour(juce::Colour(0xDD0A150D));
    g.fillRect((int) boxX, (int) boxY, (int) boxW, (int) boxH);
    g.setColour(juce::Colour(textCyan).withAlpha(0.75f));
    g.drawRect((int) boxX, (int) boxY, (int) boxW, (int) boxH, 1);
    g.setFont(getCockpitFont(14.5f, juce::Font::bold));
    g.setColour(juce::Colour(textCyan));
    g.drawText(headingStr, (int) boxX, (int) boxY, (int) boxW, (int) boxH, juce::Justification::centred);

    // MAG label directly to the right of the box
    g.setFont(getCockpitFont(13.5f, juce::Font::bold));
    g.drawText("MAG", (int)(boxX + boxW + 6.0f), (int) boxY, 36, (int) boxH, juce::Justification::left);

    // Top-right: Seed code as waypoint (proper 16px right margin, safe from bezel)
    g.setColour(juce::Colour(textCyan));
    g.setFont(getCockpitFont(14.0f, juce::Font::bold));
    g.drawText(seedCode, (int)(w - 150), 12, 134, 18, juce::Justification::right);

    // Bottom-left: Mode indicators & Interactive Octave Transpose (MONE, NORM, PONE)
    int currentOctaveMode = 1;
    if (auto* param = apvts.getParameter("OCTAVE_TRANSPOSE"))
        currentOctaveMode = (int) std::round(param->getValue() * 2.0f); // 0, 1, 2

    juce::String octStr = "NORM";
    if (currentOctaveMode == 0)      octStr = "MONE";
    else if (currentOctaveMode == 2) octStr = "PONE";

    int currentUnisonMode = 1;
    if (auto* param = apvts.getParameter("UNISON_MODE"))
        currentUnisonMode = (int) std::round(param->getValue() * 1.0f);
    
    juce::String unisonStr = (currentUnisonMode == 0) ? "2VU" : "5VU";

    // Stacked cleanly with 18px line pitch, finishing safely above bottom bezel
    g.setFont(getCockpitFont(13.5f, juce::Font::bold));
    g.setColour(juce::Colour(textDimGreen));
    g.drawText("WX-T",     16, (int)(h - 78), 70, 16, juce::Justification::left);
    g.drawText("MAP-A",    16, (int)(h - 60), 70, 16, juce::Justification::left);
    g.drawText(octStr,     16, (int)(h - 42), 70, 16, juce::Justification::left);
    g.drawText(unisonStr,  16, (int)(h - 24), 70, 16, juce::Justification::left);

    // Bottom-right: Range indicators (80NM and 160NM) with clear gap before circles
    g.setFont(getCockpitFont(13.5f, juce::Font::bold));
    g.setColour(juce::Colour(textCyan));
    g.drawText("80NM",  (int)(w - 96), (int)(h - 42), 66, 16, juce::Justification::right);
    g.drawEllipse(w - 24, h - 37, 7, 7, 1.2f);

    g.drawText("160NM", (int)(w - 96), (int)(h - 24), 66, 16, juce::Justification::right);
    g.drawEllipse(w - 24, h - 19, 7, 7, 1.2f);

    // --- Phase 6: Double Click Warning ---
    if (bootTimeSeconds >= 7.0f && !hasRandomized)
    {
        auto alertBox = juce::Rectangle<int>(w * 0.08f, h - 90, w * 0.84f, 26);
        g.setColour(juce::Colour(0xEE0A0A0A));
        g.fillRect(alertBox);
        g.setColour(juce::Colour(0xFFFF9100)); // Amber warning
        g.drawRect(alertBox, 1);
        
        g.setFont(getCockpitFont(14.0f, juce::Font::bold));
        g.drawText("DOUBLE CLICK TO RANDOMIZE PATCH", alertBox, juce::Justification::centred);
    }
}
