#ifndef slic3r_AgentBridgeMutationGate_hpp_
#define slic3r_AgentBridgeMutationGate_hpp_

#include <optional>
#include <string>

#include "AgentBridgeTypes.hpp"

// Decides when a tool may change the project, and what a client hears while a change is under way.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// UI thread. Throws BridgeError if Studio cannot safely take a change now. Nothing has happened
// to the project when it throws.
void require_can_mutate();

// Marks a change or slice start as running for as long as it lives, so that a client whose call
// outlasted the UI timeout gets told which one to look for.
class MutationScope
{
public:
    // id_field is what the client looks for: "change_id" or "job_id".
    MutationScope(const std::string &id_field, const std::string &id);
    ~MutationScope();
    MutationScope(const MutationScope &) = delete;
    MutationScope &operator=(const MutationScope &) = delete;
};

// Id of the running change or job, or empty if none. Thread safe.
std::string running_mutation_id();

// io thread. The answer to a mutation whose work is still on the UI thread when its time is up,
// typically because the work opened a dialog: the request has been accepted and will finish later.
// nullopt when no change or job is marked running, which means the work is about to answer itself.
std::optional<BridgeResponse> overrun_response();

}}} // namespace Slic3r::GUI::AgentBridge

#endif
