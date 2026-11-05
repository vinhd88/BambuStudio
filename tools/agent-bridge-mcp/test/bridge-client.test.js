// Tests the adapter against a real local HTTP server standing in for the bridge.
import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { after, before, beforeEach, describe, it } from 'node:test';

import { callBridgeTool, DATA_DIR_ENV } from '../bridge-client.js';

const TOKEN = 'test-token';

describe('callBridgeTool', () => {
  let dataDir;
  let env;
  let bridge;
  let port;
  let lastRequest;

  const writeDiscovery = (fields) => writeFile(join(dataDir, 'agent-bridge.json'), JSON.stringify({ port, token: TOKEN, pid: process.pid, ...fields }));
  const failsWith = (code, tool = 'get_project') => assert.rejects(callBridgeTool(env, tool, {}), (error) => error.code === code);

  before(async () => {
    dataDir = await mkdtemp(join(tmpdir(), 'agent-bridge-test-'));
    env = { [DATA_DIR_ENV]: dataDir };
    bridge = createServer((req, res) => {
      let body = '';
      req.on('data', (chunk) => { body += chunk; });
      req.on('end', () => {
        lastRequest = { url: req.url, headers: req.headers, body };
        if (req.headers.authorization !== `Bearer ${TOKEN}`) {
          res.writeHead(401).end(JSON.stringify({ ok: false, error: { code: 'bad_token', message: 'wrong' } }));
        } else if (req.url === '/v1/tools/garbled') {
          res.writeHead(200).end('<html>');
        } else {
          res.writeHead(200).end(JSON.stringify({ ok: true, result: { hello: 'world' } }));
        }
      });
    });
    await new Promise((resolve) => bridge.listen(0, '127.0.0.1', resolve));
    port = bridge.address().port;
  });

  after(async () => {
    bridge.close();
    await rm(dataDir, { recursive: true, force: true });
  });

  beforeEach(async () => {
    lastRequest = undefined;
    await rm(join(dataDir, 'agent-bridge.json'), { force: true });
  });

  it('explains missing configuration', async () => {
    await assert.rejects(callBridgeTool({}, 'get_project', {}), (error) => error.code === 'not_configured' && error.message.includes(DATA_DIR_ENV));
  });

  it('reports not running when there is no discovery file', () => failsWith('studio_not_running'));

  it('treats a discovery file from a dead process as not running, without sending the token', async () => {
    await writeDiscovery({ pid: 2 ** 22 + 1 });
    await failsWith('studio_not_running');
    assert.equal(lastRequest, undefined);
  });

  it('reports not running when the port refuses connections', async () => {
    const closed = createServer();
    await new Promise((resolve) => closed.listen(0, '127.0.0.1', resolve));
    const closedPort = closed.address().port;
    await new Promise((resolve) => closed.close(resolve));
    await writeDiscovery({ port: closedPort });
    await failsWith('studio_not_running');
  });

  it('surfaces a rejected token', async () => {
    await writeDiscovery({ token: 'stale-token' });
    await failsWith('bad_token');
  });

  it('rejects a malformed discovery file', async () => {
    await writeFile(join(dataDir, 'agent-bridge.json'), 'not json');
    await failsWith('discovery_unreadable');
    await writeDiscovery({ port: 'not a number' });
    await failsWith('discovery_unreadable');
  });

  it('handles an unreadable response', async () => {
    await writeDiscovery({});
    await failsWith('bad_response', 'garbled');
  });

  it('returns the result, with auth, no Origin and encoded tool name', async () => {
    await writeDiscovery({});
    assert.deepEqual(await callBridgeTool(env, 'get/project', { plate: 1 }), { hello: 'world' });
    assert.equal(lastRequest.url, '/v1/tools/get%2Fproject');
    assert.equal(lastRequest.headers.authorization, `Bearer ${TOKEN}`);
    assert.equal(lastRequest.headers.origin, undefined);
    assert.equal(lastRequest.body, '{"plate":1}');
  });
});
