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
    description: 'Quick status of Bambu Studio: open project, unsaved changes, current plate, slicing state, whether a dialog is open, selected presets. Call this first. Works while a dialog is open; other tools return studio_busy then.',
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
];
