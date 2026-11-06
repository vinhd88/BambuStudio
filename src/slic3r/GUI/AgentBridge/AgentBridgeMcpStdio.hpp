#ifndef slic3r_AgentBridgeMcpStdio_hpp_
#define slic3r_AgentBridgeMcpStdio_hpp_

#include <iosfwd>
#include <string>

// `BambuStudio --mcp-stdio`: the MCP server AI apps launch. Reads JSON-RPC from stdin, one message
// per line, and forwards tool calls to the running Studio. Starts no GUI. GUI-free.
namespace Slic3r { namespace GUI { namespace AgentBridge {

constexpr const char *MCP_STDIO_FLAG = "--mcp-stdio";

// True if `--mcp-stdio` is among the arguments.
bool is_mcp_stdio_run(int argc, char **argv);

// The data folder from `--datadir <dir>`, else env BAMBU_AGENT_DATA_DIR, else empty.
std::string mcp_stdio_data_dir(int argc, char **argv);

// Serves until `in` ends. Only JSON-RPC goes to `out`. Returns the process exit code.
int run_mcp_stdio(const std::string &data_dir, std::istream &in, std::ostream &out);

// For main(): the above on stdin/stdout.
int run_mcp_stdio(int argc, char **argv);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
