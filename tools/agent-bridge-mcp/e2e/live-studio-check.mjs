#!/usr/bin/env node
// Checks the whole path against a running custom Studio: MCP client -> adapter -> bridge -> UI thread.
// Run with Studio already open on a project and on the data dir below:
//   BAMBU_AGENT_DATA_DIR=/path/to/dev-data node e2e/live-studio-check.mjs [--expect-empty] [--expect-process-changed=a,b]
// --expect-process-changed lists keys the project changes from its process preset. Find them in
// different_settings_to_system in the 3MF's Metadata/project_settings.config.
// With a dialog open in Studio, such as Preferences, add --expect-dialog to check that reads are refused.
// Read-only: changes nothing in Studio. Prints each tool's size and time, so token cost stays visible.
import assert from 'node:assert/strict';
import { readFileSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';

const READ_LIMIT_MS = 100;
const dataDir = process.env.BAMBU_AGENT_DATA_DIR;
if (!dataDir) throw new Error('Set BAMBU_AGENT_DATA_DIR');
const expectEmpty = process.argv.includes('--expect-empty');
const expectChanged = process.argv.find((arg) => arg.startsWith('--expect-process-changed='))?.split('=')[1].split(',') ?? [];
const serverPath = join(dirname(fileURLToPath(import.meta.url)), '..', 'server.js');

const client = new Client({ name: 'live-studio-check', version: '1' });
await client.connect(new StdioClientTransport({ command: process.execPath, args: [serverPath], env: { BAMBU_AGENT_DATA_DIR: dataDir, PATH: process.env.PATH } }));

const slow = [];
async function call(name, args = {}) {
  const started = performance.now();
  const reply = await client.callTool({ name, arguments: args });
  const ms = performance.now() - started;
  const text = reply.content[0].text;
  console.log(`${name.padEnd(18)} ${JSON.stringify(args).padEnd(44)} ${String(text.length).padStart(7)} chars ${ms.toFixed(0).padStart(4)} ms`);
  if (ms > READ_LIMIT_MS) slow.push(`${name} ${JSON.stringify(args)}: ${ms.toFixed(0)} ms`);
  if (reply.isError) return { error: text };
  return JSON.parse(text);
}
const hasError = (result, code) => assert.ok(result.error?.startsWith(code), `expected ${code}, got ${JSON.stringify(result).slice(0, 200)}`);

const state = await call('get_studio_state');
console.log('  state:', JSON.stringify(state));
if (process.argv.includes('--expect-dialog')) {
  assert.equal(state.dialog_open, true, 'open a dialog in Studio first, such as Preferences');
  hasError(await call('get_project'), 'studio_busy');
  hasError(await call('get_settings'), 'studio_busy');
  await client.close();
  console.log('DIALOG CHECKS PASSED');
  process.exit(0);
}
assert.equal(state.dialog_open, false, 'close dialogs in Studio before running this');

const project = await call('get_project');
assert.equal(project.plates.length, state.plate_count);
assert.ok(project.filaments.length >= 1);
console.log('  plates:', project.plates.length, 'objects:', project.objects.length, 'slots:', JSON.stringify(project.filaments));

const global = await call('get_settings');
console.log('  changed process:', JSON.stringify(global.process?.settings).slice(0, 400));
console.log('  changed slot 1 :', JSON.stringify(global.filaments?.[0]?.settings).slice(0, 400));
assert.equal(global.process.preset, state.process_preset);
const changedProcessKeys = Object.values(global.process.settings).flatMap((page) => Object.values(page).flatMap((group) => Object.keys(group)));
assert.deepEqual(changedProcessKeys.sort(), [...expectChanged].sort(), 'changed process settings differ from what the project file records');
for (const page of Object.values(global.process.settings)) for (const group of Object.values(page)) for (const [key, entry] of Object.entries(group))
  assert.notDeepEqual(entry.value, entry.base, `${key} reported as changed but equal to its base`);
assert.equal(global.printer.preset, state.printer_preset);

const everything = await call('get_settings', { include_defaults: true });
const speed = await call('get_settings', { category: 'process/Speed', include_defaults: true });
assert.deepEqual(Object.keys(speed.process.settings), ['Speed']);
assert.equal(speed.printer, undefined);

// Secrets must not appear anywhere, even in a full dump.
const dump = JSON.stringify(everything);
for (const secret of ['printhost', 'print_host', 'post_process', 'access_code', 'dev_id', 'user_id']) assert.ok(!dump.includes(secret), `leaked ${secret}`);

const described = await call('describe_settings', { keys: ['layer_height', 'printhost_apikey', 'no_such_key'] });
assert.deepEqual(described.settings.map((s) => s.key), ['layer_height']);
assert.deepEqual(described.unknown, ['printhost_apikey', 'no_such_key']);
hasError(await call('describe_settings'), 'invalid_args');
await call('describe_settings', { category: 'process/Speed' });

for (const kind of ['process', 'filament', 'printer']) {
  const presets = await call('list_presets', { kind });
  console.log(`  ${kind} presets:`, presets.count, 'selected:', presets.presets.filter((p) => p.selected).map((p) => p.name).join());
  assert.ok(presets.count >= 1);
}
hasError(await call('list_presets', { kind: 'bogus' }), 'invalid_args');

const filaments = await call('list_filaments');
await call('list_filaments', { compatible_only: false });
console.log('  user filaments:', JSON.stringify(filaments.filaments.filter((f) => f.source === 'user')));

const slotFilament = await call('get_filament', { slot: 1 });
assert.equal(slotFilament.preset, project.filaments[0].preset);
assert.ok(Object.keys(slotFilament.settings).length > 0);
const customPetg = 'Generic PETG @BBL A1 - Speed 12mm3';
const custom = await call('get_filament', { name: customPetg, category: 'filament/Filament' });
if (custom.error) console.log(`  custom PETG not available here: ${custom.error}`);
else assert.equal(custom.source, 'user');
if (!custom.error) console.log('  custom PETG:', JSON.stringify({ source: custom.source, base: custom.base_preset, compatible: custom.compatible, type: custom.type }), JSON.stringify(custom.settings).slice(0, 500));
hasError(await call('get_filament', { name: 'No such filament' }), 'not_found');
hasError(await call('get_filament'), 'invalid_args');

const slice = await call('get_slice_result');
console.log('  slice:', JSON.stringify(slice).slice(0, 600));
hasError(await call('get_slice_result', { plate: 999 }), 'not_found');
hasError(await call('get_settings', { plate: 0, scope: 'plate' }), 'invalid_args');
hasError(await call('get_settings', { scope: 'object' }), 'invalid_args');

await call('get_settings', { scope: 'plate' });
if (expectEmpty) {
  assert.equal(project.objects.length, 0);
  assert.equal(slice.state, 'empty_plate');
  hasError(await call('get_settings', { scope: 'object', object: 1 }), 'not_found');
} else {
  assert.ok(project.objects.length >= 1);
  const object = await call('get_settings', { scope: 'object', object: 1 });
  console.log('  object 1:', JSON.stringify(object).slice(0, 300));
  await call('get_settings', { scope: 'part', object: 1, part: 1 });
  hasError(await call('get_settings', { scope: 'part', object: 1, part: 9999 }), 'not_found');
  hasError(await call('get_settings', { scope: 'height_range', object: 1, range: 9999 }), 'not_found');
}

// The adapter must be the only way in: check the raw HTTP rules against the live server too.
const discoveryPath = join(dataDir, 'agent-bridge.json');
assert.equal(statSync(discoveryPath).mode & 0o777, 0o600, 'discovery file must be owner-only');
const { port, token } = JSON.parse(readFileSync(discoveryPath, 'utf8'));
const url = `http://127.0.0.1:${port}/v1/tools/get_studio_state`;
assert.equal((await fetch(url, { method: 'POST' })).status, 401);
assert.equal((await fetch(url, { method: 'POST', headers: { authorization: 'Bearer wrong' } })).status, 401);
assert.equal((await fetch(url, { method: 'POST', headers: { authorization: `Bearer ${token}`, origin: 'https://evil.example' } })).status, 403);
assert.equal((await fetch(url, { method: 'POST', headers: { authorization: `Bearer ${token}` } })).status, 200);
console.log('auth checks passed');

await client.close();
assert.equal(slow.length, 0, `over the ${READ_LIMIT_MS} ms budget:\n  ${slow.join('\n  ')}`);
console.log('ALL CHECKS PASSED');
