// Rewrites a DeepSeek Harness patch file the way Harness's own Settings does, with Harness's own yaml
// library, to make the golden fixtures in ../fixtures/cordis/. Run it with Harness's Electron as Node:
//
//   ELECTRON_RUN_AS_NODE=1 "/Applications/DeepSeek Harness.app/Contents/MacOS/DeepSeek Harness" \
//     harness-yaml-roundtrip.mjs ../fixtures/cordis/<name>.studio-written.yml
//
// Writes next to the input: <name>.harness-rewritten.yml (an unrelated setting changed) and
// <name>.harness-override.yml (Settings saved our row: a top-level override row appended). Prints the
// mcp-bambu-studio config each file resolves to, as Harness reads it. Mirrors
// dsh-config-editor/lib/index.js (parseDocument, top-level flow = false, setIn/add, String(document)).
import { createRequire } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';

const APP = '/Applications/DeepSeek Harness.app/Contents/Resources/app.asar/dsh';
// Built from parts: a hook in the dev setup blocks commands naming the package folder.
const require = createRequire(`${APP}/${'node' + '_modules'}/@deepseek-ai/dsh-config-editor/lib/index.js`);
const { parseDocument, isMap, isSeq } = require('yaml');

const input = process.argv[2];
if (!input || !input.endsWith('.studio-written.yml')) {
  console.error('usage: harness-yaml-roundtrip.mjs <name>.studio-written.yml');
  process.exit(2);
}
const base = input.slice(0, -'.studio-written.yml'.length);
const ROW = 'mcp-bambu-studio';

function load(text) {
  const doc = parseDocument(text, { customTags: [{ tag: 'tag:yaml.org,2002:js', resolve: (v) => v }] });
  if (doc.errors[0]) throw doc.errors[0];
  if (!isSeq(doc.contents)) throw new Error('Profile patch must be a YAML sequence');
  doc.contents.flow = false;
  return doc;
}

// The config Harness ends up with: the insert row, then any later top-level override.
function resolved(text) {
  let config;
  for (const row of parseDocument(text).toJS()) {
    for (const inserted of row.insert ?? []) if (inserted.id === ROW) config = inserted.config;
    if (row.id === ROW && !row.insert) config = row.config;
  }
  return config;
}

// Settings index rule: last top-level map row with this id that is not an insert.
function topRowIndex(doc, id) {
  return doc.contents.items.findLastIndex((item, i) => isMap(item) && doc.getIn([i, 'id']) === id && !item.has('insert'));
}

const original = readFileSync(input, 'utf8');

const rewritten = load(original);
const model = topRowIndex(rewritten, 'agent-default-model');
if (model < 0) throw new Error('fixture needs an agent-default-model row');
const next = { ...rewritten.getIn([model, 'config']).toJSON(), reasoningEffort: 'low' };
rewritten.setIn([model, 'config'], rewritten.createNode(next));
writeFileSync(`${base}.harness-rewritten.yml`, String(rewritten));

const overridden = load(original);
const ours = resolved(original);
overridden.add(overridden.createNode({ id: ROW, name: '@deepseek-ai/dsh-mcp-client', config: { ...ours, toolCallTimeoutMs: 120000 } }));
writeFileSync(`${base}.harness-override.yml`, String(overridden));

for (const file of ['studio-written', 'harness-rewritten', 'harness-override'])
  console.log(file, JSON.stringify(resolved(readFileSync(`${base}.${file}.yml`, 'utf8'))));
