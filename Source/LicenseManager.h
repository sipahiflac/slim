#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstdint>
#include <iterator>

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

    // =========================================================================
    // ONLINE VERSION CHECK (2G / admin-sba-8f92 Supabase Edge Function)
    // =========================================================================
    struct UpdateCheckResult
    {
        bool ok = false;
        bool newer = false;
        juce::String latest;
        juce::String downloadUrl;
        juce::String notes;
    };

    inline bool parseVersionTriple (const juce::String& text, int (&parts)[3])
    {
        const auto pieces = juce::StringArray::fromTokens (text.trim(), ".", {});
        if (pieces.size() != 3) return false;
        for (int i = 0; i < 3; ++i)
        {
            if (pieces[i].isEmpty() || ! pieces[i].containsOnly ("0123456789") || pieces[i].length() > 3)
                return false;
            parts[i] = pieces[i].getIntValue();
        }
        return true;
    }

    inline bool isOwnDownloadPage (const juce::String& link)
    {
        if (! link.startsWith ("https://")) return false;
        const auto host = juce::URL (link).getDomain().toLowerCase();
        return host == "scrapbrainaudio.com" || host.endsWith (".scrapbrainaudio.com");
    }

    inline UpdateCheckResult checkForUpdateSync (const juce::String& currentVersion)
    {
        UpdateCheckResult res;
        res.latest = currentVersion;

        // Dev test hook (2G ile ayni: SCRAPBRAIN_FAKE_UPDATE=1.0.9)
        const auto fake = juce::SystemStats::getEnvironmentVariable ("SCRAPBRAIN_FAKE_UPDATE", {}).trim();
        int fakeParts[3] {}, curParts[3] {};
        if (fake.isNotEmpty() && parseVersionTriple (fake, fakeParts) && parseVersionTriple (currentVersion, curParts)
            && std::lexicographical_compare (std::begin (curParts), std::end (curParts), std::begin (fakeParts), std::end (fakeParts)))
        {
            res.ok = true;
            res.newer = true;
            res.latest = fake;
            res.downloadUrl = "https://download.scrapbrainaudio.com/Wildin.zip";
            res.notes = "Simulated update";
            return res;
        }

        auto* body = new juce::DynamicObject();
        body->setProperty ("product", "wildin");
       #if JUCE_MAC
        body->setProperty ("platform", "macos");
       #else
        body->setProperty ("platform", "windows");
       #endif
        body->setProperty ("version", currentVersion);

        static constexpr const char* kUpdateEndpoint = "https://vwsjdelvvrlwbdixxccp.supabase.co/functions/v1/melodragon-update";
        int status = 0;
        juce::MemoryBlock response;
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd ("POST")
            .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json\r\n")
            .withConnectionTimeoutMs (9000)
            .withNumRedirectsToFollow (0)
            .withStatusCode (&status);

        if (auto stream = juce::URL (kUpdateEndpoint).withPOSTData (juce::JSON::toString (juce::var (body))).createInputStream (options))
            stream->readIntoMemoryBlock (response, 4096);

        if (status != 200 || response.getSize() == 0)
            return res;

        const auto answer = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (response.getData()),
                                                                        static_cast<int> (response.getSize())));
        if (! answer.isObject() || ! static_cast<bool> (answer["ok"]))
            return res;

        int current[3] {}, latest[3] {};
        const auto latestText = answer["latest"].toString();
        const auto link = answer["url"].toString();
        const bool newer = static_cast<bool> (answer["newer"])
            && parseVersionTriple (currentVersion, current)
            && parseVersionTriple (latestText, latest)
            && std::lexicographical_compare (std::begin (current), std::end (current), std::begin (latest), std::end (latest))
            && isOwnDownloadPage (link);

        res.ok = true;
        res.newer = newer;
        res.latest = newer ? latestText : currentVersion;
        res.downloadUrl = newer ? link : juce::String {};
        res.notes = newer ? answer["notes"].toString().substring (0, 60) : juce::String {};
        return res;
    }
} // namespace CrystalLicense
