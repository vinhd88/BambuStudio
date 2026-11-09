// Live check for the 3D view toolbar tools (orient, arrange, transform, cut, layer height, brim ears,
// paint, text, boolean, ...) against a running custom Studio build. Changes the open project, so run it
// on a scratch copy with no unsaved edits and at least one object. Every change is reverted, and the
// objects afterwards must equal those before.
//
//   BAMBU_AGENT_DATA_DIR=<dev-data> node geometry-tools-live-check.mjs
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { BridgeCallError, callBridgeTool } from './bridge-client.js';

// A change that outlasts the bridge's UI timeout answers in_progress; its outcome then shows in
// get_studio_state's last_change, as agents are told to read it.
async function call(tool, args) {
  const result = await callBridgeTool(process.env, tool, args);
  if (result?.status !== 'in_progress' || !result.change_id) return result;
  for (let i = 0; i < 240; i++) {
    await new Promise((r) => setTimeout(r, 500));
    const last = (await callBridgeTool(process.env, 'get_studio_state')).last_change;
    if (last?.change_id === result.change_id && last.status !== 'running' && last.status !== 'in_progress') return last;
  }
  throw new Error(`${tool} ${result.change_id} did not finish`);
}
let failures = 0;

async function check(name, fn) {
  try {
    await fn();
    console.log(`ok   ${name}`);
  } catch (error) {
    failures++;
    console.log(`FAIL ${name}: ${error.code ?? ''} ${error.message}`);
  }
}

// Objects and their placement, sizes and paint/print-aid flags: what a revert must restore.
async function state() {
  const project = await call('get_project');
  const geometry = [];
  for (let i = 1; i <= project.objects.length; i++) geometry.push(await call('get_object_geometry', { object: i }));
  return { plates: project.plates?.length, objects: project.objects.map(({ object, name, parts }) => ({ object, name, parts: parts.length })), geometry };
}

async function waitForJob(jobId) {
  for (let i = 0; i < 120; i++) {
    const job = await call('get_job', { job_id: jobId });
    if (job.state === 'done') return job;
    await new Promise((r) => setTimeout(r, 500));
  }
  throw new Error(`job ${jobId} did not finish`);
}

// Runs one change, checks it, reverts it, and checks the project is back as it was.
async function changeAndRevert(name, tool, args, verify) {
  await check(name, async () => {
    const before = await state();
    const result = await call(tool, args);
    assert.ok(result.change_id, `no change_id: ${JSON.stringify(result)}`);
    if (result.job_id) Object.assign(result, await waitForJob(result.job_id));
    try {
      await verify?.(result, before);
    } finally {
      const reverted = await call('revert_change', { change_id: result.change_id });
      assert.equal(reverted.status, 'reverted', JSON.stringify(reverted));
    }
    assert.deepEqual(await state(), before, 'project differs after revert');
  });
}

function cubeStl(size) {
  const v = (x, y, z) => `vertex ${x * size} ${y * size} ${z * size}`;
  const faces = [
    [[0, 0, 0], [0, 1, 0], [1, 1, 0]], [[0, 0, 0], [1, 1, 0], [1, 0, 0]], [[0, 0, 1], [1, 0, 1], [1, 1, 1]], [[0, 0, 1], [1, 1, 1], [0, 1, 1]],
    [[0, 0, 0], [1, 0, 0], [1, 0, 1]], [[0, 0, 0], [1, 0, 1], [0, 0, 1]], [[0, 1, 0], [0, 1, 1], [1, 1, 1]], [[0, 1, 0], [1, 1, 1], [1, 1, 0]],
    [[0, 0, 0], [0, 0, 1], [0, 1, 1]], [[0, 0, 0], [0, 1, 1], [0, 1, 0]], [[1, 0, 0], [1, 1, 0], [1, 1, 1]], [[1, 0, 0], [1, 1, 1], [1, 0, 1]],
  ];
  return `solid cube\n${faces.map((f) => `facet normal 0 0 0\nouter loop\n${f.map((p) => v(...p)).join('\n')}\nendloop\nendfacet`).join('\n')}\nendsolid cube\n`;
}

const first = await call('get_object_geometry', { object: 1 });
const box = first.instances[0];
const midZ = (box.bbox_min_mm[2] + box.bbox_max_mm[2]) / 2;

await check('get_object_geometry reads object 1', async () => {
  assert.ok(box.size_mm.every((n) => n > 0));
  assert.ok(first.parts.length >= 1);
});

await changeAndRevert('transform_object rotates 45 deg', 'transform_object', { object: 1, rotate_deg: [0, 0, 45] }, (r) => {
  assert.notDeepEqual(r.geometry[0].rotation_deg, box.rotation_deg);
});
await changeAndRevert('transform_object scales 120%', 'transform_object', { object: 1, scale_pct: 120 }, (r) => {
  assert.ok(Math.abs(r.geometry[0].size_mm[0] - box.size_mm[0] * 1.2) < 0.5, JSON.stringify(r.geometry[0].size_mm));
});
await changeAndRevert('transform_object moves 5 mm in x', 'transform_object', { object: 1, move_mm: [5, 0, 0] }, (r) => {
  assert.ok(Math.abs(r.geometry[0].position_mm[0] - box.position_mm[0] - 5) < 0.05);
});
await changeAndRevert('orient_objects on object 1', 'orient_objects', { objects: [1] }, (r) => assert.equal(r.state, 'done'));
await changeAndRevert('arrange_plate all', 'arrange_plate', { spacing_mm: 10 }, (r) => assert.equal(r.state, 'done'));
await changeAndRevert('add_plate', 'add_plate', {}, async (r, before) => {
  assert.equal((await call('get_project')).plates.length, before.plates + 1);
});
await changeAndRevert('set_variable_layer_height adaptive', 'set_variable_layer_height', { object: 1, mode: 'adaptive', quality: 0.3 }, async () => {
  assert.equal((await call('get_object_geometry', { object: 1 })).variable_layer_height, true);
});
await changeAndRevert('brim_ears auto', 'brim_ears', { object: 1, action: 'auto' }, (r) => assert.ok(r.ears > 0));
await check('lay_on_face lists faces', async () => {
  const faces = await call('lay_on_face', { object: 1, list_only: true });
  assert.ok(faces.faces.length > 0);
});
await changeAndRevert('lay_on_face face 2', 'lay_on_face', { object: 1, face: 2 });
await changeAndRevert('cut_object at mid height', 'cut_object', { object: 1, height_mm: midZ }, (r, before) => {
  assert.equal(r.objects.length, before.objects.length + 1);
});
await changeAndRevert('simplify_object keeps 50%', 'simplify_object', { object: 1, part: 1, keep_percent: 50 }, (r) => {
  assert.ok(r.triangles_after < r.triangles_before);
});
await changeAndRevert('paint_facets supports on overhangs', 'paint_facets', { object: 1, kind: 'supports', select: 'all', z_to_mm: midZ }, async (r) => {
  assert.ok(r.facets_painted > 0);
  const g = await call('get_object_geometry', { object: 1 });
  assert.ok(g.parts[0].painted?.includes('supports'));
});
await changeAndRevert('paint_facets seam facing back', 'paint_facets', { object: 1, kind: 'seam', select: 'facing', facing: 'back' });
await changeAndRevert('add_text new object', 'add_text', { text: 'A1 test', size_mm: 8 }, (r, before) => {
  assert.equal(r.objects.length, before.objects.length + 1);
});
await changeAndRevert('add_text emboss on top', 'add_text', { text: 'HI', mode: 'emboss_top', object: 1, size_mm: 5, depth_mm: 1 }, (r, before) => {
  assert.equal(r.objects[0].parts, before.objects[0].parts + 1);
});
await check('mesh_boolean union of text and part', async () => {
  const before = await state();
  const text = await call('add_text', { text: 'HI', mode: 'emboss_top', object: 1, size_mm: 5, depth_mm: 1 });
  const parts = text.objects[0].parts;
  try {
    const result = await call('mesh_boolean', { object: 1, part_a: 1, part_b: parts, operation: 'union' });
    assert.equal(result.parts, parts - 1);
    assert.equal((await call('revert_change', { change_id: result.change_id })).status, 'reverted');
  } finally {
    assert.equal((await call('revert_change', { change_id: text.change_id })).status, 'reverted');
  }
  assert.deepEqual(await state(), before);
});
const stl = join(tmpdir(), 'agent-live-check-cube.stl');
writeFileSync(stl, cubeStl(20));
await changeAndRevert('add_model adds a cube', 'add_model', { path: stl }, (r, before) => {
  assert.equal(r.objects.length, before.objects.length + 1);
});
await check('split_object on the test object: refused or unchanged, nothing left to revert', async () => {
  const before = await state();
  try {
    const result = await call('split_object', { object: 1 });
    if (result.status !== 'unchanged') {
      await call('revert_change', { change_id: result.change_id });
      throw new Error(`split changed the project: ${result.status}`);
    }
  } catch (e) {
    if (!(e instanceof BridgeCallError && e.code === 'cannot_split')) throw e;
  }
  assert.deepEqual(await state(), before);
});
await check('split_object splits two-letter text into objects', async () => {
  const before = await state();
  const text = await call('add_text', { text: 'HI', size_mm: 10 });
  const index = text.added_objects[0];
  try {
    const split = await call('split_object', { object: index, into: 'objects' });
    assert.equal(split.objects.length, text.objects.length + 1);
    assert.equal((await call('revert_change', { change_id: split.change_id })).status, 'reverted');
  } finally {
    assert.equal((await call('revert_change', { change_id: text.change_id })).status, 'reverted');
  }
  assert.deepEqual(await state(), before);
});
await check('revert order: an older geometry change waits for the newer one', async () => {
  const a = await call('transform_object', { object: 1, move_mm: [3, 0, 0] });
  const b = await call('transform_object', { object: 1, move_mm: [0, 3, 0] });
  await assert.rejects(call('revert_change', { change_id: a.change_id }), (e) => e.code === 'revert_order');
  assert.equal((await call('revert_change', { change_id: b.change_id })).status, 'reverted');
  assert.equal((await call('revert_change', { change_id: a.change_id })).status, 'reverted');
});

console.log(failures === 0 ? '\nall checks passed' : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
