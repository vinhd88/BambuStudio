// Finds the running Studio through its discovery file and forwards tool calls to the bridge.
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';

export const DATA_DIR_ENV = 'BAMBU_AGENT_DATA_DIR';
const DISCOVERY_FILE = 'agent-bridge.json';
// Longer than the bridge's own UI timeout, so the bridge reports studio_busy before this fires.
const REQUEST_TIMEOUT_MS = 15_000;

export class BridgeCallError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

const notRunning = () => new BridgeCallError('studio_not_running', 'Bambu Studio (custom agent build) is not running. Ask the user to start it, then try again.');

function isProcessAlive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    // EPERM means it exists but belongs to someone else.
    return error.code === 'EPERM';
  }
}

async function readDiscovery(dataDir) {
  let text;
  try {
    text = await readFile(join(dataDir, DISCOVERY_FILE), 'utf8');
  } catch (error) {
    if (error.code === 'ENOENT') throw notRunning();
    throw new BridgeCallError('discovery_unreadable', `Cannot read the bridge discovery file: ${error.code}`);
  }
  let discovery;
  try {
    discovery = JSON.parse(text);
  } catch {
    throw new BridgeCallError('discovery_unreadable', 'The bridge discovery file is not valid JSON.');
  }
  const { port, token, pid } = discovery;
  if (!Number.isInteger(port) || typeof token !== 'string' || !Number.isInteger(pid))
    throw new BridgeCallError('discovery_unreadable', 'The bridge discovery file has an unexpected format.');
  // A crash leaves the file behind. Checking first also avoids sending the token to whatever now owns that port.
  if (!isProcessAlive(pid)) throw notRunning();
  return { port, token };
}

// Calls a tool and returns its result. Throws BridgeCallError on any failure.
export async function callBridgeTool(env, tool, args) {
  const dataDir = env[DATA_DIR_ENV];
  if (!dataDir) throw new BridgeCallError('not_configured', `The MCP server needs ${DATA_DIR_ENV} set to the --datadir that Studio runs with.`);

  const { port, token } = await readDiscovery(dataDir);

  let response;
  try {
    response = await fetch(`http://127.0.0.1:${port}/v1/tools/${encodeURIComponent(tool)}`, {
      method: 'POST',
      headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' },
      body: JSON.stringify(args ?? {}),
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });
  } catch (error) {
    if (error.name === 'TimeoutError') throw new BridgeCallError('studio_busy', 'Studio did not respond in time. It may be busy or waiting on a dialog.');
    throw notRunning();
  }

  let payload;
  try {
    payload = await response.json();
  } catch {
    throw new BridgeCallError('bad_response', `The bridge sent an unreadable response (HTTP ${response.status}).`);
  }
  if (!payload.ok) throw new BridgeCallError(payload.error?.code ?? 'unknown_error', payload.error?.message ?? `HTTP ${response.status}`);
  return payload.result;
}
