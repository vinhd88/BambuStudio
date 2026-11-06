#ifndef slic3r_AgentBridgeArgs_hpp_
#define slic3r_AgentBridgeArgs_hpp_

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "AgentBridgeTypes.hpp"

// Readers for tool arguments. Bad input is reported as BridgeError invalid_args.
namespace Slic3r { namespace GUI { namespace AgentBridge {

inline std::string string_arg(const nlohmann::json &args, const char *name, const std::string &fallback = std::string())
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return fallback;
    if (!it->is_string())
        throw BridgeError(400, "invalid_args", std::string(name) + " must be a string");
    return it->get<std::string>();
}

inline bool bool_arg(const nlohmann::json &args, const char *name, bool fallback)
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return fallback;
    if (!it->is_boolean())
        throw BridgeError(400, "invalid_args", std::string(name) + " must be true or false");
    return it->get<bool>();
}

// Reads an optional 1-based number and returns it 0-based, or -1 if absent. Throws invalid_args
// if it is not a positive integer, and not_found if it exceeds `count`.
inline int optional_index_arg(const nlohmann::json &args, const char *name, size_t count)
{
    const auto it = args.find(name);
    if (it == args.end() || it->is_null())
        return -1;
    if (!it->is_number_integer() || it->get<int64_t>() < 1)
        throw BridgeError(400, "invalid_args", std::string(name) + " must be a whole number starting at 1");
    const int64_t number = it->get<int64_t>();
    if (static_cast<uint64_t>(number) > count)
        throw BridgeError(404, "not_found", std::string(name) + " " + std::to_string(number) + " does not exist; there are " + std::to_string(count));
    return static_cast<int>(number - 1);
}

}}} // namespace Slic3r::GUI::AgentBridge

#endif
