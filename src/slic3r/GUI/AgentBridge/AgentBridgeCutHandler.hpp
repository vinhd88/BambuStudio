#ifndef slic3r_AgentBridgeCutHandler_hpp_
#define slic3r_AgentBridgeCutHandler_hpp_

#include <nlohmann/json.hpp>

// Tool cut_object: the Cut tool's planar cut with a horizontal plane at a height, without
// connectors, using libslic3r's Cut as the tool does. UI thread; the cut runs synchronously.
namespace Slic3r { namespace GUI { namespace AgentBridge {

nlohmann::json cut_object(const nlohmann::json &args);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
