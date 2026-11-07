#ifndef slic3r_AgentBridgeProcessRunner_hpp_
#define slic3r_AgentBridgeProcessRunner_hpp_

#include <chrono>
#include <string>
#include <vector>

// Runs a command-line tool (claude, codex, agy) without a shell and collects its output. GUI-free. POSIX.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct ProcessResult
{
    int         exit_code = -1; // -1 if it could not start, was killed, or timed out
    bool        timed_out = false;
    std::string output;         // stdout and stderr together
};

// Runs `program` with `args`, stdin empty, PATH set to `path`. Kills it after `timeout`. Never throws.
ProcessResult run_program(const std::string &program, const std::vector<std::string> &args, const std::string &path,
                          std::chrono::milliseconds timeout = std::chrono::seconds(30));

}}} // namespace Slic3r::GUI::AgentBridge

#endif
