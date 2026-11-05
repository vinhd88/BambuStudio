#ifndef slic3r_AgentBridgeRequestGuard_hpp_
#define slic3r_AgentBridgeRequestGuard_hpp_

#include <cstdint>
#include <optional>
#include <string>

#include "AgentBridgeTypes.hpp"

// Security checks for bridge requests. Any web page can reach localhost, so a request must prove
// it came from a local process that can read the owner-only discovery file, and must not look like
// it came from a browser or a DNS-rebound host name.
namespace Slic3r { namespace GUI { namespace AgentBridge {

struct RequestHeaders
{
    std::optional<std::string> host;
    std::optional<std::string> origin;
    std::optional<std::string> authorization;
};

// 32 random bytes as lowercase hex. Throws if the system generator fails: never fall back to a weak token.
std::string generate_token();

// Compares without an early exit, so response timing does not reveal how much of the token matched.
bool tokens_equal(const std::string &a, const std::string &b);

// Returns the rejection, or nullopt when the request may proceed.
std::optional<BridgeResponse> check_request(const RequestHeaders &headers, const std::string &token, uint16_t port);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
