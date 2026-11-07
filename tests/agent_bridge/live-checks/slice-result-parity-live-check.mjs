// Live check that get_slice_result carries the preview panel's numbers. Prints them side by side with
// the panel's labels so a person can compare against the dev Studio, and asserts internal consistency.
// Read-only unless --slice, which slices the current plate first (no settings change).
//
//   BAMBU_AGENT_DATA_DIR=<dev-data> node slice-result-parity-live-check.mjs [--slice] [--plate N]
import assert from 'node:assert/strict';
import { callBridgeTool } from './bridge-client.js';

const call = (tool, args) => callBridgeTool(process.env, tool, args);
const plateArg = process.argv.indexOf('--plate');
const plate = plateArg > 0 ? Number(process.argv[plateArg + 1]) : undefined;
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

// Same short form as the panel: 1h52m, 6m16s, 41s.
const hms = (s) => {
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = Math.round(s % 60);
  return h > 0 ? `${h}h${m}m` : m > 0 ? `${m}m${sec}s` : `${sec}s`;
};
const sum = (rows, key) => rows.reduce((total, row) => total + (row[key] ?? 0), 0);
const near = (actual, expected, tolerance, label) =>
  assert.ok(Math.abs(actual - expected) <= tolerance, `${label}: ${actual} vs ${expected} (±${tolerance})`);

if (process.argv.includes('--slice')) {
  let job = await call('slice_plate', plate ? { plate } : {});
  for (let i = 0; i < 400 && job.state === 'slicing'; i++) {
    await new Promise((resolve) => setTimeout(resolve, 1500));
    job = await call('get_job', { job_id: job.job_id });
  }
  assert.equal(job.state, 'done', `slice ended ${job.state}: ${job.reason ?? ''}`);
}

const base = plate ? { plate } : {};
const summary = await call('get_slice_result', base);
assert.equal(summary.state, 'ready', `plate is ${summary.state}; slice it first or pass --slice`);
const started = Date.now();
const detail = await call('get_slice_result', { ...base, include: ['layers', 'objects'] });
const detailMs = Date.now() - started;

console.log(`\nPlate ${summary.plate}${summary.may_be_stale ? ' (may be stale)' : ''} — compare with the dev Studio preview panel (Line Type view)\n`);
console.log('Summary');
console.log(`  Total Filament              ${summary.total_m.toFixed(2)} m  ${summary.total_g.toFixed(2)} g`);
console.log(`  Model Filament              ${summary.model_m.toFixed(2)} m  ${summary.model_g.toFixed(2)} g`);
console.log(`  Cost                        ${summary.cost.toFixed(2)}`);
console.log(`  Prepare (and timelapse)     ${hms(summary.prepare_time_s)}${summary.timelapse_time_s ? ' + ' + hms(summary.timelapse_time_s) : ''}`);
console.log(`  Model printing time         ${hms(summary.model_time_s)}`);
console.log(`  Total time                  ${hms(summary.time_s)}  (${summary.time_s} s)`);
console.log('\nLine Type: feature, time, percent, filament');
for (const f of summary.features)
  console.log(`  ${f.feature.padEnd(26)}  ${hms(f.time_s).padStart(7)}  ${f.percent.toFixed(1).padStart(5)}%  ${f.used_m.toFixed(2)} m  ${f.used_g.toFixed(2)} g`);
for (const m of summary.moves)
  console.log(`  ${m.type.padEnd(26)}  ${m.time_s ? hms(m.time_s).padStart(7) + '  ' + m.percent.toFixed(1).padStart(5) + '%' : ''.padStart(16)}  count ${m.count}`);
console.log('\nFilament per slot (Color Print view): model / support / flushed / tower / total');
for (const f of summary.filaments)
  console.log(`  slot ${f.slot}  ${f.model_g} / ${f.support_g} / ${f.flushed_g} / ${f.tower_g} / ${f.total_g} g   used ${f.used_g} g, cost ${f.cost}`);
console.log('\nLegend ranges', JSON.stringify(summary.ranges));
if (summary.custom_gcode) console.log('Custom G-code', JSON.stringify(summary.custom_gcode));

const layers = detail.layers_detail;
const slowest = [...layers].sort((a, b) => b.time_s - a.time_s).slice(0, 3);
console.log(`\nLayers: ${layers.length} rows (${detail.layer_time_source ?? 'slider times'}), slowest: ${slowest.map((l) => `#${l.layer} ${l.time_s}s`).join(', ')}`);
if (detail.objects_detail)
  for (const o of detail.objects_detail) console.log(`  ${String(o.name).padEnd(26)}  ${hms(o.time_s).padStart(7)}  ${o.percent_time}%  ${o.used_g} g`);
else console.log(`Objects: ${detail.objects_unavailable}`);
console.log(`get_slice_result with layers+objects took ${detailMs} ms\n`);

await check('old keys are still there', async () => {
  for (const key of ['plate', 'state', 'time_s', 'prepare_time_s', 'layers', 'total_g', 'total_m', 'filaments', 'features', 'warnings'])
    assert.ok(key in summary, key);
  assert.ok(!('layers_detail' in summary) && !('objects_detail' in summary), 'detail returned without include');
});

await check('model + prepare + timelapse = total time', async () =>
  near(summary.model_time_s + summary.prepare_time_s + summary.timelapse_time_s, summary.time_s, 1, 'time'));

await check('model filament = total - support - flushed - tower', async () =>
  near(summary.model_g, summary.total_g - summary.support_g - summary.flushed_g - summary.tower_g, 0.05, 'model g'));

await check('one layer row per layer', async () => {
  if (!detail.layers_truncated) assert.equal(layers.length, summary.layers);
});

await check('layer times add up to total minus prepare (±2%)', async () => {
  const expected = summary.time_s - summary.prepare_time_s;
  near(sum(layers, 'time_s'), expected, expected * 0.02, 'layer time');
});

await check('layer filament adds up to the plate total (within flushed, ±3%)', async () => {
  const total = sum(layers, 'used_g');
  assert.ok(total >= (summary.total_g - summary.flushed_g) * 0.97 && total <= summary.total_g * 1.03,
    `layers ${total.toFixed(2)} g, total ${summary.total_g} g, flushed ${summary.flushed_g} g`);
});

if (detail.objects_detail)
  await check('object rows add up to total time (±1%)', async () =>
    near(sum(detail.objects_detail, 'time_s'), summary.time_s, summary.time_s * 0.01, 'object time'));

await check('bad include is refused', async () => {
  await assert.rejects(call('get_slice_result', { include: ['speeds'] }));
});

console.log(failures === 0 ? 'all checks passed' : `${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
