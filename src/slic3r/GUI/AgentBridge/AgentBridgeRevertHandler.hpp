#ifndef slic3r_AgentBridgeRevertHandler_hpp_
#define slic3r_AgentBridgeRevertHandler_hpp_

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Tool: undoes the newest agent change not yet undone, restoring its before-values through the same
// Studio routes the change took. Refuses, changing nothing, if anything it touched has been changed
// since, so it never overwrites the user's own edits. UI thread.
nlohmann::json revert_change(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
