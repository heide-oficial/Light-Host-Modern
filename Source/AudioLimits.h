#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <atomic>
#include <utility>

namespace lightHostModern::audioLimits
{
inline constexpr size_t maximumBufferBytes = 64u * 1024 * 1024;
inline constexpr size_t maximumDryBytes = 256u * 1024 * 1024;
// Includes old and candidate buffers while controller transactions coexist.
// Third-party allocations are deliberately not represented as host allocations.
class Reservation
{
public:
    Reservation() = default;
    explicit Reservation(size_t bytes) { grow(bytes); }
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
    Reservation(Reservation&& other) noexcept : held(std::exchange(other.held, 0)) {}
    Reservation& operator=(Reservation&& other) noexcept
    {
        total.fetch_sub(held); held = std::exchange(other.held, 0); return *this;
    }
    ~Reservation() { total.fetch_sub(held); }
    void grow(size_t bytes)
    {
        auto current = total.load();
        do {
            if (bytes > maximumDryBytes || current > maximumDryBytes - bytes)
                throw std::length_error("Host audio buffers exceed the 256 MiB aggregate budget");
        } while (!total.compare_exchange_weak(current, current + bytes));
        held += bytes;
    }
private:
    inline static std::atomic<size_t> total{0};
    size_t held = 0;
};
inline size_t add(size_t a, size_t b)
{
    if (b > std::numeric_limits<size_t>::max() - a) throw std::length_error("Audio buffer size overflow");
    return a + b;
}
inline size_t multiply(size_t a, size_t b)
{
    if (b && a > std::numeric_limits<size_t>::max() / b) throw std::length_error("Audio buffer size overflow");
    return a * b;
}
inline int maximumLatency(double rate)
{
    if (!std::isfinite(rate) || rate < 8000 || rate > 768000) throw std::invalid_argument("Unsupported audio sample rate");
    return static_cast<int>(rate * 10);
}
inline int latency(int samples, double rate)
{
    if (samples < 0 || samples > maximumLatency(rate)) throw std::length_error("Plugin latency exceeds the ten-second limit");
    return samples;
}
inline void format(int channels, int block, double rate)
{
    maximumLatency(rate);
    if (channels < 1 || channels > 256 || block < 1 || block > 1048576)
        throw std::invalid_argument("Unsupported audio buffer dimensions");
}
// No clipping: finite signals above unity are valid. No logging or allocation.
inline bool sanitize(juce::AudioBuffer<float>& buffer) noexcept
{
    bool invalid = false;
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        auto* samples = buffer.getWritePointer(c);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            if (!std::isfinite(samples[s])) { samples[s] = 0; invalid = true; }
    }
    return invalid;
}
}
