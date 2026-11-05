#!/usr/bin/env node
// Regenerates src/slic3r/GUI/AgentBridge/AgentBridgeSettingsTabMap.cpp from Tab.cpp.
// Run after each upstream update, since Studio's tab layout changes between releases:
//   node tools/agent-bridge-tab-map/generate-settings-tab-map.mjs
//
// Walks the functions that build each tab in source order, tracking the current page and option
// group, and records where each setting first appears. Only keys defined in PrintConfig.cpp count.
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const tabSource = readFileSync(resolve(repo, 'src/slic3r/GUI/Tab.cpp'), 'utf8');
const configSource = readFileSync(resolve(repo, 'src/libslic3r/PrintConfig.cpp'), 'utf8');
const outputPath = resolve(repo, 'src/slic3r/GUI/AgentBridge/AgentBridgeSettingsTabMap.cpp');

// Order matters: the first place a key appears wins, and the main build functions come first.
const SECTIONS = [
  { tab: 'process', fn: 'void TabPrint::build()' },
  { tab: 'filament', fn: 'void TabFilament::build()' },
  { tab: 'filament', fn: 'void TabFilament::add_filament_overrides_page()' },
  { tab: 'printer', fn: 'void TabPrinter::build_fff()' },
  { tab: 'printer', fn: 'PageShp TabPrinter::build_kinematics_page()' },
  { tab: 'printer', fn: 'void TabPrinter::build_unregular_pages(bool from_initial_build/* = false*/)' },
  { tab: 'process', fn: 'void TabPrintPlate::build()' },
];

// Calls whose first string argument is a setting key.
const KEY_CALL = /\b(?:append_single_option_line|get_option|create_single_option_line|append_option_line|append_nozzle_scaled_option|append_multi_nozzle_auto_line|create_line_with_widget)\s*\(\s*(?:optgroup(?:\.get\(\))?\s*,\s*)?"([a-z0-9_]+)"/g;
// Calls that build the key at runtime from an axis: append_option_line(optgroup, "machine_max_jerk_" + axis)
const AXIS_CALL = /append_option_line\(\s*optgroup\s*,\s*"([a-z0-9_]+_)"\s*\+\s*axis/;
const AXES = ['x', 'y', 'z', 'e'];
// Key lists: for (const std::string opt_key : { "a", ... or const std::vector<std::string> speed_axes{ "a", ...
const LIST_START = /for\s*\(.*:\s*\{|std::vector<std::string>\s+\w+\s*\{/;
const KEY_LITERAL = /"([a-z0-9_]+)"/g;

function definedKeys() {
  const keys = new Set();
  for (const match of configSource.matchAll(/\badd(?:_nullable)?\(\s*"([a-z0-9_]+)"/g)) keys.add(match[1]);
  // PrintConfig.cpp also defines keys in loops, over axes and over filament overrides of printer keys.
  for (const match of configSource.matchAll(/\badd\(\s*"(machine_[a-z_]+_)"\s*\+\s*axis/g))
    for (const axis of AXES) keys.add(match[1] + axis);
  for (const match of tabSource.matchAll(/"filament_([a-z0-9_]+)"/g))
    if (keys.has(match[1])) keys.add(`filament_${match[1]}`);
  return keys;
}

function functionBody(signature) {
  const start = tabSource.indexOf(`\n${signature}`);
  if (start < 0) throw new Error(`Tab.cpp no longer defines: ${signature}`);
  const open = tabSource.indexOf('{', start + signature.length + 1);
  let depth = 0;
  for (let i = open; i < tabSource.length; i++) {
    if (tabSource[i] === '{') depth++;
    // Block comments hold old key literals. Stripped here, as signatures above contain comments too.
    if (tabSource[i] === '}' && --depth === 0) return tabSource.slice(open, i).replace(/\/\*[\s\S]*?\*\//g, '');
  }
  throw new Error(`Unbalanced braces in: ${signature}`);
}

function stripLineComment(line) {
  const at = line.indexOf('//');
  return at >= 0 ? line.slice(0, at) : line;
}

// Blanks out code disabled by `#if 0` or the `#else` of `#if 1`. Any other condition, such as a
// platform check, is treated as live on both branches: the UI lists those settings on some platform.
function withoutDisabledCode(lines) {
  const stack = []; // true while the current branch is disabled
  const fixed = []; // whether each open #if has a constant condition
  return lines.map((line) => {
    const directive = line.trim();
    if (/^#\s*if/.test(directive)) {
      const constant = /^#\s*if\s+[01]\b/.test(directive);
      fixed.push(constant);
      stack.push(/^#\s*if\s+0\b/.test(directive));
    } else if (/^#\s*el/.test(directive)) {
      if (fixed[fixed.length - 1]) stack[stack.length - 1] = !stack[stack.length - 1];
    } else if (/^#\s*endif/.test(directive)) {
      stack.pop();
      fixed.pop();
    } else if (!stack.includes(true)) return line;
    return '';
  });
}

function collectPlaces(known) {
  const places = new Map();
  for (const { tab, fn } of SECTIONS) {
    let page = '';
    let group = '';
    let inList = false;
    const record = (key) => {
      if (known.has(key) && !places.has(key)) places.set(key, { tab, page, group });
    };
    for (const rawLine of withoutDisabledCode(functionBody(fn).split('\n'))) {
      const line = stripLineComment(rawLine);
      const pageMatch = line.match(/add_options_page\(\s*L\("([^"]*)"\)/);
      if (pageMatch) { page = pageMatch[1]; group = ''; }
      // Extruder pages get a computed name, e.g. "Extruder 1". Only one extruder page matters here.
      if (/add_options_page\(\s*page_name/.test(line)) { page = 'Extruder'; group = ''; }
      const groupMatch = line.match(/new_optgroup\(\s*(?:L\("([^"]*)"\)|"([^"]*)")/);
      if (groupMatch) group = groupMatch[1] ?? groupMatch[2] ?? '';
      // Keys passed one per line to a local helper, such as build_aux_fan_group(L("..."),\n "key",
      const helperGroup = line.match(/=\s*\w+\(\s*L\("([^"]+)"\),\s*$/);
      if (helperGroup) group = helperGroup[1];
      const loneKey = line.match(/^\s*"([a-z0-9_]+)"\s*[,)]/);
      if (loneKey && !inList) record(loneKey[1]);

      if (LIST_START.test(line)) inList = true;
      if (inList) {
        for (const match of line.matchAll(KEY_LITERAL)) record(match[1]);
        if (line.includes('}')) inList = false;
      }
      for (const match of line.matchAll(KEY_CALL)) record(match[1]);
      const axisMatch = line.match(AXIS_CALL);
      if (axisMatch) for (const axis of AXES) record(axisMatch[1] + axis);
    }
  }
  return places;
}

const known = definedKeys();
const places = collectPlaces(known);
if (places.size < 450) throw new Error(`Only ${places.size} keys found. Tab.cpp layout probably changed; update this script.`);

const quote = (text) => JSON.stringify(text);
const rows = [...places.entries()]
  .sort(([a], [b]) => a.localeCompare(b))
  .map(([key, { tab, page, group }]) => `    {${quote(key)}, {${quote(tab)}, ${quote(page)}, ${quote(group)}}},`);

writeFileSync(outputPath, `// Generated by tools/agent-bridge-tab-map/generate-settings-tab-map.mjs from Tab.cpp. Do not edit.
#include "AgentBridgeSettingsTabMap.hpp"

#include <unordered_map>

namespace Slic3r { namespace GUI { namespace AgentBridge {

const SettingPlace *find_setting_place(const std::string &key)
{
    static const std::unordered_map<std::string, SettingPlace> places = {
${rows.join('\n')}
    };
    const auto it = places.find(key);
    return it == places.end() ? nullptr : &it->second;
}

}}} // namespace Slic3r::GUI::AgentBridge
`);

const counts = {};
for (const { tab } of places.values()) counts[tab] = (counts[tab] ?? 0) + 1;
console.log(`Wrote ${places.size} of ${known.size} defined keys:`, counts);
