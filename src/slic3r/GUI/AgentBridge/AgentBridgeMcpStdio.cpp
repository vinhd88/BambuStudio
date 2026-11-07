#include "AgentBridgeMcpStdio.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "AgentBridgeAgentDataFolder.hpp"
#include "AgentBridgeMcpProtocol.hpp"
#include "AgentBridgeStudioClient.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
const char *const DATA_DIR_ENV = "BAMBU_AGENT_DATA_DIR";
} // namespace

bool is_mcp_stdio_run(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], MCP_STDIO_FLAG) == 0)
            return true;
    return false;
}

std::string mcp_stdio_data_dir(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--datadir") == 0)
            return argv[i + 1];
    const char *env = std::getenv(DATA_DIR_ENV);
    if (env != nullptr && *env != '\0')
        return env;
    // The folder Studio uses when started without --datadir.
    const std::string official = official_data_dir_from_home();
    return official.empty() ? "" : agent_data_dir_for(official);
}

int run_mcp_stdio(const std::string &data_dir, std::istream &in, std::ostream &out)
{
    const ToolCaller call = [&data_dir](const std::string &tool, const nlohmann::json &args) { return call_running_studio(data_dir, tool, args); };
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos)
            continue;
        // One reply per line, flushed at once: the client waits for it.
        if (const auto reply = handle_mcp_message(line, call))
            out << *reply << '\n' << std::flush;
    }
    return 0;
}

int run_mcp_stdio(int argc, char **argv)
{
    const std::string data_dir = mcp_stdio_data_dir(argc, argv);
    if (data_dir.empty())
        std::cerr << "bambu-studio --mcp-stdio: no --datadir, " << DATA_DIR_ENV << " or HOME; tool calls will report not_configured" << std::endl;
    return run_mcp_stdio(data_dir, std::cin, std::cout);
}

}}} // namespace Slic3r::GUI::AgentBridge
