#include "AgentBridgeYamlScalarCodec.hpp"

#include <cctype>
#include <cstdio>

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string trimmed(const std::string &s)
{
    const size_t first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// At s[i] == '\n' inside a multi-line scalar: drops the spaces written before the break and the next
// lines' indentation. One break folds to a space; each further (empty) line keeps a newline.
void fold_break(const std::string &s, size_t &i, std::string &out)
{
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r'))
        out.pop_back();
    int breaks = 0;
    for (; i < s.size() && (s[i] == '\n' || is_blank(s[i])); ++i)
        breaks += s[i] == '\n';
    out += breaks > 1 ? std::string(breaks - 1, '\n') : std::string(" ");
}

void append_utf8(unsigned long cp, std::string &out)
{
    if (cp < 0x80)
        out += char(cp);
    else if (cp < 0x800)
        out += {char(0xC0 | (cp >> 6)), char(0x80 | (cp & 0x3F))};
    else if (cp < 0x10000)
        out += {char(0xE0 | (cp >> 12)), char(0x80 | ((cp >> 6) & 0x3F)), char(0x80 | (cp & 0x3F))};
    else
        out += {char(0xF0 | (cp >> 18)), char(0x80 | ((cp >> 12) & 0x3F)), char(0x80 | ((cp >> 6) & 0x3F)), char(0x80 | (cp & 0x3F))};
}

// s[i] is the opening quote; on success i is just past the closing one.
std::optional<std::string> parse_single(const std::string &s, size_t &i)
{
    std::string out;
    for (++i; i < s.size();) {
        if (s[i] == '\'') {
            if (i + 1 < s.size() && s[i + 1] == '\'') {
                out += '\'';
                i += 2;
                continue;
            }
            ++i;
            return out;
        }
        if (s[i] == '\n')
            fold_break(s, i, out);
        else
            out += s[i++];
    }
    return std::nullopt; // no closing quote
}

std::optional<std::string> parse_double(const std::string &s, size_t &i)
{
    static const std::string from = "0abtnvfre \"/\\\t", to = std::string("\0\a\b\t\n\v\f\r\x1b \"/\\\t", 14);
    std::string out;
    for (++i; i < s.size();) {
        const char c = s[i];
        if (c == '"') {
            ++i;
            return out;
        }
        if (c == '\n') {
            fold_break(s, i, out);
            continue;
        }
        if (c != '\\') {
            out += c;
            ++i;
            continue;
        }
        if (++i >= s.size())
            return std::nullopt;
        const char e = s[i++];
        if (e == '\n') { // escaped line break: joins the lines without a space
            while (i < s.size() && is_blank(s[i]))
                ++i;
        } else if (const size_t at = from.find(e); at != std::string::npos)
            out += to[at];
        else if (e == 'x' || e == 'u' || e == 'U') {
            const size_t digits = e == 'x' ? 2 : e == 'u' ? 4 : 8;
            if (i + digits > s.size() || s.substr(i, digits).find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                return std::nullopt;
            append_utf8(std::stoul(s.substr(i, digits), nullptr, 16), out);
            i += digits;
        } else
            return std::nullopt;
    }
    return std::nullopt;
}

// Only a comment may follow a closing quote.
bool only_comment_left(const std::string &s, size_t i)
{
    const std::string rest = trimmed(s.substr(i));
    return rest.empty() || rest[0] == '#';
}
} // namespace

std::optional<std::string> decode_yaml_scalar(const std::string &raw)
{
    const std::string text = trimmed(raw);
    if (text.empty())
        return std::string();
    size_t i = 0;
    if (text[0] == '\'' || text[0] == '"') {
        auto value = text[0] == '\'' ? parse_single(text, i) : parse_double(text, i);
        return value && only_comment_left(text, i) ? value : std::nullopt;
    }
    if (std::string("[]{}#&*!|>%@`,").find(text[0]) != std::string::npos)
        return std::nullopt;
    // Plain: lines folded with spaces, empty lines kept as newlines, cut at a comment.
    std::string out;
    int         pending_breaks = 0;
    for (size_t start = 0; start <= text.size();) {
        size_t      end  = text.find('\n', start);
        std::string line = trimmed(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        start            = end == std::string::npos ? text.size() + 1 : end + 1;
        if (!line.empty() && line[0] == '#' && !out.empty())
            break;
        const size_t comment = line.find(" #");
        const bool   ends    = comment != std::string::npos;
        line                 = trimmed(line.substr(0, comment));
        if (line.empty())
            ++pending_breaks;
        else {
            if (!out.empty())
                out += pending_breaks > 0 ? std::string(pending_breaks, '\n') : std::string(" ");
            out += line;
            pending_breaks = 0;
        }
        if (ends)
            break;
    }
    return out;
}

std::optional<std::vector<std::string>> decode_yaml_flow_list(const std::string &raw)
{
    const std::string s = trimmed(raw);
    if (s.empty() || s[0] != '[')
        return std::nullopt;
    std::vector<std::string> items;
    auto skip_space = [&s](size_t &i) {
        while (i < s.size() && (s[i] == '\n' || is_blank(s[i])))
            ++i;
    };
    size_t i = 1;
    while (true) {
        skip_space(i);
        if (i >= s.size())
            return std::nullopt;
        if (s[i] == ']')
            break; // empty list, or a trailing comma
        std::optional<std::string> item;
        if (s[i] == '\'')
            item = parse_single(s, i);
        else if (s[i] == '"')
            item = parse_double(s, i);
        else if (s[i] == '[' || s[i] == '{')
            return std::nullopt;
        else {
            std::string plain;
            while (i < s.size() && s[i] != ',' && s[i] != ']') {
                if (s[i] == '\n')
                    fold_break(s, i, plain);
                else
                    plain += s[i++];
            }
            item = trimmed(plain);
        }
        if (!item)
            return std::nullopt;
        items.push_back(*item);
        skip_space(i);
        if (i < s.size() && s[i] == ',')
            ++i;
        else if (i >= s.size() || s[i] != ']')
            return std::nullopt;
    }
    return only_comment_left(s, i + 1) ? std::optional<std::vector<std::string>>(items) : std::nullopt;
}

std::string encode_yaml_scalar(const std::string &value)
{
    bool safe_chars = !value.empty(), control = false;
    for (unsigned char c : value) {
        safe_chars = safe_chars && (std::isalnum(c) || std::string("/._+-").find(char(c)) != std::string::npos);
        control    = control || c < 0x20 || c == 0x7F;
    }
    // Plain only where it cannot read as a number, bool or null: paths and --flags.
    if (safe_chars && (value[0] == '/' || (value.size() > 2 && value.rfind("--", 0) == 0)))
        return value;
    if (!control) {
        std::string quoted = "'";
        for (char c : value)
            quoted += c == '\'' ? std::string("''") : std::string(1, c);
        return quoted + "'";
    }
    std::string quoted = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            quoted += {'\\', char(c)};
        else if (c == '\n')
            quoted += "\\n";
        else if (c == '\t')
            quoted += "\\t";
        else if (c < 0x20 || c == 0x7F) {
            char hex[5];
            std::snprintf(hex, sizeof(hex), "\\x%02X", c);
            quoted += hex;
        } else
            quoted += char(c);
    }
    return quoted + "\"";
}

}}} // namespace Slic3r::GUI::AgentBridge
