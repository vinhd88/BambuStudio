#ifndef slic3r_AgentBridgeYamlScalarCodec_hpp_
#define slic3r_AgentBridgeYamlScalarCodec_hpp_

#include <optional>
#include <string>
#include <vector>

// The few YAML scalar forms an AI app may write for a command and its arguments: plain, single-quoted
// and double-quoted, possibly folded over several lines (the yaml library folds at 80 columns), and a
// flow list of them. Enough for reading back one config row; not a YAML parser. GUI-free.
namespace Slic3r { namespace GUI { namespace AgentBridge {

// A block-context value: `raw` is the text after `key:` or `- `, then its continuation lines, joined
// with '\n'. Empty if it is another form (block scalar, alias, tag, flow collection) or malformed.
std::optional<std::string> decode_yaml_scalar(const std::string &raw);

// A flow sequence of scalars, `[a, 'b c', "d"]`, possibly over several lines. Empty if it is not one.
std::optional<std::vector<std::string>> decode_yaml_flow_list(const std::string &raw);

// `value` as one YAML scalar: plain for paths and --flags (so a rewrite by the yaml library keeps it
// as is), else single-quoted, or double-quoted with escapes when it holds control characters.
std::string encode_yaml_scalar(const std::string &value);

}}} // namespace Slic3r::GUI::AgentBridge

#endif
