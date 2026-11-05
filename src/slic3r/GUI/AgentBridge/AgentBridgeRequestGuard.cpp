#include "AgentBridgeRequestGuard.hpp"

#include <array>
#include <stdexcept>

#include <openssl/rand.h>

namespace Slic3r { namespace GUI { namespace AgentBridge {

std::string generate_token()
{
    std::array<unsigned char, 32> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
        throw std::runtime_error("system random generator failed");

    static const char hex[] = "0123456789abcdef";
    std::string       token;
    token.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        token.push_back(hex[b >> 4]);
        token.push_back(hex[b & 0x0F]);
    }
    return token;
}

bool tokens_equal(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

std::optional<BridgeResponse> check_request(const RequestHeaders &headers, const std::string &token, uint16_t port)
{
    // Browsers attach Origin to cross-site POSTs; legitimate adapter requests never carry one.
    if (headers.origin)
        return error_response(403, "origin_rejected", "Requests with an Origin header are not accepted");

    // An exact match defeats DNS rebinding, where a hostile name resolves to 127.0.0.1.
    const std::string port_suffix = ":" + std::to_string(port);
    if (!headers.host || (*headers.host != "127.0.0.1" + port_suffix && *headers.host != "localhost" + port_suffix))
        return error_response(403, "host_rejected", "Host must be the loopback address of this bridge");

    static const std::string bearer = "Bearer ";
    const bool has_bearer = headers.authorization && headers.authorization->compare(0, bearer.size(), bearer) == 0;
    if (!has_bearer || !tokens_equal(headers.authorization->substr(bearer.size()), token))
        return error_response(401, "bad_token", "Missing or wrong bridge token");

    return std::nullopt;
}

}}} // namespace Slic3r::GUI::AgentBridge
