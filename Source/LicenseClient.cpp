#include "LicenseClient.h"
#include "AppPaths.h"
#include <algorithm>
#include <cstring>
#include <iterator>

#if JUCE_MAC
 #include <CommonCrypto/CommonDigest.h>
 #include <CoreFoundation/CoreFoundation.h>
 #include <IOKit/IOKitLib.h>
 #include <sys/stat.h>
#endif

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wincred.h>
 #include <bcrypt.h>
#endif

namespace
{
juce::var reply(const juce::String& status, const juce::String& message = {})
{
    auto* object = new juce::DynamicObject();
    object->setProperty("status", status);
    if (message.isNotEmpty()) object->setProperty("message", message);
    return juce::var(object);
}

juce::String canonicalCode(const juce::String& input)
{
    const auto trimmed = input.trim().toUpperCase();
    auto compact = trimmed.removeCharacters(" -\t\r\n");
    if (compact.length() > 20 && compact.startsWith("2G")) compact = compact.substring(2);
    if (compact.length() == 20 && compact.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"))
        return "2G" + compact;
    if (trimmed.length() < 8 || trimmed.length() > 64) return {};
    for (const auto ch : trimmed)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) return {};
    return trimmed;
}

#if JUCE_MAC
juce::File macLicenseFile(const juce::String& service)
{
    return appDataFolder().getChildFile("license-" + juce::URL(service).getDomain() + ".key");
}
#endif

juce::String credentialName(const juce::String& service)
{
    // Separate staging and production credentials. Neither the code nor this
    // target name is ever written into host project state.
    const auto host = juce::URL(service).getDomain();
    return "ScrapBrainAudio/Melodragon2G/" + host;
}
}

juce::String LicenseClient::deviceId()
{
    // Shared across the standalone, plug-in, Windows users and app reinstalls.
    // Only a namespaced SHA-256 digest leaves native code; no raw registry ID
    // is exposed to JavaScript, project files or request logs.
    static const juce::String cached = []() -> juce::String
    {
       #if JUCE_WINDOWS
        wchar_t value[128]{};
        DWORD bytes = sizeof(value);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value, &bytes) != ERROR_SUCCESS)
            return {};
        const auto guid = juce::String(value).trim().toLowerCase().removeCharacters("{}-");
        if (guid.length() != 32 || !guid.containsOnly("0123456789abcdef")) return {};
        const auto input = "Melodragon2G/device/v1:" + guid;
        BCRYPT_ALG_HANDLE provider{};
        if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
        unsigned char digest[32]{};
        const auto status = BCryptHash(provider, nullptr, 0,
            reinterpret_cast<PUCHAR>(const_cast<char*>(input.toRawUTF8())),
            static_cast<ULONG>(input.getNumBytesAsUTF8()), digest, sizeof(digest));
        BCryptCloseAlgorithmProvider(provider, 0);
        if (status != 0) return {};
        return juce::String::toHexString(digest, sizeof(digest), 0).toLowerCase();
       #elif JUCE_MAC
        // The hardware UUID, the macOS counterpart of Windows' MachineGuid. Only its namespaced digest leaves here.
        const auto service = IOServiceGetMatchingService(0, IOServiceMatching("IOPlatformExpertDevice"));
        if (service == 0) return {};
        juce::String uuid;
        if (auto property = (CFStringRef) IORegistryEntryCreateCFProperty(service, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0))
        {
            char buffer[128]{};
            if (CFStringGetCString(property, buffer, sizeof(buffer), kCFStringEncodingUTF8)) uuid = buffer;
            CFRelease(property);
        }
        IOObjectRelease(service);
        const auto guid = uuid.trim().toLowerCase().removeCharacters("{}-");
        if (guid.length() != 32 || !guid.containsOnly("0123456789abcdef")) return {};
        const auto input = "Melodragon2G/device/v1:" + guid;
        unsigned char digest[CC_SHA256_DIGEST_LENGTH]{};
        CC_SHA256(input.toRawUTF8(), (CC_LONG) input.getNumBytesAsUTF8(), digest); // juce::SHA256 is not in juce_core
        return juce::String::toHexString(digest, sizeof(digest), 0).toLowerCase();
       #else
        return {};
       #endif
    }();
    return cached;
}

juce::String LicenseClient::endpoint()
{
   #ifdef SCRAPBRAIN_LICENSE_API_URL
    auto service = juce::String(SCRAPBRAIN_LICENSE_API_URL).trim();
   #else
    auto service = juce::SystemStats::getEnvironmentVariable("SCRAPBRAIN_LICENSE_API_URL", "https://vwsjdelvvrlwbdixxccp.supabase.co/functions/v1/melodragon-license").trim();
   #endif
    while (service.endsWithChar('/')) service = service.dropLastCharacters(1);
    // Activation credentials must never be sent over plaintext HTTP.
    if (!service.startsWithIgnoreCase("https://") || juce::URL(service).getDomain().isEmpty()) return {};
    return service;
}

juce::String LicenseClient::readStoredCode(const juce::String& service)
{
   #if JUCE_WINDOWS
    const auto targetName = credentialName(service);
    PCREDENTIALW credential{};
    if (!CredReadW(targetName.toWideCharPointer(), CRED_TYPE_GENERIC, 0, &credential)) return {};
    const auto code = juce::String::fromUTF8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                              (int) credential->CredentialBlobSize);
    CredFree(credential);
    return canonicalCode(code);
   #elif JUCE_MAC
    // A file in the user's Application Support folder, readable only by them. The Keychain would ask for a
    // password whenever the standalone app and a DAW (two different programs) read each other's item.
    return canonicalCode(macLicenseFile(service).loadFileAsString());
   #else
    juce::ignoreUnused(service);
    return {};
   #endif
}

bool LicenseClient::storeCode(const juce::String& service, const juce::String& code)
{
   #if JUCE_WINDOWS
    const auto targetName = credentialName(service);
    const auto utf8 = code.toRawUTF8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(targetName.toWideCharPointer());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(utf8));
    credential.CredentialBlobSize = (DWORD) std::strlen(utf8);
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"2G");
    return CredWriteW(&credential, 0) != 0;
   #elif JUCE_MAC
    const auto file = macLicenseFile(service);
    file.getParentDirectory().createDirectory();
    if (!file.replaceWithText(code)) return false;
    return chmod(file.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR) == 0; // 0600
   #else
    juce::ignoreUnused(service, code);
    return false;
   #endif
}

juce::var LicenseClient::request(const juce::String& service, const juce::String& action,
                                  const juce::String& code, juce::String* issuedCode)
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty("licenseCode", code);
    payload->setProperty("action", action);
    payload->setProperty("deviceId", deviceId());
    int httpStatus = 0;
    auto url = juce::URL(service).withPOSTData(juce::JSON::toString(juce::var(payload)));
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withHttpRequestCmd("POST")
        .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n")
        .withConnectionTimeoutMs(12000).withNumRedirectsToFollow(0).withStatusCode(&httpStatus);
    juce::MemoryBlock response;
    if (auto stream = url.createInputStream(options)) stream->readIntoMemoryBlock(response, 16384);

    if (httpStatus == 429) return reply("rate_limited", "Wait a moment");
    if (httpStatus == 409) return reply("device_conflict", "Free code already linked to this device");
    if (httpStatus == 428) return reply("device_required", "Device identification required");
    if (httpStatus == 401 || httpStatus == 403) return reply("invalid", "Code not accepted");
    if (httpStatus != 200) return reply("offline", "Service unavailable");

    const auto decoded = juce::JSON::parse(juce::String::fromUTF8((const char*) response.getData(), (int) response.getSize()));
    if (!decoded.isObject() || !(bool) decoded["ok"] || !decoded.hasProperty("plan"))
        return reply("offline", "Service unavailable");

    auto* result = new juce::DynamicObject();
    result->setProperty("status", "active");
    result->setProperty("plan", decoded["plan"].toString());
    result->setProperty("validUntil", decoded["validUntil"].isString() ? decoded["validUntil"].toString() : juce::String());
    result->setProperty("unlimited", (bool)decoded["unlimited"]);
    if (decoded["credits"].isInt()) result->setProperty("credits", (int) decoded["credits"]);
    if (issuedCode != nullptr) *issuedCode = canonicalCode(decoded["licenseCode"].toString());
    return juce::var(result);
}

juce::var LicenseClient::registerFree(const juce::String& service)
{
    juce::String issued;
    auto result = request(service, "register", {}, &issued);
    if (result["status"].toString() != "active")
        return result["status"].toString() == "device_conflict" ? reply("unlicensed") : result; // conflict: this device has a code from an e-mail, ask for it
    if (issued.isEmpty()) return reply("offline", "Service unavailable");
    if (!storeCode(service, issued)) return reply("storage_error", "Could not save code");
    return result;
}

juce::var LicenseClient::activate(const juce::String& enteredCode)
{
    const auto service = endpoint();
    if (service.isEmpty()) return reply("setup", "Service not configured");
    const auto code = canonicalCode(enteredCode);
    if (code.isEmpty()) return reply("invalid", "Check the code");
    auto result = request(service, "activate", code);
    if (result["status"].toString() == "active" && !storeCode(service, code))
        return reply("storage_error", "Could not save code");
    return result;
}

juce::var LicenseClient::checkStatus()
{
    const auto service = endpoint();
    if (service.isEmpty()) return reply("setup", "Service not configured");
    const auto code = readStoredCode(service);
    if (code.isEmpty()) return registerFree(service);
    auto result = request(service, "status", code);
    // A Studio code that has run out (trial over, subscription ended) drops the device back to its own Free licence
    // instead of leaving the app on "code not valid". The Studio code can be entered again after renewing.
    if (result["status"].toString() == "invalid")
    {
        auto free = registerFree(service);
        if (free["status"].toString() == "active") return free;
    }
    return result;
}

juce::var LicenseClient::submitFeedback(const juce::String& installationId,
                                        const juce::String& messageId, int choice)
{
    const auto service = endpoint();
    if (service.isEmpty()) return reply("offline");
    const auto canonicalId = installationId.trim().toLowerCase();
    if (canonicalId.length() != 36 || !canonicalId.containsOnly("0123456789abcdef-")
        || (messageId != "first-impression" && messageId != "next-improvement"
            && messageId != "chat-experience-v1" && messageId != "chat-synthesis-v1"
            && messageId != "chat-source-v1" && messageId != "chat-source-v2")
        || choice < 0 || choice > (messageId == "chat-source-v2" ? 4 : 3)) return reply("invalid");

    auto* body = new juce::DynamicObject();
    body->setProperty("installationId", canonicalId);
    body->setProperty("messageId", messageId);
    body->setProperty("choice", choice);
    const auto target = service.upToLastOccurrenceOf("/", true, false) + "melodragon-feedback";
    int status = 0;
    juce::MemoryBlock response;
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withHttpRequestCmd("POST")
        .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n")
        .withConnectionTimeoutMs(9000).withNumRedirectsToFollow(0).withStatusCode(&status);
    if (auto stream = juce::URL(target).withPOSTData(juce::JSON::toString(juce::var(body))).createInputStream(options))
        stream->readIntoMemoryBlock(response, 1024);
    if (status != 200) return reply("offline");
    const auto result = juce::JSON::parse(juce::String::fromUTF8((const char*)response.getData(), (int)response.getSize()));
    if (!result.isObject() || !(bool)result["ok"]) return reply("offline");
    auto* success = new juce::DynamicObject();
    success->setProperty("ok", true);
    return juce::var(success);
}

// The server authorizes every generation. Developer privileges never come from this client.
juce::MemoryBlock LicenseClient::generate(const juce::String& text, float influence, juce::String& error,
                                          const GenerationOptions& tuning, juce::String* soundKind)
{
    juce::MemoryBlock audio;
    juce::StringPairArray responseHeaders;
    const auto service = endpoint();
    if (service.isEmpty()) { error = "License service not configured"; return audio; }
    const auto code = readStoredCode(service);
    if (code.isEmpty()) { error = "Activate a license first"; return audio; }
    auto* body = new juce::DynamicObject();
    body->setProperty("licenseCode", code); body->setProperty("text", text);
    body->setProperty("deviceId", deviceId());
    body->setProperty("requestId", juce::Uuid().toDashedString()); body->setProperty("promptInfluence", influence);
    body->setProperty("durationSeconds", tuning.durationSeconds); body->setProperty("loop", tuning.loop); body->setProperty("enhance", tuning.enhance);
    const auto target = service.upToLastOccurrenceOf("/", true, false) + "melodragon-generate";
    int status = 0;
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withHttpRequestCmd("POST").withExtraHeaders("Content-Type: application/json\r\nAccept: audio/mpeg\r\n")
        .withConnectionTimeoutMs(80000).withNumRedirectsToFollow(0).withStatusCode(&status)
        .withResponseHeaders(&responseHeaders);
    if (auto stream = juce::URL(target).withPOSTData(juce::JSON::toString(juce::var(body))).createInputStream(options))
        stream->readIntoMemoryBlock(audio, 10000001);
    if (status != 200 || audio.getSize() == 0 || audio.getSize() > 10000000)
    {
        const auto failure = status == 409
            ? juce::JSON::parse(juce::String::fromUTF8((const char*)audio.getData(),
                  (int)std::min<size_t>(audio.getSize(), 4096))) : juce::var();
        error = status == 402 ? "No credits" : status == 401 ? "Check your license" : status == 429 ? "Try again later"
              : status == 428 ? "Device identification required"
              : status == 409 && failure["error"].toString() == "device_conflict" ? "Free code already linked"
              : status == 409 ? "Generation already running" : "Generation failed - try again";
        audio.reset();
    }
    else if (soundKind != nullptr)
        *soundKind = responseHeaders.getValue("x-sound-kind", {}); // what the server's rewrite decided this sound is
    return audio;
}

namespace
{
#ifndef TWO_G_VERSION
 #define TWO_G_VERSION "0.0.0"
#endif
juce::CriticalSection updateLock;
juce::String updatePage; // the last checked download page, opened only by openUpdatePage()

// "1.2.3" -> comparable triple; anything else is not a version.
bool parseVersion(const juce::String& text, int (&parts)[3])
{
    const auto pieces = juce::StringArray::fromTokens(text.trim(), ".", {});
    if (pieces.size() != 3) return false;
    for (int i = 0; i < 3; ++i)
    {
        if (pieces[i].isEmpty() || !pieces[i].containsOnly("0123456789") || pieces[i].length() > 3) return false;
        parts[i] = pieces[i].getIntValue();
    }
    return true;
}

// Only an https page on scrapbrainaudio.com (or a subdomain) is ever opened, whatever the server says.
bool isOwnDownloadPage(const juce::String& link)
{
    if (!link.startsWith("https://")) return false;
    const auto host = juce::URL(link).getDomain().toLowerCase();
    return host == "scrapbrainaudio.com" || host.endsWith(".scrapbrainaudio.com");
}
}

juce::var LicenseClient::checkForUpdate()
{
    auto* result = new juce::DynamicObject();
    result->setProperty("ok", false);
    result->setProperty("current", TWO_G_VERSION);
    // Development only: SCRAPBRAIN_FAKE_UPDATE=9.9.9 makes the app behave as if the server had announced that version,
    // so the Update screen and its Get key can be seen without publishing anything. Unset, this does nothing.
    const auto fake = juce::SystemStats::getEnvironmentVariable("SCRAPBRAIN_FAKE_UPDATE", {}).trim();
    int fakeParts[3] {}, runningParts[3] {};
    if (fake.isNotEmpty() && parseVersion(fake, fakeParts) && parseVersion(TWO_G_VERSION, runningParts)
        && std::lexicographical_compare(std::begin(runningParts), std::end(runningParts), std::begin(fakeParts), std::end(fakeParts)))
    {
        {
            const juce::ScopedLock lock(updateLock);
            updatePage = "https://download.scrapbrainaudio.com/2G.zip";
        }
        result->setProperty("ok", true);
        result->setProperty("newer", true);
        result->setProperty("latest", fake);
        result->setProperty("notes", "Simulated update (dev)");
        return juce::var(result);
    }
    const auto service = endpoint();
    if (service.isEmpty()) return juce::var(result);

    auto* body = new juce::DynamicObject();
   #if JUCE_MAC
    body->setProperty("platform", "macos");
   #else
    body->setProperty("platform", "windows");
   #endif
    body->setProperty("version", TWO_G_VERSION);
    const auto target = service.upToLastOccurrenceOf("/", true, false) + "melodragon-update";
    int status = 0;
    juce::MemoryBlock response;
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withHttpRequestCmd("POST")
        .withExtraHeaders("Content-Type: application/json\r\nAccept: application/json\r\n")
        .withConnectionTimeoutMs(9000).withNumRedirectsToFollow(0).withStatusCode(&status);
    if (auto stream = juce::URL(target).withPOSTData(juce::JSON::toString(juce::var(body))).createInputStream(options))
        stream->readIntoMemoryBlock(response, 4096);
    if (status != 200) return juce::var(result);
    const auto answer = juce::JSON::parse(juce::String::fromUTF8((const char*)response.getData(), (int)response.getSize()));
    if (!answer.isObject() || !(bool)answer["ok"]) return juce::var(result);

    int current[3] {}, latest[3] {};
    const auto latestText = answer["latest"].toString();
    const auto link = answer["url"].toString();
    const bool newer = (bool)answer["newer"] && parseVersion(TWO_G_VERSION, current) && parseVersion(latestText, latest)
        && std::lexicographical_compare(std::begin(current), std::end(current), std::begin(latest), std::end(latest))
        && isOwnDownloadPage(link);
    {
        const juce::ScopedLock lock(updateLock);
        updatePage = newer ? link : juce::String();
    }
    result->setProperty("ok", true);
    result->setProperty("newer", newer);
    result->setProperty("latest", newer ? latestText : juce::String(TWO_G_VERSION));
    if (newer) result->setProperty("notes", answer["notes"].toString().substring(0, 60));
    return juce::var(result);
}

bool LicenseClient::openUpdatePage()
{
    juce::String link;
    {
        const juce::ScopedLock lock(updateLock);
        link = updatePage;
    }
    return isOwnDownloadPage(link) && juce::URL(link).launchInDefaultBrowser();
}
