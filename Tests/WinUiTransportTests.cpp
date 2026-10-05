#include "HostTransport.h"
#include <future>
#include <iostream>

using namespace lightHostModern::ipc;
static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
#include "WinUiStateScenarios.h"
#include "UiCoordinationScenarios.h"
int main()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try
    {
        const auto document = R"({"diagnostics":{"deviceName":"\u65e5\u672c [x] } : \"z\"","cpuUsagePercent":1e-3},"activePlugins":[{"name":"[a]}"}],"flags":[true,false]})";
        require(extractString(document, "deviceName") == winrt::to_string(L"\u65e5\u672c [x] } : \"z\""), "Windows parser must decode Unicode and delimiters");
        require(extractNumber(document, "cpuUsagePercent") == 0.001, "exponent number parsing");
        require(extractArray(document, "activePlugins").Size() == 1, "brackets inside names must not end arrays");
        require(extractBoolArray(document, "flags") == std::vector<bool>({true, false}), "typed arrays");
        require(extractString("{malformed", "status", "fallback") == "fallback", "malformed response fallback");
        auto ids = extractArray(encodeRequest("swap-plugin-with:first:second", L"test"), "args");
        require(ids.GetStringAt(0) == L"first" && ids.GetStringAt(1) == L"second", "reordering encodes string IDs");

        const auto name = L"\\\\.\\pipe\\LightHostModern-ui-test-" + std::to_wstring(GetCurrentProcessId());
        Handle server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 1000, nullptr));
        require(static_cast<bool>(server), "create fake host");
        std::promise<void> cancellationReady;
        auto worker = std::async(std::launch::async, [&] {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            const char* commands[] = {"set-input-channel", "snapshot", "remove-plugin", "operation-status", "snapshot"};
            for (int i = 0; i < 5; ++i)
            {
                PipeIo io(server.get(), nullptr, 5000);
                require(io.connect() == ERROR_SUCCESS, "accept UI transport");
                std::string request;
                require(io.read(request) == ERROR_SUCCESS, "read versioned UI request");
                require(extractString(request, "command") == commands[i], "commands must arrive in FIFO order without automatic mutation resend");
                require(extractNumber(request, "version") == protocolVersion, "request version");
                if (i == 2 || i == 4)
                {
                    if (i == 2)
                        require(extractArray(request, "args").GetStringAt(0) == L"0123456789abcdef0123456789abcdef", "mutation retains instance ID");
                    if (i == 4) cancellationReady.set_value();
                    std::string receipt;
                    require(io.read(receipt) != ERROR_SUCCESS, "timed out or cancelled client must disconnect without resending");
                    DisconnectNamedPipe(server.get());
                    continue;
                }
                if (i == 0)
                {
                    auto args = extractArray(request, "args");
                    require(args.GetNumberAt(0) == 31 && args.GetBooleanAt(1), "typed UI command arguments");
                }
                auto response = parseObject(request);
                response.SetNamedValue(L"status", JsonValue::CreateStringValue(L"ok"));
                response.SetNamedValue(L"hostSession", JsonValue::CreateStringValue(L"fake-session"));
                response.SetNamedValue(L"large", JsonValue::CreateStringValue(std::wstring(300000, L'x')));
                require(io.write(winrt::to_string(response.Stringify())) == ERROR_SUCCESS, "large response");
                std::string receipt;
                require(io.read(receipt) == ERROR_SUCCESS && receipt == "received", "client must acknowledge complete response");
                DisconnectNamedPipe(server.get());
            }
        });
        auto state = std::make_shared<ClientState>();
        state->hostSession = "fake-session";
        auto first = requestAsync(state, name, "set-input-channel:31:1");
        auto second = requestAsync(state, name, "snapshot");
        require(extractString(winrt::to_string(first.get()), "status") == "ok", "first async response");
        require(extractString(winrt::to_string(second.get()), "large").size() == 300000, "complete response beyond old UI buffer");
        require(requestAsync(state, name, "remove-plugin:0123456789abcdef0123456789abcdef", 100).get().empty(), "timeout must report unknown result without resending mutation");
        auto pending = requestAsync(state, name, "snapshot");
        require(cancellationReady.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready, "pending request reached fake host");
        state->close();
        require(pending.get().empty(), "closing client cancels a pending response read");
        worker.get();
        require(requestAsync(state, name, "snapshot").get().empty(), "closed client rejects queued work");
        runStateScenarios();
        runUiCoordinationScenarios();
        runQueuedDeadlineScenario();
        std::cout << "WinUI JSON, bounded FIFO, and paged state transport regressions passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; return 1; }
}
