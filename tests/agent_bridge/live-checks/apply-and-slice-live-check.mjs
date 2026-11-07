// Live check for phase 3 tools against a running custom Studio build. Changes the open project, so run
// it on a scratch copy. Needs a project with at least one object, on a printer whose filament range
// and machine limits are known to Studio (an A1 project works).
// Spiral (vase) mode must be off: Studio then raises a dialog for infill changes, and the linked
// settings check reads waiting_for_user instead.
//
//   BAMBU_AGENT_DATA_DIR=<dev-data> node apply-and-slice-live-check.mjs [--slice]
import assert from 'node:assert/strict';
import { callBridgeTool } from './bridge-client.js';

const call = (tool, args) => callBridgeTool(process.env, tool, args);
const slice = process.argv.includes('--slice');
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

const processSettings = async () => (await call('get_settings', { category: 'process' })).process.settings;

await check('a mixed request with unsafe keys is rejected whole and changes nothing', async () => {
  const before = await processSettings();
  const result = await call('apply_settings', {
    changes: { outer_wall_speed: 100, machine_start_gcode: 'G28', nozzle_temperature: 400, nozzle_diameter: 0.6, no_such_key: 1 },
  });
  assert.equal(result.status, 'rejected');
  assert.deepEqual(result.rejections.map((r) => r.key).sort(), ['machine_start_gcode', 'no_such_key', 'nozzle_diameter', 'nozzle_temperature']);
  assert.deepEqual(await processSettings(), before);
});

await check('a global change shows in reads, marks the project unsaved and the plate unsliced', async () => {
  const current = (await call('get_settings', { category: 'process/Speed', include_defaults: true })).process.settings;
  // Grouped as { page: { group: { key: value } } }.
  const speed = Object.values(current).flatMap(Object.values).flatMap(Object.entries).find(([key]) => key === 'outer_wall_speed')[1];
  const next = (Array.isArray(speed) ? speed[0] : speed) === 150 ? 151 : 150;
  const result = await call('apply_settings', { changes: { outer_wall_speed: next } });
  assert.equal(result.status, 'applied');
  assert.ok(result.change_id);
  assert.deepEqual(result.changes[0].after, Array.isArray(speed) ? speed.map(() => next) : next);
  const state = await call('get_studio_state');
  assert.equal(state.unsaved_changes, true);
  assert.equal(state.last_change.change_id, result.change_id);
  const plate = await call('get_slice_result');
  assert.ok(plate.state !== 'ready' || plate.may_be_stale, `plate reads ${plate.state} without may_be_stale`);
});

await check('linked settings are reported', async () => {
  const result = await call('apply_settings', { changes: { sparse_infill_density: '17%' } });
  assert.equal(result.status, 'applied');
  const linked = (result.linked_changes ?? []).map((c) => c.key);
  assert.ok(linked.includes('skin_infill_density'), `linked: ${linked}`);
});

await check('an object-level change lands on the object only', async () => {
  const result = await call('apply_settings', { scope: 'object', object: 1, changes: { wall_loops: 3 } });
  assert.equal(result.status, 'applied');
  const object = await call('get_settings', { scope: 'object', object: 1 });
  assert.equal(object.settings.Strength.Walls.wall_loops.value, 3);
});

await check('a key outside the level is rejected', async () => {
  const result = await call('apply_settings', { scope: 'part', object: 1, part: 1, changes: { nozzle_temperature: 220 } });
  assert.equal(result.status, 'rejected');
});

if (slice) {
  await check('slice_plate runs to done with a result', async () => {
    let job = await call('slice_plate', {});
    assert.ok(['slicing', 'done'].includes(job.state), `state ${job.state}: ${job.reason}`);
    for (let i = 0; i < 200 && job.state === 'slicing'; i++) {
      await new Promise((resolve) => setTimeout(resolve, 1500));
      job = await call('get_job', { job_id: job.job_id });
    }
    assert.equal(job.state, 'done', job.reason);
    assert.ok(job.result.time_s > 0);
  });
}

console.log(failures === 0 ? 'all checks passed' : `${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
