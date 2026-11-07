#include "AgentBridgeAiAppConfigsCordisYaml.hpp"

#include <regex>
#include <stdexcept>
#include <vector>

#include "AgentBridgeYamlScalarCodec.hpp"

namespace Slic3r { namespace GUI { namespace AgentBridge {

namespace {
using Lines = std::vector<std::string>;

const char *const MARKER = "# bambu-studio:"; // the comment line Studio writes above its insert row

size_t indent_of(const std::string &line) { return line.find_first_not_of(' ') == std::string::npos ? line.size() : line.find_first_not_of(' '); }
bool   is_blank(const std::string &line) { return line.find_first_not_of(" \t\r") == std::string::npos; }
bool   is_comment(const std::string &line) { return !is_blank(line) && line[indent_of(line)] == '#'; }
bool   is_content(const std::string &line) { return !is_blank(line) && !is_comment(line); }
// A sequence entry starting at column `at`: "-" then a space or the line end.
bool   is_item_start(const std::string &line, size_t at) { return indent_of(line) == at && line.size() > at && line[at] == '-' && (line.size() == at + 1 || line[at + 1] == ' ' || line[at + 1] == '\r'); }

// One sequence entry: lines [begin, end), keys at `key_indent`. Blank and comment lines after its
// last content line are not part of it, so removing it keeps the comments that follow.
struct Row
{
    size_t begin = 0, end = 0, key_indent = 0;
};

// The entries at column `at` among lines [begin, end). Content at or left of `at` that is not an
// entry (a sibling key of the list) ends the current entry and sets `*other`.
std::vector<Row> rows_at(const Lines &lines, size_t begin, size_t end, size_t at, bool *other = nullptr)
{
    std::vector<Row> rows;
    bool             open = false;
    for (size_t i = begin; i < end; ++i) {
        if (is_item_start(lines[i], at)) {
            size_t key = lines[i].find_first_not_of(" \r", at + 1);
            if (key == std::string::npos) { // a bare "-": its keys start on the next content line
                size_t next = i + 1;
                while (next < end && !is_content(lines[next]))
                    ++next;
                key = next < end ? indent_of(lines[next]) : at + 2;
            }
            rows.push_back({i, i + 1, key});
            open = true;
        } else if (is_content(lines[i])) {
            if (open && indent_of(lines[i]) > at)
                rows.back().end = i + 1;
            else {
                open = false;
                if (other)
                    *other = true;
            }
        }
    }
    return rows;
}

bool has_our_id(const Lines &lines, const Row &row)
{
    static const std::regex id_key(std::string(R"(id:\s*(['"]?))") + CORDIS_ROW_ID + R"(\1([ \t]+#.*)?[ \t]*\r?)");
    for (size_t i = row.begin; i < row.end; ++i)
        if ((i == row.begin || indent_of(lines[i]) == row.key_indent) && std::regex_match(lines[i].substr(std::min(row.key_indent, lines[i].size())), id_key))
            return true;
    return false;
}

struct Owned
{
    Row  row;
    Row  insert;            // the top-level insert it sits in, or the row itself when top-level
    bool alone     = true;  // the only row of its insert (or top-level): remove the whole insert
    bool readable  = true;
    bool removable = true;  // false: removing it would leave a broken insert (keys beside the list)
};

struct Patch
{
    Lines              lines;
    size_t             placeholder = std::string::npos; // the `[]` line of an empty patch
    std::vector<Owned> owned;                           // in file order
};

Patch parse(const std::string &text)
{
    Patch patch;
    for (size_t start = 0; start < text.size();) {
        const size_t end = text.find('\n', start);
        patch.lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        start = end == std::string::npos ? text.size() : end + 1;
    }
    const Lines &lines = patch.lines;
    bool         items = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!is_content(lines[i]) || indent_of(lines[i]) > 0) {
            if (is_content(lines[i]) && !items)
                throw std::runtime_error("the patch file is not a YAML list of rows");
            continue;
        }
        const std::string head = lines[i].substr(0, lines[i].find(" #"));
        if (is_item_start(lines[i], 0) && patch.placeholder == std::string::npos)
            items = true;
        else if (head.find_first_not_of(" \t\r", 2) == std::string::npos && head.rfind("[]", 0) == 0 && !items && patch.placeholder == std::string::npos)
            patch.placeholder = i;
        else
            throw std::runtime_error("the patch file is not a YAML block list Studio can edit");
    }
    static const std::regex insert_head(R"(-\s+insert:([ \t]+#.*)?[ \t]*\r?)");
    for (const Row &top : rows_at(lines, 0, lines.size(), 0)) {
        const bool insert = top.key_indent < lines[top.begin].size() && lines[top.begin].compare(top.key_indent, 7, "insert:") == 0;
        if (!insert) {
            if (has_our_id(lines, top))
                patch.owned.push_back({top, top});
            continue;
        }
        // The insert's own list: its rows at the column of its first content line.
        size_t first = top.begin + 1;
        while (first < top.end && !is_content(lines[first]))
            ++first;
        const bool block = std::regex_match(lines[top.begin], insert_head) && first < top.end && is_item_start(lines[first], indent_of(lines[first]));
        if (!block) { // `- insert: [...]` or another shape: ours only if our id is in it
            for (size_t i = top.begin; i < top.end; ++i)
                if (lines[i].find(CORDIS_ROW_ID) != std::string::npos) {
                    patch.owned.push_back({top, top, true, false});
                    break;
                }
            continue;
        }
        bool                   siblings = false;
        const std::vector<Row> rows     = rows_at(lines, first, top.end, indent_of(lines[first]), &siblings);
        size_t                 ours     = 0;
        for (const Row &row : rows)
            ours += has_our_id(lines, row);
        // All rows ours: the whole insert goes, unless keys beside its list would be left without one.
        const bool all_ours = ours == rows.size();
        for (const Row &row : rows)
            if (has_our_id(lines, row))
                patch.owned.push_back({row, top, all_ours, true, !(all_ours && siblings)});
    }
    return patch;
}

// The value lines of the key on line `at` (indent `indent`): its rest, then deeper lines, then for a
// list also entries at the key's own column.
std::vector<std::string> value_lines(const Lines &lines, size_t at, size_t end, size_t indent, bool list)
{
    std::vector<std::string> value{lines[at].substr(lines[at].find(':') + 1)};
    for (size_t i = at + 1; i < end && (is_blank(lines[i]) || indent_of(lines[i]) > indent || (list && is_item_start(lines[i], indent))); ++i)
        value.push_back(lines[i]);
    return value;
}

std::string joined(const std::vector<std::string> &parts, size_t from = 0)
{
    std::string text;
    for (size_t i = from; i < parts.size(); ++i)
        text += (i > from ? "\n" : "") + parts[i];
    return text;
}

McpEntry read_row(const Lines &lines, const Row &row)
{
    static const std::regex command_key(R"(\s*command:[^\n]*)"), args_key(R"(\s*args:[^\n]*)"); // `.` stops at a CRLF '\r' 
    std::optional<std::string>              command;
    std::optional<std::vector<std::string>> args;
    for (size_t i = row.begin + 1; i < row.end; ++i) {
        const size_t indent = indent_of(lines[i]);
        if (!command && indent > row.key_indent && std::regex_match(lines[i], command_key))
            command = decode_yaml_scalar(joined(value_lines(lines, i, row.end, indent, false)));
        else if (!args && indent > row.key_indent && std::regex_match(lines[i], args_key)) {
            const std::vector<std::string> value = value_lines(lines, i, row.end, indent, true);
            const std::string              rest  = value[0].substr(0, value[0].find(" #"));
            const std::string flat  = joined(value);
            const size_t      start = flat.find_first_not_of(" \t\r\n");
            if (rest.find_first_not_of(" \t\r") != std::string::npos || (start != std::string::npos && flat[start] == '[')) {
                args = decode_yaml_flow_list(flat);
                continue;
            }
            // A block list: entries at the column of the first one.
            std::vector<std::string> items;
            size_t                   dash = std::string::npos;
            bool                     ok   = true;
            for (size_t j = 1; j < value.size() && ok; ++j) {
                if (!is_content(value[j]))
                    continue;
                if (dash == std::string::npos)
                    dash = indent_of(value[j]);
                if (is_item_start(value[j], dash))
                    items.push_back(value[j].substr(std::min(dash + 2, value[j].size())));
                else if (indent_of(value[j]) > dash && !items.empty())
                    items.back() += "\n" + value[j];
                else
                    ok = false;
            }
            std::vector<std::string> decoded;
            for (const std::string &item : items)
                if (auto one = decode_yaml_scalar(item))
                    decoded.push_back(*one);
                else
                    ok = false;
            if (ok)
                args = decoded;
        }
    }
    if (!command || !args)
        return {};
    return {*command, *args};
}

Lines entry_lines(const McpEntry &entry)
{
    Lines lines{std::string(MARKER) + " written by Bambu Studio (Preferences > AI Agents > Connect). Connect replaces this entry.",
                "- insert:",
                std::string("    - id: ") + CORDIS_ROW_ID,
                "      name: '@deepseek-ai/dsh-mcp-client'",
                "      config:",
                std::string("        serverName: ") + MCP_ENTRY_NAME,
                "        transport: stdio",
                "        command: " + encode_yaml_scalar(entry.command),
                entry.args.empty() ? "        args: []" : "        args:"};
    for (const std::string &arg : entry.args)
        lines.push_back("          - " + encode_yaml_scalar(arg));
    return lines;
}
} // namespace

std::optional<McpEntry> read_cordis_yaml_entry(const std::string &text)
{
    const Patch patch = parse(text);
    if (patch.owned.empty())
        return std::nullopt;
    const Owned &last = patch.owned.back();
    return last.readable ? read_row(patch.lines, last.row) : McpEntry{};
}

std::string with_cordis_yaml_entry(const std::string &text, const McpEntry &entry)
{
    const Patch       patch = parse(text);
    std::vector<bool> drop(patch.lines.size(), false);
    for (const Owned &owned : patch.owned) {
        if (!owned.readable || !owned.removable)
            throw std::runtime_error("its " + std::string(CORDIS_ROW_ID) + " row is written in a form Studio cannot edit");
        const Row &gone = owned.alone ? owned.insert : owned.row;
        std::fill(drop.begin() + gone.begin, drop.begin() + gone.end, true);
        if (owned.alone && gone.begin > 0 && patch.lines[gone.begin - 1].rfind(MARKER, 0) == 0)
            drop[gone.begin - 1] = true;
    }
    const bool  crlf  = !patch.lines.empty() && !patch.lines[0].empty() && patch.lines[0].back() == '\r';
    const Lines ours  = [&] {
        Lines lines = entry_lines(entry);
        for (std::string &line : lines)
            line += crlf ? "\r" : "";
        return lines;
    }();
    Lines kept;
    for (size_t i = 0; i < patch.lines.size(); ++i)
        if (!drop[i] && i != patch.placeholder)
            kept.push_back(patch.lines[i]);
        else if (i == patch.placeholder)
            kept.insert(kept.end(), ours.begin(), ours.end());
    if (patch.placeholder == std::string::npos) {
        // One blank line before our row, however many our removed rows left behind.
        while (!kept.empty() && is_blank(kept.back()))
            kept.pop_back();
        if (!kept.empty())
            kept.push_back(crlf ? "\r" : "");
        kept.insert(kept.end(), ours.begin(), ours.end());
    }
    return joined(kept) + "\n";
}

std::string cordis_yaml_snippet(const McpEntry &entry) { return joined(entry_lines(entry)); }

}}} // namespace Slic3r::GUI::AgentBridge
