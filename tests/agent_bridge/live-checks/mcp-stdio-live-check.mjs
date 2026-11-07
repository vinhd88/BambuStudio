// Live check for the Node-free MCP server: `BambuStudio --mcp-stdio`, driven by the MCP SDK client
// as an AI app would, against a running custom Studio. Read tools only, plus one rejected change;
// nothing in the project changes. Each result must equal a direct bridge call.
//
//   BAMBU_AGENT_DATA_DIR=<dev-data> node mcp-stdio-live-check.mjs [path/to/BambuStudio]
import assert from 'node:assert/strict';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

import { callBridgeTool } from './bridge-client.js';

const dataDir = process.env.BAMBU_AGENT_DATA_DIR;
assert.ok(dataDir, 'set BAMBU_AGENT_DATA_DIR to the --datadir Studio runs with');
const repo = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const binary = process.argv[2] ?? join(repo, 'build', 'arm64', 'src', 'BambuStudio.app', 'Contents', 'MacOS', 'BambuStudio');

const started = Date.now();
const client = new Client({ name: 'live-check', version: '1' });
await client.connect(new StdioClientTransport({ command: binary, args: ['--mcp-stdio', '--datadir', dataDir] }));
console.log(`     started and initialized in ${Date.now() - started} ms`);

let failures = 0;
const reads = [
  ['get_studio_state', {}],
  ['get_project', {}],
  ['get_settings', {}],
  ['get_settings', { scope: 'object', object: 1 }],
  ['describe_settings', { keys: ['layer_height'] }],
  ['list_presets', { kind: 'process' }],
  ['list_filaments', {}],
  ['get_slice_result', {}],
];
for (const [tool, args] of reads) {
  try {
    const at = Date.now();
    const result = await client.callTool({ name: tool, arguments: args });
    assert.ok(!result.isError, result.content[0].text);
    assert.deepEqual(JSON.parse(result.content[0].text), await callBridgeTool(process.env, tool, args));
    console.log(`ok   ${tool} ${JSON.stringify(args)} (${Date.now() - at} ms)`);
  } catch (error) {
    failures++;
    console.log(`FAIL ${tool} ${JSON.stringify(args)}: ${error.message}`);
  }
}

try {
  const result = await client.callTool({ name: 'apply_settings', arguments: { changes: { no_such_setting: 1 } } });
  const answer = JSON.parse(result.content[0].text);
  assert.equal(answer.status, 'rejected', result.content[0].text);
  console.log('ok   apply_settings with an unknown key is rejected, nothing changed');
} catch (error) {
  failures++;
  console.log(`FAIL apply_settings rejection: ${error.message}`);
}

await client.close();
console.log(failures === 0 ? 'all checks passed' : `${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
