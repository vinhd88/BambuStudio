// MCP tools the adapter lists. Defined here rather than fetched from Studio, so agents see the
// same tools whether or not Studio is running.
const CATEGORY = {
  type: 'string',
  description: 'Limit to one tab or page: process, filament, printer, or tab/page such as process/Speed or filament/Cooling.',
};
const COMPATIBLE_ONLY = {
  type: 'boolean',
  description: 'Only presets shown for the current printer. Default true.',
};
const noArgs = { type: 'object', properties: {}, additionalProperties: false };

export const TOOLS = [
  {
    name: 'get_studio_state',
    description: 'Quick status of Bambu Studio: open project, unsaved changes, current plate, slicing state, whether a dialog is open, selected presets. Call this first. Works while a dialog is open; other tools return studio_busy then. Also reports dialog_title, a running change or job, and last_change: the outcome of the latest apply_settings, including one that waited on a dialog.',
    inputSchema: noArgs,
  },
  {
    name: 'get_project',
    description: 'The open project: plates with their object instances, objects with parts and height ranges, filament slots. Lists which settings are overridden at each level; read their values with get_settings. The sliced flag can be out of date for plates not on screen. All numbers count from 1, as in the Studio UI.',
    inputSchema: noArgs,
  },
  {
    name: 'get_settings',
    description: 'Setting values at one level, grouped by page and group as in the Studio UI. By default returns only changed values as { value, base }. scope global: process, printer and per-slot filament presets compared with their parent preset. Other scopes: keys overridden there, with base being the global process value.',
    inputSchema: {
      type: 'object',
      properties: {
        scope: { type: 'string', enum: ['global', 'plate', 'object', 'part', 'height_range'], description: 'Default global.' },
        plate: { type: 'integer', minimum: 1, description: 'For scope plate. Default current plate.' },
        object: { type: 'integer', minimum: 1, description: 'Required for object, part and height_range.' },
        part: { type: 'integer', minimum: 1, description: 'Required for scope part.' },
        range: { type: 'integer', minimum: 1, description: 'Required for scope height_range.' },
        category: CATEGORY,
        include_defaults: { type: 'boolean', description: 'Also return unchanged values, as bare values. Much larger; combine with category. Default false.' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'describe_settings',
    description: 'What settings mean: type, unit, min and max, allowed values, default, tooltip, and where they appear in the UI. Give keys or a category.',
    inputSchema: {
      type: 'object',
      properties: {
        keys: { type: 'array', items: { type: 'string' }, minItems: 1, description: 'Setting names, such as outer_wall_speed.' },
        category: CATEGORY,
      },
      additionalProperties: false,
    },
  },
  {
    name: 'list_presets',
    description: 'Preset names of one kind, with source (system, user, project) and which one is selected.',
    inputSchema: {
      type: 'object',
      properties: {
        kind: { type: 'string', enum: ['process', 'filament', 'printer'] },
        compatible_only: COMPATIBLE_ONLY,
      },
      required: ['kind'],
      additionalProperties: false,
    },
  },
  {
    name: 'list_filaments',
    description: 'Available filament presets with type, vendor, source, and the project slots using each one.',
    inputSchema: {
      type: 'object',
      properties: { compatible_only: COMPATIBLE_ONLY },
      additionalProperties: false,
    },
  },
  {
    name: 'get_filament',
    description: 'One filament preset: its resolved values, and what differs from its parent as { value, base }. Give a preset name or a project slot.',
    inputSchema: {
      type: 'object',
      properties: {
        name: { type: 'string', description: 'Preset name from list_filaments.' },
        slot: { type: 'integer', minimum: 1, description: 'Project filament slot.' },
        category: { type: 'string', description: 'Limit to one page, such as filament/Cooling.' },
        include_defaults: { type: 'boolean', description: 'Set false to return only what differs from the parent. Default true.' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'get_slice_result',
    description: 'Slice result of a plate: state (ready, not_sliced, empty_plate, slicing), then when ready the time, per-feature time, filament use and warnings. may_be_stale means the plate is not on screen, so Studio has not re-checked it against settings changed since it was sliced. Default current plate.',
    inputSchema: {
      type: 'object',
      properties: { plate: { type: 'integer', minimum: 1 } },
      additionalProperties: false,
    },
  },
  {
    name: 'apply_settings',
    description: 'Change settings in the open project, live in the Studio UI. Validates every key first: if any fails, status is rejected, rejections gives a reason per key, and nothing changes. On success returns changes (before and after), linked_changes (settings Studio changed along with them), not_applied, warnings and a change_id. status waiting_for_user means Studio opened a dialog: ask the user to answer it, then read last_change from get_studio_state. Values use the forms get_settings returns; a single value on a list setting sets every element. Printer settings, G-code, machine limits and output naming are read-only. Temperatures must stay in the filament range and speeds within machine limits. Does not save the project.',
    inputSchema: {
      type: 'object',
      properties: {
        changes: { type: 'object', description: 'Setting names and new values, such as { "outer_wall_speed": 150, "sparse_infill_density": "15%" }.', minProperties: 1 },
        scope: { type: 'string', enum: ['global', 'plate', 'object', 'part', 'height_range'], description: 'Default global: the selected process and filament presets. Other levels override it for one plate, object, part or height range.' },
        slot: { type: 'integer', minimum: 1, description: 'For filament settings at scope global when the project has several slots. Must be the slot selected on the Filament tab.' },
        plate: { type: 'integer', minimum: 1, description: 'For scope plate. Default current plate.' },
        object: { type: 'integer', minimum: 1, description: 'Required for object, part and height_range.' },
        part: { type: 'integer', minimum: 1, description: 'Required for scope part.' },
        range: { type: 'integer', minimum: 1, description: 'Required for scope height_range.' },
      },
      required: ['changes'],
      additionalProperties: false,
    },
  },
  {
    name: 'slice_plate',
    description: 'Start slicing a plate, as the Slice button does. Returns at once with a job_id and state; poll get_job. state blocked means Studio declined, with a reason, and no slice ran. Selects the plate if it is not current and shows the Preview tab. Default current plate.',
    inputSchema: {
      type: 'object',
      properties: { plate: { type: 'integer', minimum: 1 } },
      additionalProperties: false,
    },
  },
  {
    name: 'get_job',
    description: 'State of a slice job from slice_plate: queued, slicing (with progress_percent), done (with the result, as get_slice_result), failed or blocked (with a reason). result_outdated means settings or the plate changed after the slice. Works while a dialog is open.',
    inputSchema: {
      type: 'object',
      properties: { job_id: { type: 'string' } },
      required: ['job_id'],
      additionalProperties: false,
    },
  },
];
