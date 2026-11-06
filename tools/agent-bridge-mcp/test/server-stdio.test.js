// Runs the real adapter over stdio, as an MCP client would, with Studio closed.
import assert from 'node:assert/strict';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { after, before, describe, it } from 'node:test';
import { fileURLToPath } from 'node:url';

import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

const serverPath = join(dirname(fileURLToPath(import.meta.url)), '..', 'server.js');
const EXPECTED_TOOLS = ['apply_settings', 'describe_settings', 'get_filament', 'get_job', 'get_project', 'get_settings', 'get_slice_result', 'get_studio_state', 'list_filaments', 'list_presets', 'slice_plate'];

describe('MCP adapter with Studio closed', () => {
  let client;
  let dataDir;

  before(async () => {
    dataDir = await mkdtemp(join(tmpdir(), 'agent-bridge-stdio-'));
    client = new Client({ name: 'test', version: '1' });
    await client.connect(new StdioClientTransport({ command: process.execPath, args: [serverPath], env: { BAMBU_AGENT_DATA_DIR: dataDir } }));
  });

  after(async () => {
    await client.close();
    await rm(dataDir, { recursive: true, force: true });
  });

  it('still lists every tool', async () => {
    const { tools } = await client.listTools();
    assert.deepEqual(tools.map((tool) => tool.name).sort(), EXPECTED_TOOLS);
  });

  it('answers a call with a clear error', async () => {
    const result = await client.callTool({ name: 'get_studio_state', arguments: {} });
    assert.equal(result.isError, true);
    assert.match(result.content[0].text, /^studio_not_running: .*not running/);
  });
});
