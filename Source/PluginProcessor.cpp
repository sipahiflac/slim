#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MechClicks.h"

CrystalAudioProcessor::CrystalAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WildinParams", createParameterLayout())
{
    loadPresetsFromDisk();
    if (! savedPresets.empty())
        applyPreset (savedPresets.front(), false);

    activeLicenseKey = CrystalLicense::loadSavedKey();
    licensed.store (activeLicenseKey.isNotEmpty(), std::memory_order_release);
}

bool CrystalAudioProcessor::tryActivateLicense (const juce::String& key)
{
    const auto norm = CrystalLicense::normalizeKey (key);
    if (! CrystalLicense::validateKey (norm)) return false;
    CrystalLicense::saveKey (norm);
    activeLicenseKey = norm;
    licensed.store (true, std::memory_order_release);
    triggerMechanicalClick (1);
    return true;
}

void CrystalAudioProcessor::deactivateLicense()
{
    CrystalLicense::clearSavedKey();
    activeLicenseKey.clear();
    licensed.store (false, std::memory_order_release);
    triggerMechanicalClick (-1);
}

juce::AudioProcessorValueTreeState::ParameterLayout CrystalAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // DAW / MIDI kontrolcu isimleri: temiz geometrik & muzikal isimler (seed haric!)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "knob0", 1 }, "Square", 0.0f, 1.0f, 0.55f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "knob1", 1 }, "Triangle", 0.0f, 1.0f, 0.45f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "knob2", 1 }, "Circle", 0.0f, 1.0f, 0.40f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "drive", 1 }, "Mix", 0.0f, 1.0f, 0.40f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tilt", 1 }, "Filter", -1.0f, 1.0f, 0.0f));

    return { params.begin(), params.end() };
}

void CrystalAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSr = sampleRate > 1000.0 ? sampleRate : 44100.0;
    for (auto& ch : channels) ch.reset();
    sK[0] = *apvts.getRawParameterValue ("knob0");
    sK[1] = *apvts.getRawParameterValue ("knob1");
    sK[2] = *apvts.getRawParameterValue ("knob2");
    sDrive = *apvts.getRawParameterValue ("drive");
    sTilt  = *apvts.getRawParameterValue ("tilt");

    // Decode embedded custom click.wav once (native 44.1 kHz!)
    juce::WavAudioFormat wavFormat;
    for (int i = 0; i < 3; ++i)
    {
        auto stream = std::make_unique<juce::MemoryInputStream> (
            MechClicks::kAllClicks[i], MechClicks::kAllClickSizes[i], false);
        if (std::unique_ptr<juce::AudioFormatReader> reader (wavFormat.createReaderFor (stream.release(), true)); reader != nullptr)
        {
            clickBuffers[static_cast<size_t> (i)].setSize (1, static_cast<int> (reader->lengthInSamples));
            reader->read (&clickBuffers[static_cast<size_t> (i)], 0, static_cast<int> (reader->lengthInSamples), 0, true, false);
        }
    }
    activeClickStep = MechClicks::kClickSampleRate / currentSr;
}

bool CrystalAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    return true;
}

void CrystalAudioProcessor::triggerMechanicalClick (int dir) noexcept
{
    const int idx = clickRR.fetch_add (1, std::memory_order_relaxed) % 3;
    clickReqIdx.store (dir >= 0 ? idx : (idx + 3), std::memory_order_release);
}

void CrystalAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = std::min (2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (numCh <= 0 || numSamples <= 0) return;

    const double tK0 = *apvts.getRawParameterValue ("knob0");
    const double tK1 = *apvts.getRawParameterValue ("knob1");
    const double tK2 = *apvts.getRawParameterValue ("knob2");
    const double tDrive = *apvts.getRawParameterValue ("drive");
    const double tTilt  = *apvts.getRawParameterValue ("tilt");
    const int seedVal   = currentSeed.load (std::memory_order_relaxed);

    const auto dec = CrystalDSP::decodeSeedToChain (seedVal);
    const int m0 = dec.chain[0], m1 = dec.chain[1], m2 = dec.chain[2];
    const double skewBase = (static_cast<double> (dec.flavor) - 4.0) * 0.25;
    constexpr double sc = 0.004;

    // Katmanlar arasi 30 ms RMS enerji kilidi + hafif 10.5 kHz analog tiz torpuleme katsayilari
    const double rmsAlpha   = 1.0 - std::exp (-1.0 / (0.030 * currentSr));
    const double gainSmooth = 1.0 - std::exp (-1.0 / (0.025 * currentSr));
    const double midHiAlpha = 1.0 - std::exp (-6.283185307179586 * 13500.0 / currentSr);
    const double outHiAlpha = 1.0 - std::exp (-6.283185307179586 * 10500.0 / currentSr);

    // Check tactile mechanical click request
    const int req = clickReqIdx.exchange (-1, std::memory_order_acquire);
    if (req >= 0)
    {
        activeClickIdx = req % 3;
        activeClickPos = 0.0;
        activeClickStep = (MechClicks::kClickSampleRate / currentSr) * (req < 3 ? 1.015 : 0.985);
    }

    float* ch0 = buffer.getWritePointer (0);
    float* ch1 = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    int sw = scopeWritePos.load (std::memory_order_relaxed);
    float peakIn = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        sK[0] += (tK0 - sK[0]) * sc;
        sK[1] += (tK1 - sK[1]) * sc;
        sK[2] += (tK2 - sK[2]) * sc;
        sDrive += (tDrive - sDrive) * sc;
        sTilt  += (tTilt  - sTilt)  * sc;

        const double mixWet = licensed.load (std::memory_order_relaxed) ? std::clamp (sDrive, 0.0, 1.0) : 0.0; // Lisanssizken temiz dry bypass, lisansliyken 0..100% Dry/Wet!
        const double inGain = 1.65; // Sabit saglikli ic surus (dry/wet karisimiyla temiz kontrol)
        const double lpAlpha = (sTilt < -0.05) ? std::max (0.035, 1.0 - std::pow ((-sTilt - 0.05) / 0.95, 0.5) * 0.96) : 1.0;
        const double hpAlpha = (sTilt >  0.05) ? std::min (0.42,  std::pow (( sTilt - 0.05) / 0.95, 1.45) * 0.42) : 0.0;

        float clickSample = 0.0f;
        if (activeClickIdx >= 0)
        {
            const auto& cb = clickBuffers[static_cast<size_t> (activeClickIdx)];
            const int cLen = cb.getNumSamples();
            const int i0 = static_cast<int> (activeClickPos);
            if (i0 + 1 < cLen)
            {
                const float fr = static_cast<float> (activeClickPos - static_cast<double> (i0));
                const float* cd = cb.getReadPointer (0);
                clickSample = (cd[i0] + (cd[i0 + 1] - cd[i0]) * fr) * 0.28f;
                activeClickPos += activeClickStep;
            }
            else
            {
                activeClickIdx = -1;
            }
        }

        double outL = 0.0, outR = 0.0;
        for (int c = 0; c < numCh; ++c)
        {
            auto& st = channels[c];
            const double skew = (c == 0) ? -skewBase : skewBase;
            const float rawIn = (c == 0) ? ch0[i] : ch1[i];
            peakIn = std::max (peakIn, std::abs (rawIn));
            const double dry = static_cast<double> (rawIn);
            double s = dry * inGain;

            if (lpAlpha < 0.999) { st.lp1 += (s - st.lp1) * lpAlpha; st.lp2 += (st.lp1 - st.lp2) * lpAlpha; s = st.lp2; }
            else { st.lp1 = st.lp2 = s; }
            if (hpAlpha > 0.001) { st.hp1 += (s - st.hp1) * hpAlpha; s -= st.hp1; }
            else { st.hp1 = 0.0; }

            auto applyEnergyLock = [&] (double sig, double& rmsSq, double& smoothG) noexcept
            {
                rmsSq += (sig * sig - rmsSq) * rmsAlpha;
                const double rms = std::sqrt (rmsSq + 1e-9);
                // Hedef 0.38 RMS (sicak & canli sweet-spot), [0.52..1.48] sinirli -> 3 katman da net duyulur!
                const double targetG = (rms > 0.015) ? std::clamp (0.38 / rms, 0.52, 1.48) : 1.0;
                smoothG += (targetG - smoothG) * gainSmooth;
                return sig * smoothG;
            };

            s = CrystalDSP::processAtomicModule (s, st.slots[0], m0, sK[0], currentSr, skew, dspRng);
            s = applyEnergyLock (s, st.stageRmsSq[0], st.stageGain[0]);

            s = CrystalDSP::processAtomicModule (s, st.slots[1], m1, sK[1], currentSr, skew, dspRng);
            s = applyEnergyLock (s, st.stageRmsSq[1], st.stageGain[1]);

            // Slot 2 oncesi cok hafif 13.5 kHz ultrasonik temizlik (aliasing katlanmasini azaltir)
            st.midHiLp += (s - st.midHiLp) * midHiAlpha;
            s = st.midHiLp;

            s = CrystalDSP::processAtomicModule (s, st.slots[2], m2, sK[2], currentSr, skew, dspRng);

            // Cok hafif 10.5 kHz analog tiz torpuleme (canliligi korur, jilet gibi dijital tizleri yumusatir)
            st.outHiLp += (s - st.outHiLp) * outHiAlpha;
            s = st.outHiLp;

            const double dcOut = s - st.dcOutX1 + 0.995 * st.dcOutY1;
            st.dcOutX1 = s; st.dcOutY1 = dcOut;
            const double wet = std::tanh (dcOut * 0.92) * 0.88;
            const double y = dry * (1.0 - mixWet) + wet * mixWet + static_cast<double> (clickSample);

            if (c == 0) { ch0[i] = static_cast<float> (y); outL = y; }
            else        { ch1[i] = static_cast<float> (y); outR = y; }
        }

        scopeRing[sw] = static_cast<float> (numCh > 1 ? (outL + outR) * 0.5 : outL);
        sw = (sw + 1) & (kScopeSize - 1);
    }

    scopeWritePos.store (sw, std::memory_order_release);
    lastInputLevel.store (peakIn, std::memory_order_relaxed);
}

int CrystalAudioProcessor::getCurrentSeed() const noexcept
{
    return currentSeed.load (std::memory_order_relaxed);
}

void CrystalAudioProcessor::pushHistory()
{
    CrystalPreset cur;
    cur.seed     = getCurrentSeed();
    cur.knobs[0] = *apvts.getRawParameterValue ("knob0");
    cur.knobs[1] = *apvts.getRawParameterValue ("knob1");
    cur.knobs[2] = *apvts.getRawParameterValue ("knob2");
    cur.drive    = *apvts.getRawParameterValue ("drive");
    cur.tilt     = *apvts.getRawParameterValue ("tilt");
    history.push_back (cur);
    if (history.size() > 24) history.erase (history.begin());
}

void CrystalAudioProcessor::applyPreset (const CrystalPreset& p, bool pushUndo)
{
    if (pushUndo) pushHistory();

    auto setParam = [this] (const char* id, float val)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (val));
    };

    currentSeed.store (p.seed & 32767, std::memory_order_relaxed);
    setParam ("knob0", p.knobs[0]);
    setParam ("knob1", p.knobs[1]);
    setParam ("knob2", p.knobs[2]);
    setParam ("drive", p.drive);
    setParam ("tilt",  p.tilt);
}

void CrystalAudioProcessor::mutateChain()
{
    static constexpr int fam0[6] = { 0, 1, 2, 3, 4, 5 };
    static constexpr int fam1[5] = { 6, 7, 8, 9, 10 };
    static constexpr int fam2[5] = { 11, 12, 13, 14, 15 };

    int m0 = 0, m1 = 1, m2 = 2;
    if (uiRng.nextFloat() < 0.78f)
    {
        int order[3] = { 0, 1, 2 };
        for (int i = 2; i > 0; --i) std::swap (order[i], order[uiRng.nextInt (i + 1)]);
        auto pickFam = [this] (int f)
        {
            if (f == 0) return fam0[uiRng.nextInt (6)];
            if (f == 1) return fam1[uiRng.nextInt (5)];
            return fam2[uiRng.nextInt (5)];
        };
        m0 = pickFam (order[0]);
        m1 = pickFam (order[1]);
        m2 = pickFam (order[2]);
    }
    else
    {
        int pool[16];
        for (int i = 0; i < 16; ++i) pool[i] = i;
        for (int i = 15; i > 0; --i) std::swap (pool[i], pool[uiRng.nextInt (i + 1)]);
        m0 = pool[0]; m1 = pool[1]; m2 = pool[2];
    }

    CrystalPreset next;
    next.seed     = CrystalDSP::encodeChainToSeed (m0, m1, m2, uiRng.nextInt (9));
    // Tam 0..1 genis rastgelelik (sadece ucu birden sifir olmasin diye en az biri >= 0.35)
    next.knobs[0] = uiRng.nextFloat();
    next.knobs[1] = uiRng.nextFloat();
    next.knobs[2] = uiRng.nextFloat();
    if (std::max ({ next.knobs[0], next.knobs[1], next.knobs[2] }) < 0.35f)
        next.knobs[uiRng.nextInt (3)] = 0.45f + uiRng.nextFloat() * 0.50f;
    next.drive    = 0.50f + uiRng.nextFloat() * 0.50f; // Dry/Wet: %50..%100 arasi duyulur mutasyon
    next.tilt     = (uiRng.nextFloat() - 0.5f) * 0.45f;

    applyPreset (next, true);
    triggerMechanicalClick (1);
}

void CrystalAudioProcessor::undoMutation()
{
    if (history.empty()) return;
    CrystalPreset prev = history.back();
    history.pop_back();
    applyPreset (prev, false);
    triggerMechanicalClick (-1);
}

bool CrystalAudioProcessor::isCurrentSeedSaved() const
{
    const int cur = getCurrentSeed();
    for (const auto& p : savedPresets)
        if (p.seed == cur) return true;
    return false;
}

void CrystalAudioProcessor::toggleSaveCurrentPreset()
{
    CrystalPreset cur;
    cur.seed     = getCurrentSeed();
    cur.knobs[0] = *apvts.getRawParameterValue ("knob0");
    cur.knobs[1] = *apvts.getRawParameterValue ("knob1");
    cur.knobs[2] = *apvts.getRawParameterValue ("knob2");
    cur.drive    = *apvts.getRawParameterValue ("drive");
    cur.tilt     = *apvts.getRawParameterValue ("tilt");

    bool updated = false;
    for (auto& p : savedPresets)
    {
        if (p.seed == cur.seed) { p = cur; updated = true; break; }
    }
    if (! updated)
    {
        savedPresets.insert (savedPresets.begin(), cur);
    }
    savePresetsToDisk();
    triggerMechanicalClick (1);
}

void CrystalAudioProcessor::deletePresetAt (int index)
{
    if (savedPresets.size() <= 1 || index < 0 || index >= static_cast<int> (savedPresets.size()))
        return;
    savedPresets.erase (savedPresets.begin() + index);
    savePresetsToDisk();
    triggerMechanicalClick (-1);
}

juce::File CrystalAudioProcessor::getPresetsFile() const
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    auto dir = base.getChildFile ("ScrapBrainAudio").getChildFile ("Wildin");
    dir.createDirectory();
    return dir.getChildFile ("presets.xml");
}

void CrystalAudioProcessor::loadPresetsFromDisk()
{
    savedPresets.clear();
    auto file = getPresetsFile();
    if (auto xml = juce::XmlDocument::parse (file))
    {
        for (auto* child : xml->getChildIterator())
        {
            if (child->hasTagName ("PRESET"))
            {
                CrystalPreset p;
                p.seed     = child->getIntAttribute ("seed", 0) & 32767;
                p.knobs[0] = static_cast<float> (child->getDoubleAttribute ("k0", 0.5));
                p.knobs[1] = static_cast<float> (child->getDoubleAttribute ("k1", 0.5));
                p.knobs[2] = static_cast<float> (child->getDoubleAttribute ("k2", 0.5));
                p.drive    = static_cast<float> (child->getDoubleAttribute ("drive", 0.4));
                p.tilt     = static_cast<float> (child->getDoubleAttribute ("tilt", 0.0));
                savedPresets.push_back (p);
            }
        }
    }
    if (savedPresets.empty())
    {
        savedPresets.push_back ({ CrystalDSP::encodeChainToSeed (6, 2, 13, 1),  { 0.55f, 0.45f, 0.40f }, 0.40f,  0.00f });
        savedPresets.push_back ({ CrystalDSP::encodeChainToSeed (11, 1, 8, 3),  { 0.45f, 0.60f, 0.35f }, 0.35f, -0.10f });
        savedPresets.push_back ({ CrystalDSP::encodeChainToSeed (10, 4, 14, 5), { 0.50f, 0.40f, 0.55f }, 0.45f,  0.08f });
    }
}

void CrystalAudioProcessor::savePresetsToDisk() const
{
    juce::XmlElement root ("WILDIN_PRESETS");
    for (const auto& p : savedPresets)
    {
        auto* e = root.createNewChildElement ("PRESET");
        e->setAttribute ("seed", p.seed);
        e->setAttribute ("k0", p.knobs[0]);
        e->setAttribute ("k1", p.knobs[1]);
        e->setAttribute ("k2", p.knobs[2]);
        e->setAttribute ("drive", p.drive);
        e->setAttribute ("tilt", p.tilt);
    }
    root.writeTo (getPresetsFile());
}

void CrystalAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        xml->setAttribute ("wildinSeed", currentSeed.load (std::memory_order_relaxed));
        copyXmlToBinary (*xml, destData);
    }
}

void CrystalAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            if (xml->hasAttribute ("wildinSeed"))
                currentSeed.store (xml->getIntAttribute ("wildinSeed", currentSeed.load()) & 32767, std::memory_order_relaxed);
            else if (xml->hasAttribute ("crystalSeed"))
                currentSeed.store (xml->getIntAttribute ("crystalSeed", currentSeed.load()) & 32767, std::memory_order_relaxed);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
    }
}

juce::AudioProcessorEditor* CrystalAudioProcessor::createEditor()
{
    return new CrystalAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrystalAudioProcessor();
}
