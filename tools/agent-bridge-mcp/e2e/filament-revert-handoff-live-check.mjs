// Live check for phase 4 tools against a running custom Studio build. Changes the open project, so run
// it on a scratch copy with no unsaved edits, at least one object, and spiral mode off (an A1 project works).
// Every change it makes is reverted, and the settings afterwards must equal those before.
//
//   BAMBU_AGENT_DATA_DIR=<dev-data> node e2e/filament-revert-handoff-live-check.mjs [--handoff]
//
// --handoff also slices the current plate and opens a copy in the official Studio.
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { BridgeCallError, callBridgeTool } from '../bridge-client.js';

const call = (tool, args) => callBridgeTool(process.env, tool, args);
const handoff = process.argv.includes('--handoff');
let failures = 0;

async function check(name, fn) {
  try {
    await fn();
    console.log(`ok   ${name}`);
  } catch (error) {
    failures++;
    console.log(`FAIL ${name}: ${error.message}`);
  }
}

// Everything that differs from the parent presets, plus object 1's overrides and filament.
async function snapshot() {
  const state = await call('get_studio_state');
  const project = await call('get_project');
  return {
    global: await call('get_settings', {}),
    object: await call('get_settings', { scope: 'object', object: 1 }),
    presets: [state.process_preset, state.filament_presets],
    filaments: project.objects[0].filament ?? null,
  };
}

async function revertAll(ids) {
  for (const id of [...ids].reverse()) {
    const result = await call('revert_change', { change_id: id });
    assert.equal(result.status, 'reverted', `${id}: ${JSON.stringify(result)}`);
    assert.equal(result.not_restored, undefined, `${id} not restored: ${JSON.stringify(result.not_restored)}`);
  }
}

const start = await snapshot();
assert.equal((await call('get_studio_state')).dialog_open, false, 'close dialogs in Studio first');

await check('settings at two levels revert newest first to the exact before state', async () => {
  const ids = [];
  for (const request of [
    { changes: { outer_wall_speed: 151 } },
    { changes: { sparse_infill_density: '17%' } },
    { scope: 'object', object: 1, changes: { wall_loops: 4 } },
  ]) {
    const result = await call('apply_settings', request);
    assert.equal(result.status, 'applied', JSON.stringify(result));
    ids.push(result.change_id);
  }
  await assert.rejects(call('revert_change', { change_id: ids[0] }), (error) => error instanceof BridgeCallError && error.code === 'revert_order');
  await revertAll(ids);
  assert.deepEqual(await snapshot(), start);
});

await check('project-wide bed type: unknown names rejected; a change reverts', async () => {
  const bad = await call('apply_settings', { changes: { curr_bed_type: 'No Such Plate' } });
  assert.equal(bad.status, 'rejected');
  for (const plate of ['High Temp Plate', 'Textured PEI Plate', 'Cool Plate']) {
    const result = await call('apply_settings', { changes: { curr_bed_type: plate } });
    if (result.status !== 'applied' || !result.change_id) continue;
    assert.equal(result.changes[0].after, plate);
    await revertAll([result.change_id]);
    return;
  }
  throw new Error('no other bed type could be applied');
});

await check('select_filament picks a listed preset, rejects unknown ones, and reverts', async () => {
  const { filaments } = await call('list_filaments', {});
  const current = start.presets[1][0];
  const other = filaments.find((f) => f.name !== current && f.source === 'system' && f.type === 'PLA') ?? filaments.find((f) => f.name !== current);
  await assert.rejects(call('select_filament', { slot: 1, name: 'No such filament' }), (error) => error.code === 'not_found');
  const result = await call('select_filament', { slot: 1, name: other.name });
  assert.equal(result.status, 'applied', JSON.stringify(result));
  assert.equal((await call('get_studio_state')).filament_presets[0], other.name);
  await revertAll([result.change_id]);
  assert.equal((await call('get_studio_state')).filament_presets[0], current);
});

await check('select_process_preset refuses to drop edits, keeps them on request, and reverts', async () => {
  const { presets } = await call('list_presets', { kind: 'process' });
  const other = presets.find((p) => !p.selected);
  const edit = await call('apply_settings', { changes: { outer_wall_speed: 152 } });
  const refused = await call('select_process_preset', { name: other.name });
  assert.equal(refused.code, 'unsaved_changes');
  assert.ok(refused.unsaved_keys.includes('outer_wall_speed'), JSON.stringify(refused));
  const kept = await call('select_process_preset', { name: other.name, keep_changes: true });
  assert.equal(kept.status, 'applied', JSON.stringify(kept));
  assert.ok('outer_wall_speed' in (kept.kept_changes ?? {}), JSON.stringify(kept));
  await revertAll([edit.change_id, kept.change_id]);
  assert.deepEqual(await snapshot(), start);
});

await check('set_object_filament changes the object and reverts', async () => {
  const slots = start.presets[1].length;
  const slot = slots > 1 ? 2 : 1;
  const result = await call('set_object_filament', { object: 1, slot });
  if (result.status === 'already_set') return;
  assert.equal(result.status, 'applied', JSON.stringify(result));
  assert.equal((await call('get_project')).objects[0].filament, slot);
  await revertAll([result.change_id]);
  assert.deepEqual(await snapshot(), start);
});

await check('nothing is left to revert from this run', async () => {
  const state = await call('get_studio_state');
  assert.deepEqual([state.process_preset, state.filament_presets], start.presets);
});

if (handoff) {
  await check('handoff refuses an unsliced plate, then opens a sliced copy', async () => {
    let job = await call('slice_plate', {});
    for (let i = 0; i < 200 && ['queued', 'slicing'].includes(job.state); i++) {
      await new Promise((resolve) => setTimeout(resolve, 1500));
      job = await call('get_job', { job_id: job.job_id });
    }
    assert.ok(['done', 'blocked'].includes(job.state), JSON.stringify(job));
    const result = await call('open_in_official_studio', {});
    assert.equal(result.status, 'opened', JSON.stringify(result));
    assert.ok(existsSync(result.copy_path));
    console.log(`     copy ${result.copy_path}, estimate ${result.estimate.time_s} s, official ${result.official_version}`);
  });
}

console.log(failures === 0 ? 'all checks passed' : `${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
