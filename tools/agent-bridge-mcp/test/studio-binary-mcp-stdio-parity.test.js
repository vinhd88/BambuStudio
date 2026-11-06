// Runs `BambuStudio --mcp-stdio` (the Node-free MCP server built into the agent build) with the MCP
// SDK client, Studio closed, and checks it offers exactly what this adapter offers. Skipped when the
// app has not been built. Set BAMBU_STUDIO_BINARY to test another build.
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { after, before, describe, it } from 'node:test';
import { fileURLToPath } from 'node:url';

import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

import { TOOLS } from '../tool-definitions.js';

const repo = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
const binary = process.env.BAMBU_STUDIO_BINARY ?? join(repo, 'build', 'arm64', 'src', 'BambuStudio.app', 'Contents', 'MacOS', 'BambuStudio');

describe('BambuStudio --mcp-stdio with Studio closed', { skip: !existsSync(binary) && `no build at ${binary}` }, () => {
  let client;
  let dataDir;
  let stderr = '';

  before(async () => {
    dataDir = await mkdtemp(join(tmpdir(), 'agent-bridge-binary-'));
    client = new Client({ name: 'test', version: '1' });
    const transport = new StdioClientTransport({ command: binary, args: ['--mcp-stdio', '--datadir', dataDir], stderr: 'pipe' });
    transport.stderr?.on('data', (chunk) => (stderr += chunk));
    await client.connect(transport);
  });

  after(async () => {
    await client?.close();
    await rm(dataDir, { recursive: true, force: true });
  });

  it('lists the same tools, descriptions and schemas as the Node adapter', async () => {
    const { tools } = await client.listTools();
    assert.deepEqual(tools, TOOLS);
  });

  it('identifies itself as bambu-studio', () => {
    assert.equal(client.getServerVersion()?.name, 'bambu-studio');
  });

  it('answers a call with studio_not_running', async () => {
    const result = await client.callTool({ name: 'get_studio_state', arguments: {} });
    assert.equal(result.isError, true);
    assert.match(result.content[0].text, /^studio_not_running: .*not running/);
  });

  it('writes nothing to stderr when configured', () => {
    assert.equal(stderr, '');
  });
});
