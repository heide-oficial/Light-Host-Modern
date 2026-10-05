#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "AudioLimits.h"

// Controller-owned allocation; capture() only accesses prepared storage. The
// ring retains recent input even at zero latency, allowing short live changes.
class DryDelay
{
public:
    bool prepare(int channels, int blockSize, int latency, double sampleRate)
    {
        lightHostModern::audioLimits::format(channels, blockSize, sampleRate);
        lightHostModern::audioLimits::latency(latency, sampleRate);
        const int transition = juce::jmax(1, static_cast<int>(sampleRate * 0.005));
        if (channels == history.getNumChannels() && blockSize == dry.getNumSamples()
            && latency == delaySamples && rate == sampleRate) return true;
        const bool compatible = rate == sampleRate && channels == history.getNumChannels()
            && latency <= validSamples;
        const int retainedDelay = compatible ? delaySamples : latency;
        using namespace lightHostModern::audioLimits;
        const auto total = add(add(static_cast<size_t>(juce::jmax(latency, retainedDelay)), blockSize), transition + 1);
        const auto bytes = multiply(multiply(add(total, blockSize), channels), sizeof(float));
        if (total > static_cast<size_t>(std::numeric_limits<int>::max()) || bytes > maximumBufferBytes)
            throw std::length_error("Dry audio buffer exceeds the 64 MiB limit");
        const int capacity = static_cast<int>(total);
        Reservation nextMemory(bytes);
        juce::AudioBuffer<float> replacement(channels, capacity);
        replacement.clear();
        juce::AudioBuffer<float> nextDry(channels, blockSize);
        nextDry.clear();
        const int retained = rate == sampleRate ? juce::jmin(validSamples, capacity - 1) : 0;
        for (int channel = 0; channel < juce::jmin(channels, history.getNumChannels()); ++channel)
            for (int age = 1; age <= retained; ++age)
                replacement.setSample(channel, capacity - age, history.getSample(channel,
                    (position + history.getNumSamples() - age) % history.getNumSamples()));
        history = std::move(replacement);
        dry = std::move(nextDry);
        memory = std::move(nextMemory);
        validSamples = retained;
        position = 0;
        oldDelaySamples = delaySamples;
        transitionRemaining = compatible && latency != delaySamples ? transition : 0;
        transitionLength = transition;
        delaySamples = latency;
        rate = sampleRate;
        return compatible;
    }

    void capture(const juce::AudioBuffer<float>& input) noexcept
    {
        const int capacity = history.getNumSamples();
        const int samples = input.getNumSamples();
        const float transitionStep = 1.0f / static_cast<float>(transitionLength);
        for (int channel = 0; channel < input.getNumChannels(); ++channel)
        {
            const auto* source = input.getReadPointer(channel);
            auto* target = dry.getWritePointer(channel);
            auto* ring = history.getWritePointer(channel);
            if (delaySamples == 0 && transitionRemaining == 0)
            {
                juce::FloatVectorOperations::copy(target, source, samples);
                const int first = juce::jmin(samples, capacity - position);
                juce::FloatVectorOperations::copy(ring + position, source, first);
                juce::FloatVectorOperations::copy(ring, source + first, samples - first);
                continue;
            }
            int write = position, read = (position + capacity - delaySamples) % capacity;
            int oldRead = transitionRemaining > 0 ? (position + capacity - oldDelaySamples) % capacity : 0;
            int remaining = transitionRemaining;
            for (int sample = 0; sample < samples; ++sample)
            {
                const auto current = delaySamples == 0 ? source[sample] : ring[read];
                if (remaining > 0)
                {
                    const auto old = oldDelaySamples == 0 ? source[sample] : ring[oldRead];
                    const auto weight = static_cast<float>(remaining--) * transitionStep;
                    target[sample] = current * (1.0f - weight) + old * weight;
                }
                else target[sample] = current;
                ring[write] = source[sample];
                if (++write == capacity) write = 0;
                if (++read == capacity) read = 0;
                if (++oldRead == capacity) oldRead = 0;
            }
        }
        position = (position + samples) % capacity;
        transitionRemaining = juce::jmax(0, transitionRemaining - samples);
        validSamples = juce::jmin(capacity - 1, validSamples + samples);
    }
    const juce::AudioBuffer<float>& output() const noexcept { return dry; }
    int channels() const noexcept { return dry.getNumChannels(); }
    size_t allocatedSamples() const noexcept { return static_cast<size_t>(history.getNumChannels()) * (history.getNumSamples() + dry.getNumSamples()); }

private:
    lightHostModern::audioLimits::Reservation memory;
    juce::AudioBuffer<float> history, dry;
    int position = 0, validSamples = 0, delaySamples = 0, oldDelaySamples = 0;
    int transitionRemaining = 0, transitionLength = 1;
    double rate = 0;
};
