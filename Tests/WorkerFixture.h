#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <Windows.h>

class WorkerFixture final : public juce::AudioPluginInstance
{
public:
    ~WorkerFixture() override { if (captureWriteBlocker != INVALID_HANDLE_VALUE) CloseHandle(captureWriteBlocker); }
    explicit WorkerFixture(const juce::PluginDescription& d) : AudioPluginInstance(layout(d)), description(d) {
        fault("create");
        if (d.name == "descendant") {
            const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
            auto command = std::wstring(("\"" + exe + "\" --fixture-child").toWideCharPointer());
            STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
            if (!CreateProcessW(exe.toWideCharPointer(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child))
                throw std::runtime_error("Cannot spawn descendant fixture");
            descendant = child.dwProcessId; CloseHandle(child.hThread); CloseHandle(child.hProcess);
        }
    }
    static BusesProperties layout(const juce::PluginDescription& d) {
        auto buses = BusesProperties();
        if (d.name.contains("sidechain")) return buses.withInput("Main", juce::AudioChannelSet::stereo(), true)
            .withInput("Sidechain", juce::AudioChannelSet::mono(), true).withInput("Disabled", juce::AudioChannelSet::stereo(), false)
            .withOutput("Main", juce::AudioChannelSet::stereo(), true).withOutput("Auxiliary", juce::AudioChannelSet::mono(), true);
        if (d.numInputChannels) buses = buses.withInput("Input", juce::AudioChannelSet::discreteChannels(d.numInputChannels), true);
        return buses.withOutput("Output", juce::AudioChannelSet::discreteChannels(d.numOutputChannels), true);
    }
    void fault(const char* stage) const {
        if (description.name == juce::String("crash-") + stage) TerminateProcess(GetCurrentProcess(), 0xee);
        if (description.name == juce::String("hang-") + stage) Sleep(INFINITE);
    }
    void fillInPluginDescription(juce::PluginDescription& d) const override { d = description; }
    const juce::String getName() const override { return description.name; }
    void prepareToPlay(double, int) override { fault("prepare"); }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) override {
        if (captureWaiting.exchange(false)) {
            gain = 3;
            updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
        }
        fault("process"); audio.applyGain(gain);
        if (description.name == "invalid-audio") audio.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
        if (description.name == "dynamic-latency") setLatencySamples(64);
        if (description.name == "dynamic-channels" && !changedLayout) {
            changedLayout = true; auto next = getBusesLayout();
            next.inputBuses.set(0,juce::AudioChannelSet::mono()); next.outputBuses.set(0,juce::AudioChannelSet::mono());
            setBusesLayout(next);
        }
    }
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    bool acceptsMidi() const override { return true; } bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { fault("editor"); return new juce::GenericAudioProcessorEditor(*this); }
    void getStateInformation(juce::MemoryBlock& state) override {
        fault("capture");
        if (description.name == "large-state") { state.setSize(4u * 1024 * 1024); std::memset(state.getData(), 0x5a, state.getSize()); }
        else if (descendant) state.replaceAll(&descendant, sizeof(descendant));
        else state.replaceAll(&gain, sizeof(gain));
    }
    void afterStateCapture(const juce::File& directory) {
        if (description.name == "capture-write-failure") {
            if (captureWriteBlocker == INVALID_HANDLE_VALUE)
                captureWriteBlocker = CreateFileW(directory.getChildFile("capture.bin").getFullPathName().toWideCharPointer(),
                    GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (captureWriteBlocker == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot prepare persistence failure fixture");
        }
        if (description.name != "capture-barrier" || captureBarrierPassed) return;
        captureBarrierPassed = true;
        struct Event { HANDLE value; ~Event() { if (value) CloseHandle(value); } };
        const auto prefix = description.fileOrIdentifier;
        Event captured{OpenEventW(EVENT_MODIFY_STATE, FALSE, (prefix + "-captured").toWideCharPointer())};
        Event resume{OpenEventW(SYNCHRONIZE, FALSE, (prefix + "-resume").toWideCharPointer())};
        if (!captured.value || !resume.value) throw std::runtime_error("Missing capture test barriers");
        captureWaiting.store(true);
        SetEvent(captured.value);
        if (WaitForSingleObject(resume.value, 10000) != WAIT_OBJECT_0) throw std::runtime_error("Capture test barrier expired");
    }
    void setStateInformation(const void* data, int size) override { fault("restore"); if (size == sizeof(gain)) std::memcpy(&gain, data, sizeof(gain)); }
private:
    juce::PluginDescription description;
    bool changedLayout = false;
    bool captureBarrierPassed = false;
    std::atomic<bool> captureWaiting{false};
    float gain = 2;
    DWORD descendant = 0;
    HANDLE captureWriteBlocker = INVALID_HANDLE_VALUE;
};
