#pragma once
#include "AudioLimits.h"
#include "BoundedMidi.h"
#include "IpcPipe.h"
#include <array>
#include <cstring>

namespace lightHostModern::worker
{
// Shared POD only: no pointers, STL containers or process-local locks. Ownership
// moves using Windows interlocked operations (also the publication barriers).
inline constexpr LONG protocol = 3, slots = 3, midiBytes = 64 * 1024;
enum Ownership : LONG { empty, writing, pending, processing, complete, reading };
struct alignas(64) Block
{
    volatile LONG owner = empty;
    LONG midiSize = 0;
    LONG64 sequence = 0;
    LONG frames = 0, channels = 0;
    std::array<unsigned char, midiBytes> midi{};
};
struct alignas(64) Header
{
    LONG version = protocol, blockSize = 0, channels = 0;
    volatile LONG fault = 0, latency = 0;
    volatile LONG64 stateRevision = 0;
    volatile LONG64 processed = 0;
    volatile LONG64 droppedMidi = 0;
    std::array<Block, slots> blocks;
};
inline LONG read(volatile LONG& value) noexcept { return InterlockedCompareExchange(&value, 0, 0); }
inline LONG64 read(volatile LONG64& value) noexcept { return InterlockedCompareExchange64(&value, 0, 0); }
inline size_t mappingSize(int channels, int frames)
{
    audioLimits::format(channels, frames, 48000);
    const auto bytes = audioLimits::add(sizeof(Header), audioLimits::multiply(
        audioLimits::multiply(static_cast<size_t>(channels), static_cast<size_t>(frames)), sizeof(float) * slots));
    if (bytes > audioLimits::maximumBufferBytes) throw std::length_error("Isolated plugin audio exceeds 64 MiB");
    return bytes;
}
inline float* audio(Header& header, int slot, int channels, int frames) noexcept
{
    return reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(&header) + sizeof(Header))
        + static_cast<size_t>(slot) * channels * frames;
}
class Mapping
{
public:
    Mapping(const juce::String& name, int channels, int frames, bool create)
        : size(mappingSize(channels, frames)),
          file(create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                       static_cast<DWORD>(size), name.toWideCharPointer())
                      : OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.toWideCharPointer()))
    {
        if (!file || (create && GetLastError() == ERROR_ALREADY_EXISTS)) throw std::runtime_error("Cannot create isolated audio mapping");
        view = static_cast<Header*>(MapViewOfFile(file.get(), FILE_MAP_ALL_ACCESS, 0, 0, size));
        if (!view) throw std::runtime_error("Cannot map isolated audio");
        if (create) { new (view) Header(); view->channels = channels; view->blockSize = frames; }
        else if (view->version != protocol || view->channels != channels || view->blockSize != frames) {
            UnmapViewOfFile(view); view = nullptr; throw std::runtime_error("Incompatible isolated audio mapping");
        }
    }
    ~Mapping() { if (view) UnmapViewOfFile(view); }
    Mapping(const Mapping&) = delete;
    Header& header() const noexcept { return *view; }
    const size_t size;
private:
    ipc::Handle file;
    Header* view = nullptr;
};
// Validate the worker's packed MIDI before exposing any of it to JUCE. JUCE's
// native MidiBuffer representation is not used as a wire contract.
inline bool unpackMidi(const Block& block, juce::MidiBuffer& output, int capacity) noexcept
{
    if (block.midiSize < 0 || block.midiSize > midiBytes) return false;
    int offset = 0, previous = -1;
    while (offset < block.midiSize) {
        if (block.midiSize - offset < 8) return false;
        int32_t sample = 0, bytes = 0;
        std::memcpy(&sample, block.midi.data() + offset, 4); std::memcpy(&bytes, block.midi.data() + offset + 4, 4);
        offset += 8;
        if (sample < previous || sample < 0 || sample >= block.frames || bytes <= 0 || bytes > 65535 || bytes > block.midiSize - offset) return false;
        if (bytes + 6 <= capacity - output.data.size()) output.addEvent(block.midi.data() + offset, bytes, sample);
        offset += bytes; previous = sample;
    }
    return true;
}
inline uint64_t packMidi(Block& block, const juce::MidiBuffer& input) noexcept
{
    block.midiSize = 0; uint64_t dropped = 0;
    for (const auto event : input) {
        if (event.samplePosition < 0 || event.samplePosition >= block.frames || event.numBytes <= 0 || event.numBytes > 65535) continue;
        if (event.numBytes + 8 > midiBytes - block.midiSize) { ++dropped; continue; }
        const int32_t sample = event.samplePosition, bytes = event.numBytes;
        auto* p = block.midi.data() + block.midiSize;
        std::memcpy(p, &sample, 4); std::memcpy(p + 4, &bytes, 4); std::memcpy(p + 8, event.data, bytes);
        block.midiSize += bytes + 8;
    }
    return dropped;
}
}
