#ifndef slic3r_AgentBridgeDiscoveryFile_hpp_
#define slic3r_AgentBridgeDiscoveryFile_hpp_

#include <cstdint>
#include <string>

namespace Slic3r { namespace GUI { namespace AgentBridge {

// Publishes port, token and pid for the adapter, in a file only the current user can read.
void write_discovery_file(const std::string &path, uint16_t port, const std::string &token);

// Removes the file, unless it now holds another instance's token.
void remove_discovery_file(const std::string &path, const std::string &token);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
