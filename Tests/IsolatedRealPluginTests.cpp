#include "IsolatedPlugin.h"
#include "BoundedInput.h"
#include "PluginWorkerProtocol.h"
#include <iostream>
#include <thread>
#include <shellapi.h>

void lightHostModernLog(const juce::String&) {}
void setLightHostModernCrashContext(const juce::String&) {}
using namespace juce;
using namespace lightHostModern;
namespace {
void require(bool result, const char* error) { if (!result) throw std::runtime_error(error); }
template<class Predicate> void await(Predicate ready) {
    const auto deadline = GetTickCount64() + 20000;
    while (!ready()) { if (GetTickCount64() > deadline) throw std::runtime_error("Isolated real plugin timed out"); Thread::sleep(5); }
}
}
int main()
{
    ScopedJuceInitialiser_GUI initialization;
    int count = 0; auto** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (count != 5) { LocalFree(args); return 2; }
    const File workerFile{String(args[1])}, module{String(args[3])}, report{String(args[4])}; const String formatName(args[2]); LocalFree(args);
    auto result = var(new DynamicObject()); auto& fields = *result.getDynamicObject(); fields.setProperty("module", module.getFullPathName());
    try {
        // This opt-in runner also runs under a process deadline. Discovery never
        // loads the module into a user's running host or touches their profiles.
        AudioPluginFormatManager manager; addDefaultFormatsToManager(manager); AudioPluginFormat* format = nullptr;
        for (auto* candidate : manager.getFormats()) if (candidate->getName() == formatName) format = candidate;
        require(format, "Plugin format unavailable"); OwnedArray<PluginDescription> plugins; format->findAllTypesForFile(plugins, module.getFullPathName());
        require(plugins.size() > 0, "No plugin classes found");
        Array<var> classes;
        for (const auto* description : plugins) {
            IsolatedPluginSession session(*description, {}, 48000, 256, workerFile);
            await([&] { return session.initialized() || session.failure().isNotEmpty(); }); require(session.failure().isEmpty(), session.failure().toRawUTF8());
            auto proxy = session.createProxy(); proxy->setRateAndBufferSizeDetails(48000, 256); proxy->prepareToPlay(48000, 256);
            await([&] { return session.poll() || session.failure().isNotEmpty(); }); require(session.failure().isEmpty(), session.failure().toRawUTF8());
            session.showEditor(); Thread::sleep(100); require(session.failure().isEmpty(), session.failure().toRawUTF8());
            const int channels = jmax(1, jmax(proxy->getTotalNumInputChannels(), proxy->getTotalNumOutputChannels()));
            AudioBuffer<float> audio(channels, 256); MidiBuffer midi; midi.ensureSize(worker::midiBytes);
            double maximumMs = 0, energy = 0;
            for (int block = 0; block < 200; ++block) {
                for (int c = 0; c < channels; ++c) for (int i = 0; i < 256; ++i) audio.setSample(c, i, .05f * std::sin(static_cast<float>((block * 256 + i) * .03)));
                const auto start = Time::getMillisecondCounterHiRes(); proxy->processBlock(audio, midi);
                maximumMs = jmax(maximumMs, Time::getMillisecondCounterHiRes() - start);
                require(!audioLimits::sanitize(audio), "Invalid audio escaped worker containment"); energy += audio.getMagnitude(0, 256);
                session.poll(); Thread::sleep(6);
            }
            require(session.failure().isEmpty(), session.failure().toRawUTF8()); require(energy > 0, "No output from real plugin");
            const auto ticket = session.requestCapture(); await([&] { return session.captureFinished(ticket); }); require(session.failure().isEmpty(), session.failure().toRawUTF8());
            require(session.captureFailure().isEmpty(), session.captureFailure().toRawUTF8());
            const auto state = session.capturedState(); size_t decoded = 0; require(validPluginState(state, decoded), "Invalid captured state");
            IsolatedPluginSession restored(*description, state, 48000, 256, workerFile);
            await([&] { return restored.initialized() || restored.failure().isNotEmpty(); }); require(restored.initialized(), restored.failure().toRawUTF8());
            auto metrics = session.diagnostics();
            require(static_cast<int64>(metrics.getProperty("underruns", 200)) < 190, "Real plugin never produced enough worker blocks; dry fallback is not a processing pass");
            metrics.getDynamicObject()->setProperty("name", description->name);
            metrics.getDynamicObject()->setProperty("maximumProxyCallbackMs", maximumMs); metrics.getDynamicObject()->setProperty("stateBytes", static_cast<int64>(decoded)); classes.add(metrics);
        }
        fields.setProperty("classes", classes); fields.setProperty("passed", true); report.replaceWithText(JSON::toString(result)); std::cout << JSON::toString(result) << '\n'; return 0;
    } catch (const std::exception& e) { fields.setProperty("passed", false); fields.setProperty("error", String::fromUTF8(e.what())); report.replaceWithText(JSON::toString(result)); std::cerr << e.what() << '\n'; return 1; }
}
