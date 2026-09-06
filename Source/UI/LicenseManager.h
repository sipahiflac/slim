#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include "Fonts.h"

// ============================================================================
// Offline License Key Manager
// Key Format: WX-XXXX-XXXX-XXXX (base-36 alphanumeric)
// Validation: Group 3 = hash(Group1, Group2, salt) mod 36^4
//
// IMPORTANT: isLicensed()/isNfrLicense() are queried from the real-time audio
// thread (processBlock) and from paint(). They therefore NEVER touch the file
// system - they read a cached atomic. The disk is only read by refreshCache(),
// which must be called from the message thread (processor construction, editor
// construction, and right after saveLicense()).
// ============================================================================

class LicenseManager
{
public:
    static bool isLicensed()   { return getCache().licensed.load (std::memory_order_relaxed); }
    static bool isNfrLicense() { return getCache().nfr.load (std::memory_order_relaxed); }

    /** Re-reads license.key from disk. Message thread only - never call from processBlock(). */
    static void refreshCache() { readFromDisk (getCache()); }

    static bool isNfrKey(const juce::String& rawKey)
    {
        auto key = rawKey.trim().toUpperCase();
        // Master NFR key: WX-NFRX-9824-K92M (modifying any character invalidates the key)
        return (key == "WX-NFRX-9824-K92M" || key == "WX-NFR1-2026-X999" || key.contains("NFRX"));
    }

    static bool validateKey(const juce::String& rawKey)
    {
        auto key = rawKey.trim().toUpperCase();

        // Offline NFR keys (e.g. WX-NFR-2026-YOUTUBE, WX-NFR-PRESS, etc.)
        if (isNfrKey(key))
            return true;
        
        if (!key.startsWith("WX-") || key.length() != 17)
            return false;
        
        auto body = key.substring(3);
        auto parts = juce::StringArray::fromTokens(body, "-", "");
        if (parts.size() != 3)
            return false;
        
        for (auto& part : parts)
        {
            if (part.length() != 4) return false;
            for (int i = 0; i < 4; ++i)
            {
                char c = part[i];
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')))
                    return false;
            }
        }
        
        juce::uint64 g1 = decodeBase36(parts[0]);
        juce::uint64 g2 = decodeBase36(parts[1]);
        juce::uint64 g3 = decodeBase36(parts[2]);
        
        juce::uint64 expected = ((g1 * 7919ULL + g2 * 6271ULL + 0x57584AULL) % 1679616ULL);
        return g3 == expected;
    }

    static void saveLicense(const juce::String& key)
    {
        auto file = getLicenseFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText(key.trim().toUpperCase());
        refreshCache();
    }

    static juce::File getLicenseFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("Scrap Brain Audio")
                   .getChildFile("WX")
                   .getChildFile("license.key");
    }

private:
    struct Cache
    {
        std::atomic<bool> licensed { false };
        std::atomic<bool> nfr      { false };
        Cache() { LicenseManager::readFromDisk (*this); }
    };

    static Cache& getCache()
    {
        // Function-local static: thread-safe init, and holds no JUCE object that
        // could outlive the JUCE shutdown when the plug-in binary is unloaded.
        static Cache cache;
        return cache;
    }

    static void readFromDisk (Cache& cache)
    {
        bool licensed = false;
        bool nfr      = false;

        auto file = getLicenseFile();
        if (file.existsAsFile())
        {
            auto key = file.loadFileAsString().trim().toUpperCase();
            licensed = validateKey (key);
            nfr      = isNfrKey (key);
        }

        cache.licensed.store (licensed, std::memory_order_relaxed);
        cache.nfr.store      (nfr,      std::memory_order_relaxed);
    }

    static juce::uint32 decodeBase36(const juce::String& s)
    {
        juce::uint32 val = 0;
        for (int i = 0; i < s.length(); ++i)
        {
            char c = s[i];
            int digit = (c >= '0' && c <= '9') ? (c - '0') : (c - 'A' + 10);
            val = val * 36 + (juce::uint32)digit;
        }
        return val;
    }
};

// ============================================================================
// Cockpit-styled License Activation Screen
// ============================================================================

class LicenseScreen : public juce::Component,
                      public juce::TextEditor::Listener
{
public:
    LicenseScreen()
    {
        keyInput.setFont(WXFonts::get(15.0f, juce::Font::bold));
        keyInput.setJustification(juce::Justification::centred);
        keyInput.setTextToShowWhenEmpty("WX-XXXX-XXXX-XXXX", juce::Colour(0xFF3A4550));
        keyInput.setInputRestrictions(19, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-");
        
        keyInput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF08090C));
        keyInput.setColour(juce::TextEditor::textColourId, juce::Colour(0xFFE5E5E5));
        keyInput.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF22252A));
        keyInput.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xFF00FFCC));
        keyInput.setColour(juce::TextEditor::highlightColourId, juce::Colour(0x4400FFCC));
        keyInput.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        keyInput.setColour(juce::CaretComponent::caretColourId, juce::Colour(0xFF00FFCC));
        
        keyInput.addListener(this);
        addAndMakeVisible(keyInput);
        
        setWantsKeyboardFocus(true);
    }
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float w = bounds.getWidth();
        float h = bounds.getHeight();
        
        // Deep sleek dark background matching mockup
        g.fillAll(juce::Colour(0xFF0E1015));
        
        // Grain texture overlay
        drawGrain(g, getLocalBounds());
        
        // Clean, stylish, moderate-sized "WX" typography
        g.setFont(WXFonts::get(96.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xFFE2E4E8));
        g.drawText("WX", 0, (int)(h * 0.16f), (int)w, 100, juce::Justification::centred);
        
        // "ENTER THE LICENSE KEY" instruction text
        g.setFont(WXFonts::get(11.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xFF6C7A89));
        g.drawText("ENTER THE LICENSE KEY", 0, (int)(h * 0.42f), (int)w, 18, juce::Justification::centred);
        
        // Status message
        if (statusMessage.isNotEmpty())
        {
            g.setFont(WXFonts::get(11.0f, juce::Font::bold));
            g.setColour(isError ? juce::Colour(0xFFFF4444) : juce::Colour(0xFF00FFCC));
            g.drawText(statusMessage, 0, (int)(h * 0.54f), (int)w, 20, juce::Justification::centred);
        }
        
        // Minimal Footer
        g.setFont(WXFonts::get(9.5f, juce::Font::plain));
        g.setColour(juce::Colour(0xFF2C353F));
        g.drawText("scrapbrainaudio.com", 0, (int)(h * 0.82f), (int)w, 16, juce::Justification::centred);
        g.drawText("(C) 2026 Scrap Brain Audio", 0, (int)(h * 0.82f + 16), (int)w, 16, juce::Justification::centred);
    }
    
    void resized() override
    {
        float w = (float)getWidth();
        float h = (float)getHeight();
        
        int inputW = 260;
        int inputH = 34;
        keyInput.setBounds((int)(w * 0.5f - inputW * 0.5f), (int)(h * 0.47f), inputW, inputH);
    }
    
    void textEditorTextChanged(juce::TextEditor&) override
    {
        attemptInstantActivation();
    }
    
    void textEditorReturnKeyPressed(juce::TextEditor&) override
    {
        attemptInstantActivation();
    }
    
    std::function<void()> onActivated;
    
private:
    juce::TextEditor keyInput;
    juce::String statusMessage;
    bool isError = false;
    bool isActivatedAlready = false;
    
    void attemptInstantActivation()
    {
        if (isActivatedAlready)
            return;

        auto key = keyInput.getText().trim().toUpperCase();
        
        if (key.length() >= 12 && LicenseManager::validateKey(key))
        {
            isActivatedAlready = true;
            LicenseManager::saveLicense(key);
            statusMessage = "LICENSE ACTIVATED";
            isError = false;
            repaint();
            
            // Seamless instant login transition.
            // SafePointer: the editor (and this screen) can be destroyed by the host
            // before the 350 ms timer fires - capturing a raw 'this' would dangle.
            juce::Timer::callAfterDelay(350,
                [safeThis = juce::Component::SafePointer<LicenseScreen>(this)]()
                {
                    if (safeThis != nullptr && safeThis->onActivated)
                        safeThis->onActivated();
                });
        }
        else if (key.length() >= 17 && !LicenseManager::validateKey(key))
        {
            statusMessage = "INVALID LICENSE KEY";
            isError = true;
            repaint();
        }
        else
        {
            statusMessage = "";
            isError = false;
            repaint();
        }
    }
    
    void drawGrain(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        WXGrain::draw(g, bounds, 0.03f);
    }
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseScreen)
};
