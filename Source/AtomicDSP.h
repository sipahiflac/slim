#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace CrystalDSP
{
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kTwoPi = 6.28318530717958647692;
    static constexpr double kSqrt2 = 1.41421356237309504880;

    // 4+4 Polyphase Hilbert Allpass katsayilari (F_2100B0 topolojisi)
    static constexpr double kHilbA[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    static constexpr double kHilbB[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };

    // APMod 6-dal difuzor parametreleri
    static constexpr double kAPModKMod[6] = { 0.35, 0.25, 0.20, 0.10, 0.10, 0.05 };
    static constexpr double kAPModKSat[6] = { 0.166, 0.166, 0.266, 0.166, 0.166, 0.166 };

    struct DecodedSeed
    {
        std::array<int, 3> chain { 6, 2, 13 };
        int flavor = 1;
    };

    inline int encodeChainToSeed (int m0, int m1, int m2, int flavor = 0) noexcept
    {
        const int i1 = (m1 > m0) ? (m1 - 1) : m1;
        const int lo = std::min (m0, m1);
        const int hi = std::max (m0, m1);
        const int i2 = m2 - (m2 > lo ? 1 : 0) - (m2 > hi ? 1 : 0);
        const int chainIdx = m0 * 210 + i1 * 14 + i2; // 0..3359
        return (((flavor % 9) + 9) % 9 * 3360 + chainIdx) & 32767;
    }

    inline DecodedSeed decodeSeedToChain (int seed) noexcept
    {
        const int s = seed & 32767;
        const int chainIdx = s % 3360;
        const int flavor = s / 3360;
        const int m0 = chainIdx / 210;
        const int rem = chainIdx % 210;
        const int i1 = rem / 14;
        const int m1 = (i1 >= m0) ? (i1 + 1) : i1;
        const int i2 = rem % 14;
        const int lo = std::min (m0, m1);
        const int hi = std::max (m0, m1);
        int m2 = i2;
        if (m2 >= lo) ++m2;
        if (m2 >= hi) ++m2;
        return { { m0, m1, m2 }, flavor };
    }

    inline std::array<int, 5> seedToRunes (int seed) noexcept
    {
        const int s = seed & 32767;
        return { (s >> 12) & 7, (s >> 9) & 7, (s >> 6) & 7, (s >> 3) & 7, s & 7 };
    }

    // =========================================================================
    // PROSEDUREL RISOGRAPH RENK PALETI URETICISI (Preset Seed'ine 100% Bagli!)
    // Kural-bazli ama rastgele: her zaman estetik, asla camur/neon degil!
    // =========================================================================
    struct Palette
    {
        juce::Colour paper;
        juce::Colour base;
        juce::Colour shadow;
        juce::Colour ink;
        juce::Colour contrast;
    };

    inline uint32_t splitmix32 (uint32_t& x) noexcept
    {
        uint32_t z = (x += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        return z ^ (z >> 16);
    }

    inline float rand01 (uint32_t& rng) noexcept
    {
        return static_cast<float> (splitmix32 (rng) & 0x00FFFFFFu) / 16777216.0f;
    }

    inline juce::Colour hslColour (float hDeg, float s, float l) noexcept
    {
        float h = std::fmod (hDeg, 360.0f);
        if (h < 0.0f) h += 360.0f;
        return juce::Colour::fromHSL (h / 360.0f,
                                      std::clamp (s, 0.0f, 1.0f),
                                      std::clamp (l, 0.0f, 1.0f),
                                      1.0f);
    }

    inline Palette makeProceduralPalette (int seed) noexcept
    {
        // Ilk kuratorlu starter preset icin klasik Hardal paletini koru
        if (seed == encodeChainToSeed (6, 2, 13, 1))
        {
            return {
                juce::Colour (0xffe2b665),
                juce::Colour (0xffeceae4),
                juce::Colour (0xffc3bfb7),
                juce::Colour (0xff1f1613),
                juce::Colour (0xff1f3b73)
            };
        }

        uint32_t rng = static_cast<uint32_t> (seed * 747796405u + 2891336453u);
        const float archetype = rand01 (rng);

        // Ink her zaman derin sicak komur/espresso -> her zeminde keskin keceli kalem okunurlugu
        const float inkHue = 15.0f + rand01 (rng) * 28.0f;
        const juce::Colour ink = hslColour (inkHue, 0.18f + rand01 (rng) * 0.08f, 0.10f + rand01 (rng) * 0.025f);

        if (archetype < 0.38f)
        {
            // Arketip A: Renkli Risograph Kagit + Tebeşir / Fildisi Kristal (Hardal ruhu!)
            // Sicak, toprak, adaçayi, tozlu gul, hardal, kil tonlari (neon yesil/mor yok)
            static constexpr float kPaperBands[6] = { 38.0f, 16.0f, 145.0f, 350.0f, 205.0f, 48.0f };
            const int band = static_cast<int> (splitmix32 (rng) % 6u);
            const float paperH = kPaperBands[band] + (rand01 (rng) - 0.5f) * 22.0f;
            const float paperS = 0.36f + rand01 (rng) * 0.24f;
            const float paperL = 0.62f + rand01 (rng) * 0.11f;

            const float baseH = paperH + (rand01 (rng) - 0.5f) * 18.0f;
            const juce::Colour paper  = hslColour (paperH, paperS, paperL);
            const juce::Colour base   = hslColour (baseH, 0.10f + rand01 (rng) * 0.08f, 0.90f + rand01 (rng) * 0.03f);
            const juce::Colour shadow = hslColour (baseH + 8.0f, 0.10f + rand01 (rng) * 0.08f, 0.74f + rand01 (rng) * 0.04f);

            // Split-complementary derin vurgu murekkebi
            const float compOffset = (rand01 (rng) > 0.5f ? 155.0f : 205.0f) + (rand01 (rng) - 0.5f) * 24.0f;
            const juce::Colour contrast = hslColour (paperH + compOffset, 0.48f + rand01 (rng) * 0.20f, 0.28f + rand01 (rng) * 0.10f);
            return { paper, base, shadow, ink, contrast };
        }
        else
        {
            // Arketip B: Sicak Arsiv Kremi Kagit + Tozlu Pigment Kristal (Mavi / Mercan / Sari ruhu!)
            const bool coolPaper = rand01 (rng) < 0.18f;
            const float paperH = coolPaper ? (195.0f + rand01 (rng) * 20.0f) : (34.0f + rand01 (rng) * 16.0f);
            const float paperS = coolPaper ? (0.14f + rand01 (rng) * 0.12f) : (0.28f + rand01 (rng) * 0.18f);
            const float paperL = 0.89f + rand01 (rng) * 0.045f;

            // Zengin ama olgun risograph pigment halkasi
            const float baseH = rand01 (rng) * 360.0f;
            const bool isGreenBand = (baseH > 85.0f && baseH < 155.0f);
            const float baseS = isGreenBand ? (0.30f + rand01 (rng) * 0.18f) : (0.44f + rand01 (rng) * 0.22f);
            const float baseL = 0.54f + rand01 (rng) * 0.12f;

            const juce::Colour paper  = hslColour (paperH, paperS, paperL);
            const juce::Colour base   = hslColour (baseH, baseS, baseL);
            // Cel-shaded golge: hafif soguk kayma + %17 daha koyu
            const juce::Colour shadow = hslColour (baseH + 8.0f, std::min (0.72f, baseS + 0.05f), std::max (0.28f, baseL - 0.17f));

            // Kontrast vurgu: karsit veya uclu uyum, doygun ama koyu-orta
            const float compOffset = (rand01 (rng) > 0.5f ? 150.0f : 205.0f) + (rand01 (rng) - 0.5f) * 30.0f;
            const juce::Colour contrast = hslColour (baseH + compOffset, 0.54f + rand01 (rng) * 0.20f, 0.34f + rand01 (rng) * 0.11f);
            return { paper, base, shadow, ink, contrast };
        }
    }

    // =========================================================================
    // KATEGORIK PROSEDUREL HATCH STILI (4 Yamuk/Diyagonal Kategori, Seed'e Bagli)
    // =========================================================================
    enum class HatchStyle : int
    {
        DiagUpRight = 0,     // Klasik / diyagonal ileri
        DiagDownRight = 1,   // Tam ters \ diyagonal geri
        ZigzagUpRight = 2,   // El cizimi keceli kalem / zikzak ileri
        ZigzagDownRight = 3  // Tam ters \ el cizimi keceli kalem zikzak geri
    };

    inline HatchStyle makeProceduralHatchStyle (int seed) noexcept
    {
        if (seed == encodeChainToSeed (6, 2, 13, 1))
            return HatchStyle::DiagUpRight;
        uint32_t rng = static_cast<uint32_t> (seed * 2246822519u + 3266489917u);
        return static_cast<HatchStyle> (splitmix32 (rng) % 4u);
    }

    // =========================================================================
    // PROSEDUREL SHADER KATMANI (1/6 Kristal Bayer Dither, 1/6 Hatch Pixelate)
    // Hicbir DSP parametresine bagli degil — sadece seed ile rastgelelik!
    // =========================================================================
    enum class VisualFx : int
    {
        None = 0,
        CrystalDither = 1,   // 1/6: Donen 3D kristalin uzerine 8x8 Bayer Dither shader katmani
        HatchPixelate = 2    // 1/6: Arkadaki hareketli cizgilerin uzerine Pixelate shader katmani
    };

    inline VisualFx makeProceduralVisualFx (int seed) noexcept
    {
        if (seed == encodeChainToSeed (6, 2, 13, 1))
            return VisualFx::None;
        uint32_t rng = static_cast<uint32_t> (seed * 374761393u + 668265263u);
        // Her ikisi de 1/18 (~%5.5) nadir ozel shader katmani!
        const uint32_t roll = splitmix32 (rng) % 18u;
        if (roll == 0u) return VisualFx::CrystalDither;
        if (roll == 1u) return VisualFx::HatchPixelate;
        return VisualFx::None;
    }

    // =========================================================================
    // 16 SAF ATOMIK DISTORTION YAPI TASI (Sifir heap, processBlock icinde %100 RT-safe)
    // =========================================================================
    struct SlotState
    {
        double dcX1 = 0.0, dcY1 = 0.0, dc2X1 = 0.0, dc2Y1 = 0.0;
        double shCount = 0.0, shHeld = 0.0;
        double hAx[4] {}, hAy[4] {}, hBx[4] {}, hBy[4] {}, hADel = 0.0;
        double rmsS = 0.0;
        int zcSign = 0, zcCount = 0, zcHalfP = 120;
        double svTsmooth = 120.0, svCnt = 0.0, svSq = 1.0;
        bool svFlag = false;
        bool blComp = false, blQ = false;
        double blHold = 0.0;
        int blCnt = 0;
        int zc2State = 0, zc2Cnt = 0, zc2HalfP = 140;
        double apPh = 0.0;
        double apX1[3] {}, apX2[3] {}, apY1[3] {}, apY2[3] {};
        double apLastTap = 0.0;
        double apSch[6][2] {};
        float apDel[6][1024] {};
        int apDelW = 0;
        double desPrev = 0.0;
        float jitBuf[256] {};
        int jitW = 0;
        double jitPh = 0.0;

        void reset() noexcept
        {
            *this = SlotState {};
        }
    };

    struct ChannelState
    {
        double lp1 = 0.0, lp2 = 0.0, hp1 = 0.0;
        double dcOutX1 = 0.0, dcOutY1 = 0.0;
        double stageRmsSq[2] { 0.0, 0.0 };
        double stageGain[2]  { 1.0, 1.0 };
        double midHiLp = 0.0;
        double outHiLp = 0.0;
        SlotState slots[3];

        void reset() noexcept
        {
            lp1 = lp2 = hp1 = dcOutX1 = dcOutY1 = 0.0;
            stageRmsSq[0] = stageRmsSq[1] = 0.0;
            stageGain[0]  = stageGain[1]  = 1.0;
            midHiLp = outHiLp = 0.0;
            for (auto& s : slots) s.reset();
        }
    };

    inline double reflectFold (double val, int count, double T) noexcept
    {
        double v = val;
        for (int i = 0; i < count; ++i)
        {
            if (v > T) v = 2.0 * T - v; else if (v < -T) v = -2.0 * T - v;
            if (v > T) v = 2.0 * T - v; else if (v < -T) v = -2.0 * T - v;
        }
        return v;
    }

    inline double nextUniformNoise (uint32_t& rng) noexcept
    {
        uint32_t x = rng;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng = x;
        return (static_cast<double> (x) / 2147483648.0) - 1.0;
    }

    inline double processAtomicModule (double x, SlotState& st, int modId, double p,
                                       double sr, double stereoSkew, uint32_t& rng) noexcept
    {
        if (p <= 0.008) return x;
        double wet = x;

        switch (modId)
        {
            case 0: // TanhPow (MonkeyCart normalize tanh + r^p)
            {
                const double k = 1.2 + 28.0 * p;
                const double pw = 1.0 + 6.0 * p;
                const double a = std::min (2.4, std::abs (x));
                const double s = x >= 0.0 ? 1.0 : -1.0;
                const double t1 = std::tanh (k * 0.5);
                const double r = std::tanh (k * a * 0.5) / (t1 > 1e-6 ? t1 : 1e-6);
                wet = s * std::pow (std::max (0.0, r), 1.0 + 0.35 * pw) * 0.92;
                break;
            }
            case 1: // SinFMFold (MonkeyCart dinamik t*sin(theta) FM katlama)
            {
                const double k = 2.5 + 16.0 * p;
                const double c = 3.0 + 6.5 * p;
                const double a = std::min (2.2, std::abs (x));
                const double s = x >= 0.0 ? 1.0 : -1.0;
                const double t = std::tanh (k * a * 0.5);
                const double t1 = std::tanh (k * 0.5);
                const double r = t / (t1 > 1e-6 ? t1 : 1e-6);
                const double theta = (c + 3.0 * r * r) * a * t1 * t1;
                wet = s * (t * std::sin (theta) * 0.85 + r * 0.25);
                break;
            }
            case 2: // ReflFold (GutCut 6-kademeli reflection wavefolder)
            {
                const double gain = std::pow (10.0, 0.05 * (13.0 * p));
                const double nf = 1.0 + 5.0 * p;
                const int n0 = static_cast<int> (std::floor (nf));
                const double fr = nf - static_cast<double> (n0);
                const double yin = x * gain;
                wet = (reflectFold (yin, n0, 0.60) * (1.0 - fr) + reflectFold (yin, n0 + 1, 0.60) * fr) * 1.1;
                break;
            }
            case 3: // AtanRect (GutCut cift atan(2|x|) tam dalga dogrultma)
            {
                double y = p * std::atan (2.2 * std::abs (x)) + (1.0 - p) * x;
                const double d1 = y - st.dcX1 + 0.994 * st.dcY1;
                st.dcX1 = y; st.dcY1 = d1; y = d1;
                const double y2 = (p * 0.55) * std::atan (2.2 * std::abs (y)) + (1.0 - p * 0.55) * y;
                const double d2 = y2 - st.dc2X1 + 0.994 * st.dc2Y1;
                st.dc2X1 = y2; st.dc2Y1 = d2;
                wet = d2 * 1.15;
                break;
            }
            case 4: // AsymPoly5 (Blaster w=1 etrafi 5. derece asimetrik polinom)
            {
                const double w = std::clamp (x * (1.0 + 1.5 * p), -1.38, 2.1);
                const double wm1 = w - 1.0;
                const double f = 1.0 + 0.7 * wm1 + std::pow (0.786 * wm1, 5.0);
                const double lim = 1.0 - 0.5 * std::clamp (std::abs (w) - 1.0, 0.0, 1.0);
                const double raw = std::tanh (lim * f * 0.48);
                const double dc = raw - st.dcX1 + 0.994 * st.dcY1;
                st.dcX1 = raw; st.dcY1 = dc;
                wet = dc * 1.1;
                break;
            }
            case 5: // SlopeHyst (DESCart isaretli turev histerezis)
            {
                const double slope = x - st.desPrev;
                st.desPrev = x;
                const double thr = 0.005 * (1.0 - p * 0.7);
                const double gA = 1.6 + 6.5 * p;
                const double gB = 1.2 + 3.8 * p;
                const double y = (slope >= thr)
                    ? std::sin (std::atan (x * gA) * (1.5 + 2.2 * p))
                    : (std::atan ((x + 0.35 * p * x * x) * gB) / std::atan (gB));
                const double dc = y - st.dcX1 + 0.994 * st.dcY1;
                st.dcX1 = y; st.dcY1 = dc;
                wet = dc;
                break;
            }
            case 6: // HilbertIM (IM 4+4 Hilbert analitik zarf yerine koyma)
            {
                double ia = x, ib = x;
                for (int j = 0; j < 4; ++j)
                {
                    const double yA = kHilbA[j] * (ia - st.hAy[j]) + st.hAx[j];
                    st.hAx[j] = ia; st.hAy[j] = yA; ia = yA;
                    const double yB = kHilbB[j] * (ib - st.hBy[j]) + st.hBx[j];
                    st.hBx[j] = ib; st.hBy[j] = yB; ib = yB;
                }
                const double I = st.hADel;
                st.hADel = ia;
                const double env = std::sqrt (I * I + ib * ib);
                const double thr = 0.08 * (1.0 - p * 0.85);
                const double src = ((thr > x) || (thr > env)) ? env : x;
                const double k = std::min (1.0, (0.2 + 0.8 * p) * std::abs (x) * 1.6);
                const double y = (1.0 - k) * x + k * src;
                const double dc = y - st.dcX1 + 0.994 * st.dcY1;
                st.dcX1 = y; st.dcY1 = dc;
                wet = dc * 1.15;
                break;
            }
            case 7: // SinAtanFld (IM sin(g*atan(y)) wavefolder)
            {
                const double g = 1.0 + 2.14159 * p;
                const double gp = g / kPi;
                wet = (1.0 - gp) * x + gp * std::sin (g * std::atan (x * (1.4 + p * 1.6)));
                break;
            }
            case 8: // ZCRingMod (Supervoid pitch-kilitli kare dalga AM/RingMod)
            {
                const int sgn = x >= 0.0 ? 1 : 0;
                if (st.zcCount >= 10 && sgn != st.zcSign)
                {
                    st.zcHalfP = std::min (st.zcCount, 2400);
                    st.zcCount = 0;
                }
                ++st.zcCount;
                st.zcSign = sgn;
                const double targetT = static_cast<double> (st.zcHalfP) * (0.35 + 0.65 * (1.0 - p * 0.7))
                                       * (1.0 + stereoSkew * 0.06) + 6.0;
                st.svTsmooth += (targetT - st.svTsmooth) * 0.04;
                if (st.svTsmooth - st.svCnt < 0.0) { st.svSq = -st.svSq; st.svCnt = 0.0; }
                else { st.svCnt += 1.0; }
                wet = (x - 1.65 * p * x * st.svSq) * 0.78;
                break;
            }
            case 9: // RMSThump (Supervoid isaretli RMS zarf enjeksiyonu)
            {
                const double dRms = x * x - st.rmsS;
                st.rmsS += (std::abs (dRms) > 0.005) ? 0.005 * dRms : dRms;
                const double env = std::sqrt (std::max (0.0, st.rmsS)) * kSqrt2;
                const int sgn = x >= 0.0 ? 1 : 0;
                if (st.zcCount >= 14 && sgn != st.zcSign) { st.zcCount = 0; st.svFlag = ! st.svFlag; }
                ++st.zcCount; st.zcSign = sgn;
                const double raw = x + (st.svFlag ? -env : env) * (0.95 * p);
                const double dc = raw - st.dcX1 + 0.992 * st.dcY1;
                st.dcX1 = raw; st.dcY1 = dc;
                wet = std::tanh (dc * 1.15) * 0.92;
                break;
            }
            case 10: // OctFuzz (Blaster/Smoothie T flip-flop /2 octave fuzz)
            {
                const bool comp = x >= 0.055;
                if (comp && ! st.blComp)
                {
                    st.blQ = ! st.blQ;
                    st.blHold = st.blQ ? x : std::abs (x);
                    st.blCnt = 0;
                }
                st.blComp = comp;
                if (++st.blCnt > 1600) st.blHold *= 0.995;
                const double u = 0.5 * ((st.blQ ? -x : x) + st.blHold);
                const double dc = u - st.dcX1 + 0.994 * st.dcY1;
                st.dcX1 = u; st.dcY1 = dc;
                wet = std::tanh (dc * (1.6 + 2.4 * p)) * 0.95;
                break;
            }
            case 11: // TruncBit (BitBoy sifira dogru kesmeli quantizer + ADC rasyonel)
            {
                const double levels = std::pow (2.0, 4.8 * (1.0 - p) + 2.4);
                const double v = x * (1.0 + 1.4 * p);
                const double q = (v > 0.0 ? std::floor (v * levels) : std::ceil (v * levels)) / levels;
                const double a = std::min (1.35, std::abs (q));
                const double a2 = a * a, a3 = a2 * a, a4 = a3 * a, a5 = a4 * a;
                const double num = a * (1.001983 - 1.383312 * a + 1.161835 * a2 - 1.326116 * a3 + 0.916885 * a4);
                const double den = std::max (0.18, 1.0 - 1.297165 * a + 0.509567 * a2 + 0.482476 * a3 - 1.188457 * a4 + 0.874658 * a5);
                wet = ((q >= 0.0 ? 1.0 : -1.0) * (num / den)) / (1.0 + 0.7 * p);
                break;
            }
            case 12: // FracS&H (BitBoy fraksiyonel sample & hold aliaser)
            {
                const double P = 1.0 + (sr / 48000.0) * (2.0 + 44.0 * p * p);
                if (st.shCount >= P) { st.shCount -= P; st.shHeld = x; }
                st.shCount += 1.0;
                wet = st.shHeld;
                break;
            }
            case 13: // AllpassFM (APMod ZC2 pitch-takipli 800Hz 2. derece allpass FM)
            {
                if (x > 0.0316)
                {
                    if (st.zc2State <= 0 && st.zc2Cnt >= 10) { st.zc2HalfP = std::min (st.zc2Cnt, 2400); st.zc2Cnt = 0; }
                    st.zc2State = 1;
                }
                else if (x < -0.0316)
                {
                    if (st.zc2State >= 0 && st.zc2Cnt >= 10) { st.zc2HalfP = std::min (st.zc2Cnt, 2400); st.zc2Cnt = 0; }
                    st.zc2State = -1;
                }
                ++st.zc2Cnt;
                const double f0 = std::clamp (sr / (2.0 * static_cast<double> (std::max (12, st.zc2HalfP))), 50.0, 3000.0);
                st.apPh = std::fmod (st.apPh + (4.0 + 680.0 * p * p) * (1.0 + stereoSkew * 0.04) / sr, 1.0);
                const double tBW = std::tan (1000.0 * kPi / sr);
                const double aCoef = (tBW - 1.0) / (tBW + 1.0);
                const double cosPh = std::cos (kTwoPi * st.apPh);
                double apSig = x;
                static constexpr double kDepths[3] = { 180.0, 320.0, 520.0 };
                static constexpr double kDivs[3]   = { 1.0, 1.5, 2.2 };
                for (int s = 0; s < 3; ++s)
                {
                    const double freq = std::clamp ((f0 * (0.45 + 0.55 * st.apPh) + kDepths[s] * cosPh) / kDivs[s], 30.0, sr * 0.42);
                    const double cCoef = -std::cos (kTwoPi * freq / sr) * (1.0 - aCoef);
                    const double y = -aCoef * apSig + cCoef * st.apX1[s] + st.apX2[s] - cCoef * st.apY1[s] + aCoef * st.apY2[s];
                    st.apX2[s] = st.apX1[s]; st.apX1[s] = apSig;
                    st.apY2[s] = st.apY1[s]; st.apY1[s] = std::clamp (y, -4.0, 4.0);
                    apSig = st.apY1[s];
                }
                wet = apSig * 0.85;
                break;
            }
            case 14: // AtanBeam (APMod 6-dal Schroeder + paralel atan difuzor)
            {
                st.apPh = std::fmod (st.apPh + (1.2 + 8.0 * p) / sr, 1.0);
                const double dDrive = 1.2 + 2.6 * p;
                const double inp = x + 0.14 * st.apLastTap;
                constexpr double gSch = 0.52;
                double sum = 0.0;
                for (int br = 0; br < 6; ++br)
                {
                    const double v1 = st.apSch[br][0]; st.apSch[br][0] = inp + gSch * v1; const double s1 = v1 - inp;
                    const double v2 = st.apSch[br][1]; st.apSch[br][1] = s1 + gSch * v2; const double s2 = v2 - s1;
                    st.apDel[br][st.apDelW] = static_cast<float> (s2);
                    const double dly = 34.0 + br * 54.0 + (st.apPh + stereoSkew * 0.12) * kAPModKMod[br] * 180.0;
                    double rp = static_cast<double> (st.apDelW) - dly;
                    while (rp < 0.0) rp += 1024.0;
                    const int i0 = static_cast<int> (rp) & 1023;
                    const int i1 = (i0 + 1) & 1023;
                    const double fr = rp - std::floor (rp);
                    const double tap = st.apDel[br][i0] + (st.apDel[br][i1] - st.apDel[br][i0]) * fr;
                    if (br == 5) st.apLastTap = tap;
                    sum += std::atan (kAPModKSat[br] * dDrive * tap * 2.5);
                }
                st.apDelW = (st.apDelW + 1) & 1023;
                wet = sum * 0.72;
                break;
            }
            case 15: // Jitter440 (JitterCart 440Hz FM gecikme + DSOne gurultulu drive)
            default:
            {
                st.jitBuf[st.jitW] = static_cast<float> (x * (1.0 + nextUniformNoise (rng) * 0.34 * p));
                st.jitPh = std::fmod (st.jitPh + (12.0 + 410.0 * p * p) * (1.0 + stereoSkew * 0.05) / sr, 1.0);
                const double dly = std::clamp (32.0 + (2.0 + 28.0 * p)
                                               * (std::sin (kTwoPi * st.jitPh) * 0.72 + nextUniformNoise (rng) * 0.38 * p),
                                               1.0, 240.0);
                double rp = static_cast<double> (st.jitW) - dly;
                while (rp < 0.0) rp += 256.0;
                const int i0 = static_cast<int> (rp) & 255;
                const int i1 = (i0 + 1) & 255;
                const double fr = rp - std::floor (rp);
                wet = st.jitBuf[i0] + (st.jitBuf[i1] - st.jitBuf[i0]) * fr;
                st.jitW = (st.jitW + 1) & 255;
                break;
            }
        }

        const double blend = std::min (1.0, p * 4.5);
        return std::tanh ((x * (1.0 - blend) + wet * blend) * 1.02);
    }
} // namespace CrystalDSP
