#pragma once
#include "IpcSchema.h"
#include "BoundedInput.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <limits>

namespace lightHostModern::ipc
{
struct Request
{
    juce::String id;
    juce::String hostSession;
    juce::String command;
    juce::Array<juce::var> args;
    juce::String errorCode;
    juce::String errorMessage;
    explicit operator bool() const { return errorCode.isEmpty(); }
};

inline Request parseRequest(const juce::String& json)
{
    Request request;
    juce::var value;
    auto fail = [&](const char* code, const char* message) {
        request.errorCode = code; request.errorMessage = message; return request;
    };
    if (!boundedJson(json) || juce::JSON::parse(json, value).failed() || !value.isObject())
        return fail("invalid_request", "Expected a versioned JSON request; update both host and UI");
    const auto id = value["id"];
    if (value["hostSession"].isString()) request.hostSession = value["hostSession"].toString();
    if (id.isString()) request.id = id.toString();
    const auto version = value["version"];
    if (!(version.isInt() || version.isInt64()) || static_cast<int>(version) != protocolVersion)
        return fail("incompatible_version", "Host and UI protocol versions do not match");
    if (!id.isString() || request.id.isEmpty() || request.id.length() > 128)
        return fail("invalid_request", "Request id must be a non-empty string of at most 128 characters");
    if (!value["command"].isString() || !value["args"].isArray())
        return fail("invalid_request", "Expected a command string and typed argument array");
    request.command = value["command"].toString();
    request.args = *value["args"].getArray();
    const auto schema = argumentsFor(request.command.toStdString());
    if (schema == Arguments::unknown) return fail("unknown_command", "Unknown host command");
    const int count = schema == Arguments::none ? 0
        : (schema == Arguments::twoTexts || schema == Arguments::twoIntegers || schema == Arguments::integerBoolean ? 2 : 1);
    if (request.args.size() != count) return fail("invalid_arguments", "Incorrect argument count");
    if (count == 0) return request;
    const auto integer = [](const juce::var& item) {
        return (item.isInt() || item.isInt64())
            && static_cast<juce::int64>(item) >= std::numeric_limits<int>::min()
            && static_cast<juce::int64>(item) <= std::numeric_limits<int>::max();
    };
    const auto& first = request.args[0];
    bool valid = true;
    switch (schema)
    {
        case Arguments::integer: valid = integer(first); break;
        case Arguments::number: valid = (first.isDouble() || first.isInt() || first.isInt64()) && std::isfinite(static_cast<double>(first)); break;
        case Arguments::boolean: valid = first.isBool(); break;
        case Arguments::text: valid = first.isString(); break;
        case Arguments::object: valid = first.isObject(); break;
        case Arguments::twoTexts: valid = first.isString() && request.args[1].isString(); break;
        case Arguments::twoIntegers: valid = integer(first) && integer(request.args[1]); break;
        case Arguments::integerBoolean: valid = integer(first) && request.args[1].isBool(); break;
        default: break;
    }
    if (!valid) return fail("invalid_arguments", "Argument types do not match the command schema");
    return request;
}

inline juce::String errorResponse(const Request& request)
{
    auto error = new juce::DynamicObject();
    error->setProperty("code", request.errorCode);
    error->setProperty("message", request.errorMessage);
    auto response = new juce::DynamicObject();
    response->setProperty("version", protocolVersion);
    response->setProperty("id", request.id);
    response->setProperty("status", "error");
    response->setProperty("message", request.errorMessage);
    response->setProperty("error", juce::var(error));
    return juce::JSON::toString(juce::var(response), true);
}
}
