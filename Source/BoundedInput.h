#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <limits>
#include <string_view>
#include "JsonLimits.h"

namespace lightHostModern
{
// Inspect before invoking JUCE's recursive parser. Quoted delimiters and escaped
// quotes do not consume nesting. The parser still validates the JSON grammar.
inline bool boundedJson(const juce::String& text, size_t maximumBytes = 4u * 1024 * 1024)
{
    if (text.getNumBytesAsUTF8() > maximumBytes) return false;
    return boundedJsonStructure(text);
}

inline juce::var parseBoundedJson(const juce::String& text, size_t maximumBytes = 4u * 1024 * 1024)
{
    return boundedJson(text, maximumBytes) ? juce::JSON::parse(text) : juce::var();
}

inline juce::var parseBoundedJson(const juce::File& file, size_t maximumBytes = 4u * 1024 * 1024)
{
    if (!file.existsAsFile() || file.getSize() < 0 || static_cast<juce::uint64>(file.getSize()) > maximumBytes) return {};
    return parseBoundedJson(file.loadFileAsString(), maximumBytes);
}

inline constexpr size_t maximumPluginStateBytes = 192u * 1024 * 1024;
inline constexpr size_t maximumSessionStateBytes = 256u * 1024 * 1024;

// Read once into owned, bounded bytes. A concurrent writer can only make this
// snapshot invalid; the parser never reopens a path after it was validated.
inline bool readBoundedText(const juce::File& file, juce::String& text, size_t maximumBytes)
{
    juce::FileInputStream input(file);
    if (!input.openedOk() || input.getTotalLength() < 0
        || static_cast<juce::uint64>(input.getTotalLength()) > maximumBytes) return false;
    juce::MemoryOutputStream bytes;
    std::array<char, 16384> buffer{};
    for (;;) {
        const auto count = input.read(buffer.data(), static_cast<int>(buffer.size()));
        if (count < 0 || input.getStatus().failed()) return false;
        if (count == 0) break;
        if (static_cast<size_t>(count) > maximumBytes - bytes.getDataSize()) return false;
        bytes.write(buffer.data(), static_cast<size_t>(count));
    }
    text = juce::String::createStringFromData(bytes.getData(), static_cast<int>(bytes.getDataSize()));
    return true;
}

// XML snapshots carry plugin state as text. Reject DTD/entity expansion and
// excessive nesting before JUCE's recursive reader sees the document.
inline std::unique_ptr<juce::XmlElement> parseBoundedXml(const juce::String& text,
    size_t maximumBytes = maximumSessionStateBytes, size_t maximumDepth = 64, size_t maximumTags = 262144)
{
    if(text.getNumBytesAsUTF8()>maximumBytes)return {};
    const std::string_view bytes(text.toRawUTF8(),text.getNumBytesAsUTF8());size_t at=0,depth=0,tags=0;
    while((at=bytes.find('<',at))!=std::string_view::npos) {
        if(bytes.substr(at,4)=="<!--"||bytes.substr(at,9)=="<![CDATA[") {
            const auto end=bytes.find(bytes.substr(at,4)=="<!--"?"-->":"]]>",at+4);if(end==std::string_view::npos)return {};at=end+3;continue;
        }
        if(bytes.substr(at,2)=="<!")return {};
        if(bytes.substr(at,2)=="<?") {const auto end=bytes.find("?>",at+2);if(end==std::string_view::npos)return {};at=end+2;continue;}
        bool closing=at+1<bytes.size()&&bytes[at+1]=='/';char quote=0;size_t end=at+1;
        for(;end<bytes.size();++end){const char c=bytes[end];if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='>')break;}
        if(end==bytes.size()||++tags>maximumTags)return {};
        if(closing){if(!depth)return {};--depth;}
        else if(bytes[end-1]!='/'){if(++depth>maximumDepth)return {};}
        at=end+1;
    }
    return depth==0?juce::parseXML(text):nullptr;
}
inline std::unique_ptr<juce::XmlElement> parseBoundedXml(const juce::File& file,
    size_t maximumBytes = maximumSessionStateBytes, size_t maximumDepth = 64, size_t maximumTags = 262144)
{
    juce::String text;
    return readBoundedText(file, text, maximumBytes)
        ? parseBoundedXml(text, maximumBytes, maximumDepth, maximumTags) : nullptr;
}

// MemoryBlock encoding is NOT RFC Base64: decimal byte count, '.', then JUCE's
// little-endian six-bit alphabet. Validate without allocating decoded storage.
inline bool validPluginState(const juce::String& encoded, size_t& decodedBytes)
{
    decodedBytes = 0;
    if (encoded.isEmpty()) return true;
    auto p = encoded.getCharPointer();
    size_t digits = 0;
    for (; !p.isEmpty() && *p != '.'; ++p) {
        const auto c = *p;
        if (c < '0' || c > '9' || ++digits > 10) return false;
        const auto digit = static_cast<size_t>(c - '0');
        if (decodedBytes > (maximumPluginStateBytes - digit) / 10) return false;
        decodedBytes = decodedBytes * 10 + digit;
    }
    if (!digits || p.isEmpty()) return false;
    ++p;
    const size_t expected = (decodedBytes * 8 + 5) / 6;
    size_t count = 0;
    for (; !p.isEmpty(); ++p) {
        const auto c = *p;
        if (++count > expected || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '.' || c == '+')) return false;
    }
    return count == expected;
}

inline bool decodePluginState(const juce::String& encoded, juce::MemoryBlock& binary)
{
    size_t bytes = 0;
    if (!validPluginState(encoded, bytes)) return false;
    if (encoded.isEmpty()) { binary.reset(); return true; }
    return binary.fromBase64Encoding(encoded) && binary.getSize() == bytes;
}
}
