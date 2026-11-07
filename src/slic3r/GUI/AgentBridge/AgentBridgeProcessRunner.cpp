#include "AgentBridgeProcessRunner.hpp"

#include <algorithm>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
// The current environment with PATH replaced: apps started from Finder have only /usr/bin:/bin.
std::vector<std::string> environment_with_path(const std::string &path)
{
    std::vector<std::string> env;
    for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry)
        if (std::strncmp(*entry, "PATH=", 5) != 0)
            env.emplace_back(*entry);
    env.push_back("PATH=" + path);
    return env;
}

std::vector<char *> c_strings(std::vector<std::string> &strings)
{
    std::vector<char *> pointers;
    for (std::string &s : strings)
        pointers.push_back(s.data());
    pointers.push_back(nullptr);
    return pointers;
}
} // namespace

ProcessResult run_program(const std::string &program, const std::vector<std::string> &args, const std::string &path, std::chrono::milliseconds timeout)
{
    ProcessResult result;
    int           out[2];
    if (::pipe(out) != 0) {
        result.output = "cannot create a pipe";
        return result;
    }
    // Not inherited by tools other threads may start meanwhile: they would hold the pipe open.
    ::fcntl(out[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(out[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    posix_spawn_file_actions_addclose(&actions, out[1]);

    std::vector<std::string> argv_strings{program};
    argv_strings.insert(argv_strings.end(), args.begin(), args.end());
    std::vector<std::string> env_strings = environment_with_path(path);
    std::vector<char *>      argv = c_strings(argv_strings), envp = c_strings(env_strings);

    // The tool gets stdin, stdout and stderr only, not Studio's files and sockets (the bridge's port).
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    // Its own process group, so a timeout also ends what it started (node, for example).
    short flags = POSIX_SPAWN_SETPGROUP;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    posix_spawnattr_setflags(&attributes, flags);
    posix_spawnattr_setpgroup(&attributes, 0);
    pid_t     pid     = 0;
    const int spawned = ::posix_spawn(&pid, program.c_str(), &actions, &attributes, argv.data(), envp.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    ::close(out[1]);
    if (spawned != 0) {
        ::close(out[0]);
        result.output = "cannot start " + program + ": " + std::strerror(spawned);
        return result;
    }

    // One deadline for reading and for exiting: a tool that closes its output but keeps running
    // must not hold Studio either.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto left_ms  = [&deadline] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    };
    char buffer[4096];
    for (bool open = true; open && left_ms() > 0;) {
        pollfd fd{out[0], POLLIN, 0};
        if (::poll(&fd, 1, static_cast<int>(std::min<long long>(left_ms(), 1000))) <= 0)
            continue;
        const ssize_t n = ::read(out[0], buffer, sizeof(buffer));
        if (n > 0)
            result.output.append(buffer, static_cast<size_t>(n));
        else
            open = false;
    }
    ::close(out[0]);

    int status = 0;
    for (;;) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            if (WIFEXITED(status))
                result.exit_code = WEXITSTATUS(status);
            break;
        }
        if (done < 0)
            break;
        if (left_ms() <= 0) {
            result.timed_out = true;
            ::kill(-pid, SIGKILL);
            ::waitpid(pid, &status, 0); // returns at once after SIGKILL
            break;
        }
        ::usleep(20 * 1000);
    }
    return result;
}

}}} // namespace Slic3r::GUI::AgentBridge
