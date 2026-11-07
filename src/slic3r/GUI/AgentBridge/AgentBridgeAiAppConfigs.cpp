#include "AgentBridgeAiAppConfigs.hpp"

#include <sstream>
#include <stdexcept>

#include <boost/filesystem/path.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;
namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// Ordered, so a written config keeps its keys where the user and the app put them.
template<class Json> Json parse_object(const std::string &text)
{
    if (text.find_first_not_of(" \t\r\n") == std::string::npos)
        return Json::object();
    Json config = Json::parse(text);
    if (!config.is_object())
        throw std::runtime_error("the config is not a JSON object");
    return config;
}

std::string trim(const std::string &text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return "";
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

// TOML basic strings ("...") in `text`, in order. Literal strings ('...') too. Stops at a comment.
std::vector<std::string> toml_strings(const std::string &text)
{
    std::vector<std::string> strings;
    for (size_t i = 0; i < text.size(); ++i) {
        const char quote = text[i];
        if (quote == '#')
            break;
        if (quote != '"' && quote != '\'')
            continue;
        std::string value;
        for (++i; i < text.size() && text[i] != quote; ++i) {
            if (quote == '"' && text[i] == '\\' && i + 1 < text.size()) {
                const char next = text[++i];
                value += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            } else
                value += text[i];
        }
        strings.push_back(value);
    }
    return strings;
}
} // namespace

McpEntry studio_entry(const std::string &binary, const std::string &data_dir, const std::string &default_data_dir)
{
    McpEntry entry{binary, {"--mcp-stdio"}};
    // "/a/b/" normalizes to "/a/b/.": drop that, so a trailing slash is the same folder.
    const auto normal = [](const std::string &dir) {
        fs::path path = fs::path(dir).lexically_normal();
        return path.filename_is_dot() ? path.parent_path() : path;
    };
    if (normal(data_dir) != normal(default_data_dir))
        entry.args.insert(entry.args.end(), {"--datadir", data_dir});
    return entry;
}

std::optional<McpEntry> read_json_entry(const std::string &text)
{
    const json config  = parse_object<json>(text);
    const auto servers = config.find("mcpServers");
    if (servers == config.end() || !servers->is_object() || !servers->contains(MCP_ENTRY_NAME))
        return std::nullopt;
    const json &server = (*servers)[MCP_ENTRY_NAME];
    McpEntry    entry;
    if (server.is_object() && server.contains("command") && server["command"].is_string())
        entry.command = server["command"].get<std::string>();
    if (server.is_object() && server.contains("args") && server["args"].is_array())
        for (const json &arg : server["args"])
            entry.args.push_back(arg.is_string() ? arg.get<std::string>() : arg.dump());
    return entry;
}

std::optional<McpEntry> read_toml_entry(const std::string &text)
{
    std::istringstream      in(text);
    std::optional<McpEntry> entry;
    for (std::string line; std::getline(in, line);) {
        const std::string trimmed = trim(line);
        if (!trimmed.empty() && trimmed.front() == '[') {
            if (entry)
                break; // the next table: the entry has ended
            if (trimmed == "[mcp_servers.bambu-studio]" || trimmed == "[mcp_servers.\"bambu-studio\"]")
                entry = McpEntry{};
            continue;
        }
        if (!entry)
            continue;
        const auto equals = trimmed.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key   = trim(trimmed.substr(0, equals));
        const auto        value = toml_strings(trimmed.substr(equals + 1));
        if (key == "command" && value.size() == 1)
            entry->command = value.front();
        else if (key == "args")
            entry->args = value;
    }
    return entry;
}

ConnectionState compare_entry(const std::optional<McpEntry> &found, const McpEntry &expected)
{
    if (!found)
        return ConnectionState::NotConnected;
    return *found == expected ? ConnectionState::Connected : ConnectionState::Different;
}

std::string with_json_entry(const std::string &text, const McpEntry &entry)
{
    auto config = parse_object<nlohmann::ordered_json>(text);
    if (!config.contains("mcpServers") || !config["mcpServers"].is_object()) {
        if (config.contains("mcpServers"))
            throw std::runtime_error("mcpServers is not an object");
        config["mcpServers"] = nlohmann::ordered_json::object();
    }
    config["mcpServers"][MCP_ENTRY_NAME] = {{"command", entry.command}, {"args", entry.args}};
    return config.dump(2) + "\n";
}

std::string json_snippet(const McpEntry &entry)
{
    return json({{"mcpServers", {{MCP_ENTRY_NAME, {{"command", entry.command}, {"args", entry.args}}}}}}).dump(2);
}

}}} // namespace Slic3r::GUI::AgentBridge
