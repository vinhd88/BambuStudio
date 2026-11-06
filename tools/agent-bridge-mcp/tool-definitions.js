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
    description: 'Quick status of Bambu Studio: open project, unsaved changes, current plate, slicing state, whether a dialog is open, selected presets. Call this first. Works while a dialog is open; other tools return studio_busy then. Also reports dialog_title, a running change or job, last_change: the outcome of the latest agent change, including one that waited on a dialog, and last_handoff: the outcome of the latest open_in_official_studio.',
    inputSchema: noArgs,
  },
  {
    name: 'get_project',
    description: 'The open project: plates with their object instances, objects (with the filament slot each prints with) with parts and height ranges, filament slots. Lists which settings are overridden at each level; read their values with get_settings. The sliced flag can be out of date for plates not on screen. All numbers count from 1, as in the Studio UI.',
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
    description: 'Change settings in the open project, live in the Studio UI. Validates every key first: if any fails, status is rejected, rejections gives a reason per key, and nothing changes. On success returns changes (before and after), linked_changes (settings Studio changed along with them), not_applied, warnings and a change_id. status waiting_for_user means Studio opened a dialog: ask the user to answer it, then read last_change from get_studio_state. Values use the forms get_settings returns; a single value on a list setting sets every element. Printer settings, G-code, machine limits and output naming are read-only. curr_bed_type at scope global sets the project-wide bed type, as the sidebar does; at scope plate, for one plate. Temperatures must stay in the filament range and speeds within machine limits. Does not save the project.',
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
  {
    name: 'select_filament',
    description: 'Pick the filament preset for a project slot, as choosing it in the sidebar does; the slot keeps its colour. Only presets Studio lists for the printer are accepted; others are rejected with a reason. status rejected with code unsaved_changes means the slot\'s preset has unsaved edits: ask the user to save or discard them. Returns before, after and a change_id for revert_change.',
    inputSchema: {
      type: 'object',
      properties: {
        slot: { type: 'integer', minimum: 1, description: 'Project filament slot, as in get_project.' },
        name: { type: 'string', description: 'Preset name from list_filaments.' },
      },
      required: ['slot', 'name'],
      additionalProperties: false,
    },
  },
  {
    name: 'select_process_preset',
    description: 'Switch the process preset, as picking it in the Process list does. If the current one has unsaved edits, status is rejected with code unsaved_changes and the edited keys, unless keep_changes is true: then Studio carries the edits over to the new preset (kept_changes lists them). linked_changes lists filament slots Studio switched because the old filament does not suit the new process. Returns a change_id for revert_change.',
    inputSchema: {
      type: 'object',
      properties: {
        name: { type: 'string', description: 'Preset name from list_presets with kind process.' },
        keep_changes: { type: 'boolean', description: 'Carry unsaved edits over to the new preset. Default false.' },
      },
      required: ['name'],
      additionalProperties: false,
    },
  },
  {
    name: 'set_object_filament',
    description: 'Set which filament slot an object prints with, or one part or modifier of it, as the filament column in the object list does. Setting the object makes its parts follow it. One undo step in Studio. Returns a change_id for revert_change.',
    inputSchema: {
      type: 'object',
      properties: {
        object: { type: 'integer', minimum: 1 },
        part: { type: 'integer', minimum: 1, description: 'Only for objects with several parts.' },
        slot: { type: 'integer', minimum: 1, description: 'Filament slot to print with.' },
      },
      required: ['object', 'slot'],
      additionalProperties: false,
    },
  },
  {
    name: 'revert_change',
    description: 'Undo the newest agent change not yet undone: settings, filament or process preset picks, or object filament. Changes are undone newest first; call again to go further back. status refused (with changed_since) means the user changed those values afterwards, or picked another preset, and nothing was reverted. A refused or failed change blocks older ones; after telling the user, call again with skip true and its change_id to leave it as it is and move past it. not_restored lists values Studio set differently while restoring. Works on this Studio session\'s changes in the open project only.',
    inputSchema: {
      type: 'object',
      properties: {
        change_id: { type: 'string', description: 'Optional check: must be the newest change not yet undone. Required with skip.' },
        skip: { type: 'boolean', description: 'Leave this change as it is and mark it done, so older changes can be reverted. Only after its revert was refused or failed. Default false.' },
      },
      additionalProperties: false,
    },
  },
  {
    name: 'open_in_official_studio',
    description: 'Hand the project to the official Bambu Studio for printing: saves a copy with this build\'s G-code to a temporary folder and opens it there. The current plate must be sliced and current (slice_plate, then get_job done). The project here, its file and its unsaved state are untouched. The user starts the print in the official app; no tool can. Reports the version of each app and this build\'s estimate, to compare with the official one. Saving can take a while: status in_progress with a handoff_id means it is still running; do not call again, read last_handoff in get_studio_state.',
    inputSchema: noArgs,
  },
];
