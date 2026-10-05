#pragma once
#include "UpdateContract.h"
#include <chrono>

namespace lightHostModern::update
{
inline bool installationCompleted(const std::string& state, uint32_t code)
{
    return (state == "completed" && code == 0)
        || (state == "restart_required" && (code == 3010 || code == 1641));
}
struct ApplyEnvironment
{
    virtual ~ApplyEnvironment() = default;
    virtual bool cancelled() = 0;
    virtual bool armed() = 0;
    virtual bool processesExited() = 0;
    virtual uint64_t milliseconds() = 0;
    virtual void wait() = 0;
    virtual uint32_t install() = 0;
    virtual void report(const std::string&, uint32_t) = 0;
};
inline void applyWhenReady(ApplyEnvironment& environment, uint64_t timeout = 120000)
{
    const auto started = environment.milliseconds();
    for (;;)
    {
        if (environment.cancelled()) { environment.report("cancelled", 1602); return; }
        if (environment.armed() && environment.processesExited()) break;
        if (environment.milliseconds() - started >= timeout) { environment.report("shutdown_timeout", 0); return; }
        environment.wait();
    }
    if (environment.cancelled()) { environment.report("cancelled", 1602); return; }
    environment.report("installing", 0);
    try { const auto code = environment.install(); environment.report(installerOutcome(code), code); }
    catch (const Error& error) { environment.report(error.code, 0); }
    catch (...) { environment.report("installer_start_failed", 0); }
}
}
