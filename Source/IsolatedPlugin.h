#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace lightHostModern
{
// One absolute deadline covers every recapture needed by a host mutation.
// The injected monotonic clock also makes expiry testable without sleeping.
class IsolatedCaptureBarrier
{
public:
    enum class Status { waiting, ready, timedOut };
    template<class Clock, class Prepare> Status poll(Clock now, Prepare prepare) {
        const bool start = !active;
        if (start) { active = true; deadline = now() + 30000; }
        if (now() >= deadline) return Status::timedOut;
        const bool ready = prepare(start);
        if (now() >= deadline) return Status::timedOut;
        return ready ? Status::ready : Status::waiting;
    }
    void reset() noexcept { active = false; deadline = 0; }
private:
    bool active = false;
    uint64_t deadline = 0;
};

// Control lives off the audio and application message threads. This is process
// crash containment, not a security sandbox for untrusted native code.
class IsolatedPluginSession : public std::enable_shared_from_this<IsolatedPluginSession>
{
public:
    IsolatedPluginSession(const juce::PluginDescription&, const juce::String& savedState, double rate, int block,
                          const juce::File& workerExecutable = {}, bool testFixture = false, const juce::var& busLayout = {});
    ~IsolatedPluginSession();
    bool initialized() const;
    juce::var busInventory() const;
    bool configureBuses(const juce::var& layout, const juce::var& previous);
    bool layoutChanged() const;
    juce::String failure() const;
    std::unique_ptr<juce::AudioPluginInstance> createProxy(); // main thread, after initialized
    void showEditor();
    struct CaptureResult {
        uint64_t ticket = 0, revision = 0;
        juce::String state, error;
        double captureMs = 0, persistMs = 0;
    };
    uint64_t requestCapture(size_t maximumEncodedBytes = 256u * 1024 * 1024);
    bool captureFinished(uint64_t ticket) const;
    CaptureResult captureResult() const;
    uint64_t stateRevision() const;
    juce::String captureFailure() const;
    juce::String capturedState() const;
    bool poll(); // main thread; updates latency and host listener, returns status change
    bool needsPoll() const;
    static bool isProxy(const juce::AudioPluginInstance*);
    juce::var diagnostics() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl;
    class Proxy;
};
}
