#pragma once
#include <string_view>

namespace lightHostModern::ipc
{
inline constexpr int protocolVersion = 5;
enum class Arguments { none, integer, number, boolean, text, object, twoIntegers, integerBoolean, twoTexts, unknown };

inline Arguments argumentsFor(std::string_view command)
{
    using A = Arguments;
    struct Entry { std::string_view command; A arguments; };
    static constexpr Entry entries[] = {
        {"hello", A::none}, {"operation-status", A::twoTexts},
        {"snapshot", A::none}, {"state-snapshot", A::none}, {"telemetry", A::none}, {"meter-levels", A::none},
        {"snapshot-manifest", A::none}, {"snapshot-page", A::object}, {"events", A::object},
        {"transport-diagnostics", A::none},
        {"restore-all-names", A::none}, {"factory-reset", A::object}, {"rename-audio-devices", A::object}, {"rename-audio-channel", A::object}, {"operating-state", A::none}, {"operating-command", A::object}, {"routing-meters", A::none},
        {"verbose-log-status", A::none}, {"set-verbose-logs", A::boolean},
        {"stop-verbose-logs", A::none}, {"complete-verbose-logs", A::text}, {"restart-host", A::object},
        {"measure-callbacks", A::twoIntegers}, {"callback-measurement", A::none},
        {"select-audio-device", A::object}, {"audio-device-options", A::text}, {"set-preferred-audio-device", A::object},
        {"update-enabled-audio-choices", A::object}, {"enabled-audio-choices", A::none}, {"remove-missing-known-plugins", A::none},
        {"plugin-scan-status", A::none}, {"cancel-plugin-scan", A::none}, {"retry-plugin-scan", A::none},
        {"plugin-scan-failures", A::object}, {"retry-plugin-scan-selection", A::object},
        {"known-plugin-details", A::text},
        {"instance-details", A::text}, {"plugin-buses", A::text},
        {"begin-plugin-scan", A::none},
        {"clear-known-plugins", A::none}, {"retry-audio-device", A::none},
        {"delete-plugin-states", A::none}, {"quit-host", A::none}, {"flush-session", A::none},
        {"toggle-bypass", A::text}, {"remove-plugin", A::text},
        {"duplicate-plugin", A::text}, {"move-plugin-up", A::text},
        {"move-plugin-down", A::text}, {"open-plugin-editor", A::text},
        {"add-known-plugin", A::text}, {"remove-known-plugin", A::text},
        {"open-known-plugin-location", A::text}, {"set-audio-backend", A::integer},
        {"set-audio-input", A::integer}, {"set-audio-output", A::integer},
        {"set-buffer-size", A::integer}, {"set-input-channels", A::integer},
        {"set-output-channels", A::integer}, {"set-audio-persistence-retry-seconds", A::integer},
        {"set-audio-persistence-retry-attempts", A::integer},
        {"set-audio-persistence-custom-backend", A::integer},
        {"set-audio-persistence-custom-input", A::integer},
        {"set-audio-persistence-custom-output", A::integer},
        {"remove-blocked-audio-backend", A::integer}, {"remove-blocked-audio-device", A::integer},
        {"set-sample-rate", A::number},
        {"set-all-input-channels", A::boolean}, {"set-all-output-channels", A::boolean},
        {"set-start-with-windows", A::boolean}, {"set-enable-vst2", A::boolean},
        {"set-mono-inputs", A::object}, {"set-mono-output", A::object}, {"set-global-mute", A::boolean}, {"set-global-bypass", A::boolean},
        {"set-diagnostics-enabled", A::boolean}, {"rename-known-plugin", A::twoTexts},
        {"reset-clipping", A::object}, {"rename-plugin", A::twoTexts},
        {"set-audio-persistence-mode", A::text}, {"block-audio-backend", A::text},
        {"block-audio-input", A::text}, {"block-audio-output", A::text},
        {"scan-default-plugins", A::text}, {"scan-plugin-path", A::text},
        {"scan-plugin-roots", A::object},
        {"notify-release", A::text}, {"set-close-behavior", A::text}, {"set-tray-icon-mode", A::text},
        {"move-plugin-to", A::twoTexts}, {"swap-plugin-with", A::twoTexts},
        {"set-input-channel", A::integerBoolean}, {"set-output-channel", A::integerBoolean},
        {"set-enabled-audio-backend", A::integerBoolean}, {"set-enabled-audio-device", A::integerBoolean}
    };
    for (const auto& entry : entries)
        if (entry.command == command) return entry.arguments;
    return A::unknown;
}

inline bool isReadOnly(std::string_view command)
{
    return command == "hello" || command == "operation-status" || command == "snapshot"
        || command == "state-snapshot" || command == "telemetry" || command == "meter-levels"
        || command == "snapshot-manifest" || command == "snapshot-page" || command == "events"
        || command == "transport-diagnostics"
        || command == "operating-state" || command == "routing-meters"
        || command == "verbose-log-status"
        || command == "callback-measurement"
        || command == "audio-device-options"
        || command == "enabled-audio-choices" || command == "plugin-scan-status"
        || command == "plugin-scan-failures" || command == "known-plugin-details" || command == "instance-details" || command == "plugin-buses";
}
}
