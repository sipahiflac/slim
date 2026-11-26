#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>

namespace CrystalLicense
{
    static constexpr uint64_t kMaxBlock = 1679616ULL;    // 36^4
    static constexpr uint64_t kSaltWldn = 0x574C444EULL; // 'WLDN'

    inline bool parseBase36Block (const juce::String& block, uint64_t& outVal) noexcept
    {
        if (block.length() != 4) return false;
        uint64_t val = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto ch = block[i];
            uint64_t digit = 0;
            if (ch >= '0' && ch <= '9')      digit = static_cast<uint64_t> (ch - '0');
            else if (ch >= 'A' && ch <= 'Z') digit = static_cast<uint64_t> (ch - 'A' + 10);
            else if (ch >= 'a' && ch <= 'z') digit = static_cast<uint64_t> (ch - 'a' + 10);
            else return false;
            val = val * 36ULL + digit;
        }
        outVal = val;
        return true;
    }

    inline juce::String normalizeKey (const juce::String& raw)
    {
        return raw.trim().toUpperCase().removeCharacters (" \t\r\n");
    }

    // Dogrulama: WLDN-XXXX-XXXX-XXXX (Cloudflare Worker ile birebir ayni matematik!)
    inline bool validateKey (const juce::String& rawKey)
    {
        const auto key = normalizeKey (rawKey);
        auto parts = juce::StringArray::fromTokens (key, "-", "");
        if (parts.size() != 4) return false;
        if (parts[0] != "WLDN") return false;

        uint64_t g1 = 0, g2 = 0, g3 = 0;
        if (! parseBase36Block (parts[1], g1)) return false;
        if (! parseBase36Block (parts[2], g2)) return false;
        if (! parseBase36Block (parts[3], g3)) return false;

        const uint64_t expectedG3 = (g1 * 7919ULL + g2 * 6271ULL + kSaltWldn) % kMaxBlock;
        return g3 == expectedG3;
    }

    inline juce::File getLicenseFile()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif
        auto dir = base.getChildFile ("ScrapBrainAudio").getChildFile ("Wildin");
        dir.createDirectory();
        return dir.getChildFile ("license.txt");
    }

    inline juce::String loadSavedKey()
    {
        auto file = getLicenseFile();
        if (! file.existsAsFile()) return {};
        const auto k = normalizeKey (file.loadFileAsString());
        return validateKey (k) ? k : juce::String {};
    }

    inline bool saveKey (const juce::String& rawKey)
    {
        const auto k = normalizeKey (rawKey);
        if (! validateKey (k)) return false;
        return getLicenseFile().replaceWithText (k);
    }

    inline void clearSavedKey()
    {
        auto file = getLicenseFile();
        if (file.existsAsFile()) file.deleteFile();
    }
} // namespace CrystalLicense
