#include "AgentBridgeAiAppConnect.hpp"

#include <ctime>
#include <iterator>
#include <map>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <unistd.h>

#include "AgentBridgeAiAppConfigsCordisYaml.hpp"
#include "AgentBridgeProcessRunner.hpp"

namespace fs = boost::filesystem;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
std::string expand(const std::string &path, const std::string &home) { return path.rfind("~/", 0) == 0 ? home + path.substr(1) : path; }

bool executable(const std::string &path) { return !path.empty() && path.front() == '/' && ::access(path.c_str(), X_OK) == 0 && fs::is_regular_file(path); }

std::string read_file(const std::string &path)
{
    boost::nowide::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Beside the file and renamed into place, with the file's permissions: never half a config. A
// linked config (dotfiles) is written where the link points, so the link stays.
void write_file_atomically(const std::string &config, const std::string &text)
{
    boost::system::error_code ec;
    const std::string path = fs::is_symlink(fs::symlink_status(config, ec)) ? fs::canonical(config).string() : config;
    const std::string temp = path + ".bambu-studio-" + std::to_string(::getpid()) + ".tmp";
    try {
        {
            boost::nowide::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out.flush())
                throw std::runtime_error("cannot write " + path);
        }
        // A new config is private: Harness writes its own as 0600, and MCP entries may carry env values.
        const auto perms = fs::status(path, ec).permissions();
        fs::permissions(temp, ec ? fs::owner_read | fs::owner_write : perms);
        fs::rename(temp, path);
    } catch (...) {
        fs::remove(temp, ec);
        throw;
    }
}

// "<config>.bak-<time>", never overwriting an earlier backup. Empty if there is no config yet.
std::string back_up(const std::string &config)
{
    if (!fs::exists(config))
        return "";
    const std::time_t now = std::time(nullptr);
    std::tm           local{};
    localtime_r(&now, &local);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    std::string backup = config + ".bak-" + stamp;
    for (int n = 2; fs::exists(backup); ++n)
        backup = config + ".bak-" + stamp + "-" + std::to_string(n);
    fs::copy_file(config, backup);
    return backup;
}

// The PATH a CLI runs with: its own folder first (claude and codex may start node), then the usual ones.
std::string cli_path_env(const std::string &cli) { return fs::path(cli).parent_path().string() + ":/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin"; }

std::vector<std::string> add_args(const AiApp &app, const McpEntry &entry)
{
    std::vector<std::string> args{"mcp", "add"};
    if (app.id == "claude_code")
        args.insert(args.end(), {"-s", "user"});
    args.insert(args.end(), {MCP_ENTRY_NAME, "--", entry.command});
    args.insert(args.end(), entry.args.begin(), entry.args.end());
    return args;
}

std::string last_lines(const std::string &output)
{
    const size_t keep = 400;
    return output.size() <= keep ? output : "..." + output.substr(output.size() - keep);
}
} // namespace

std::vector<AiApp> ai_apps(const std::string &home)
{
    return {{"claude_desktop", "Claude Desktop", home + "/Library/Application Support/Claude/claude_desktop_config.json", "", ConfigFormat::Json,
             {"/Applications/Claude.app", "~/Applications/Claude.app"}},
            {"claude_code", "Claude Code", home + "/.claude.json", "claude"},
            {"codex", "Codex", home + "/.codex/config.toml", "codex", ConfigFormat::Toml},
            {"antigravity", "Gemini (Antigravity)", home + "/.gemini/config/mcp_config.json", "agy"},
            // The desktop profile's patch layer; Harness hot-reloads it, so no restart and no CLI.
            {"deepseek_harness", "DeepSeek Harness", home + "/.dsh/profiles/desktop/cordis.patch.yml", "", ConfigFormat::CordisYaml,
             {"/Applications/DeepSeek Harness.app", "~/Applications/DeepSeek Harness.app"}}};
}

std::optional<McpEntry> read_app_entry(ConfigFormat format, const std::string &text)
{
    switch (format) {
    case ConfigFormat::Toml: return read_toml_entry(text);
    case ConfigFormat::CordisYaml: return read_cordis_yaml_entry(text);
    case ConfigFormat::Json: break;
    }
    return read_json_entry(text);
}

std::string find_cli(const std::string &name, const std::string &home)
{
    // The login shell can take seconds: remember what it said, a miss only for a minute so a CLI
    // installed meanwhile is found. UI thread only.
    using Clock = std::chrono::steady_clock;
    static std::map<std::string, std::pair<std::string, Clock::time_point>> s_found;
    const std::string key = home + "|" + name;
    if (const auto known = s_found.find(key); known != s_found.end()) {
        const std::string &path = known->second.first;
        if (path.empty() ? Clock::now() - known->second.second < std::chrono::minutes(1) : executable(path))
            return path;
    }
    std::string &found_path = s_found[key].first;
    s_found[key].second     = Clock::now();
    for (const char *folder : {"~/.local/bin", "/opt/homebrew/bin", "/usr/local/bin", "~/.claude/local", "~/.npm-global/bin", "~/.bun/bin", "~/.volta/bin"}) {
        const std::string candidate = expand(folder, home) + "/" + name;
        if (executable(candidate))
            return found_path = candidate;
    }
    // Installed somewhere else: ask the user's shell, interactive so nvm or fnm set up in .zshrc
    // count. Only the last line is used: .zshrc may print. `name` is one of ours.
    const ProcessResult found = run_program("/bin/zsh", {"-ilc", "command -v " + name}, "/usr/bin:/bin", std::chrono::seconds(5));
    std::string         path  = found.output.substr(0, found.output.find_last_not_of("\r\n") + 1);
    path                      = path.substr(path.find_last_of('\n') + 1);
    return found_path = found.exit_code == 0 && executable(path) ? path : "";
}

std::vector<AiApp> apps_pointing_elsewhere(const McpEntry &expected, const std::string &home)
{
    std::vector<AiApp> elsewhere;
    for (const AiApp &app : ai_apps(home))
        try {
            if (!fs::exists(app.config))
                continue;
            const std::string text  = read_file(app.config);
            const auto        found = read_app_entry(app.format, text);
            if (compare_entry(found, expected) == ConnectionState::Different)
                elsewhere.push_back(app);
        } catch (const std::exception &) {
            // An unreadable config is reported in Preferences, not at every start.
        }
    return elsewhere;
}

bool claude_desktop_running() { return run_program("/usr/bin/pgrep", {"-x", "Claude"}, "/usr/bin:/bin", std::chrono::seconds(5)).exit_code == 0; }

AppStatus app_status(const AiApp &app, const McpEntry &expected, const std::string &home)
{
    AppStatus status;
    try {
        if (app.cli.empty()) {
            bool installed = fs::exists(fs::path(app.config).parent_path());
            for (const std::string &bundle : app.bundles)
                installed = installed || fs::exists(expand(bundle, home));
            if (!installed)
                return status;
        } else if ((status.cli_path = find_cli(app.cli, home)).empty())
            return status;

        std::optional<McpEntry> found;
        if (fs::exists(app.config)) {
            const std::string text = read_file(app.config);
            found = read_app_entry(app.format, text);
        }
        status.state = compare_entry(found, expected);
        if (found)
            status.found_command = found->command;
    } catch (const std::exception &) {
        status.state = ConnectionState::Unreadable;
    }
    return status;
}

ConnectResult connect_app(const AiApp &app, const McpEntry &entry, const std::string &home)
{
    ConnectResult result;
    try {
        if (app.cli.empty()) {
            // Claude Desktop rewrites its file on quit; Harness re-reads its file before every write.
            if (app.id == "claude_desktop" && claude_desktop_running()) {
                result.message = "Claude Desktop is running and may overwrite the change. Quit Claude, then connect again.";
                return result;
            }
            const std::string text    = fs::exists(app.config) ? read_file(app.config) : "";
            // Throws on a config it cannot read: nothing written.
            const std::string updated = app.format == ConfigFormat::CordisYaml ? with_cordis_yaml_entry(text, entry) : with_json_entry(text, entry);
            result.backup             = back_up(app.config);
            fs::create_directories(fs::path(app.config).parent_path());
            write_file_atomically(app.config, updated);
            result.changed = true;
        } else {
            const std::string cli = find_cli(app.cli, home);
            if (cli.empty()) {
                result.message = "The " + app.cli + " command was not found.";
                return result;
            }
            // Read before changing anything: a config the app cannot have written is left alone.
            const std::string text     = fs::exists(app.config) ? read_file(app.config) : "";
            const auto        previous = read_app_entry(app.format, text);
            result.backup              = back_up(app.config);
            // Claude Code refuses to add over an existing entry; codex and agy update it in place.
            const bool replace = app.id == "claude_code" && previous;
            if (replace) {
                const ProcessResult removed = run_program(cli, {"mcp", "remove", "-s", "user", MCP_ENTRY_NAME}, cli_path_env(cli));
                if (removed.exit_code != 0) {
                    result.message = "claude mcp remove failed: " + last_lines(removed.output);
                    return result;
                }
            }
            const ProcessResult added = run_program(cli, add_args(app, entry), cli_path_env(cli));
            if (added.exit_code != 0) {
                result.message = app.cli + " mcp add " + (added.timed_out ? "did not finish in time." : "failed: " + last_lines(added.output));
                // Put the old entry back, through the CLI as well: the app may have rewritten its file since.
                if (replace) {
                    const bool restored = !previous->command.empty() && run_program(cli, add_args(app, *previous), cli_path_env(cli)).exit_code == 0;
                    result.changed      = !restored;
                    result.message += restored ? " The previous entry was put back." : " The previous entry could not be put back.";
                }
                return result;
            }
            result.changed = true;
        }
        if (app_status(app, entry, home).state != ConnectionState::Connected) {
            result.message = "The entry was written, but reading it back did not match.";
            return result;
        }
        result.ok = true;
    } catch (const std::exception &e) {
        result.message = e.what();
    }
    return result;
}

}}} // namespace Slic3r::GUI::AgentBridge
