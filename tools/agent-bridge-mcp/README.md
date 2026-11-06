# agent-bridge-mcp

Stdio MCP server that lets coding agents (Claude Code, Codex, Gemini CLI) read and change the project open in the custom Bambu Studio build, live, without screenshots.

```
agent -> stdio MCP (this folder) -> HTTP 127.0.0.1 + token -> agent bridge in Studio -> Studio UI
```

Studio writes `agent-bridge.json` (port, token, pid) into its data folder at startup. The adapter reads that file on every call, so Studio can be restarted while the agent stays connected.

## Requirements

- Node 20 or newer; run `npm install` here once
- Custom build running with its own data folder:
  ```sh
  build/arm64/BambuStudio/BambuStudio.app/Contents/MacOS/BambuStudio --datadir <DATA_DIR>
  ```
- Official `/Applications/BambuStudio.app`, for printing (`open_in_official_studio`)
- `BAMBU_AGENT_DATA_DIR` set to the same `<DATA_DIR>` in the client config

Below, `<REPO>` is this repository's root and `<DATA_DIR>` is the `--datadir` Studio runs with.

## Client setup

### Claude Code

```sh
claude mcp add bambu-studio --scope user -e BAMBU_AGENT_DATA_DIR=<DATA_DIR> -- node <REPO>/tools/agent-bridge-mcp/server.js
```

Check with `claude mcp list`: `bambu-studio` should show as connected.

### Codex

```sh
codex mcp add bambu-studio --env BAMBU_AGENT_DATA_DIR=<DATA_DIR> -- node <REPO>/tools/agent-bridge-mcp/server.js
```

or add to `~/.codex/config.toml`:

```toml
[mcp_servers.bambu-studio]
command = "node"
args = ["<REPO>/tools/agent-bridge-mcp/server.js"]
env = { BAMBU_AGENT_DATA_DIR = "<DATA_DIR>" }
```

`codex exec` waits on stdin; run it with `</dev/null` in scripts.

### Gemini CLI

Untested. Add to `~/.gemini/settings.json`:

```json
{
  "mcpServers": {
    "bambu-studio": {
      "command": "node",
      "args": ["<REPO>/tools/agent-bridge-mcp/server.js"],
      "env": { "BAMBU_AGENT_DATA_DIR": "<DATA_DIR>" }
    }
  }
}
```

## Tools

| Tool | Does |
|---|---|
| `get_studio_state` | Open project, unsaved state, plate, slicing, open dialog, presets, last change. Call first |
| `get_project` | Plates, objects, parts, height ranges, filament slots |
| `get_settings`, `describe_settings` | Values at a level; what settings mean |
| `list_presets`, `list_filaments`, `get_filament` | Presets and filaments |
| `apply_settings` | Change settings at global, plate, object, part or height-range level |
| `select_filament` | Filament preset for a slot |
| `select_process_preset` | Process preset; `keep_changes` carries unsaved edits over |
| `set_object_filament` | Slot an object or part prints with |
| `revert_change` | Undo the newest agent change; refuses if the user changed it since |
| `slice_plate`, `get_job`, `get_slice_result` | Slice and read time, filament, per-feature time |
| `open_in_official_studio` | Copy with G-code, opened in the official app for printing |

No tool saves the user's project, starts a print or controls a printer. G-code, printer settings and machine limits are read-only.

## Agent instructions

Paste into the agent's instructions (CLAUDE.md, AGENTS.md, GEMINI.md):

```markdown
## Bambu Studio (MCP server bambu-studio)
- Start with get_studio_state, then get_project. Read before you change: get_settings with a category keeps replies small.
- Change with apply_settings, select_filament, select_process_preset or set_object_filament. Each returns a change_id.
- A status of rejected or refused means nothing changed: read the reason; never work around it. unsaved_changes means the user must save or discard edits in Studio.
- waiting_for_user means Studio opened a dialog: ask the user to answer it, then read last_change from get_studio_state.
- After changing anything, slice_plate and poll get_job until done. Compare time_s and features with the result before the change.
- Report what you measured (slice results) separately from what you judged (quality, strength, looks). Slice time does not show the quality cost of less cooling or faster walls.
- To undo, call revert_change repeatedly; it goes newest first.
- To print: slice, then open_in_official_studio. The user starts the print in the official app.
```

## Tests

```sh
npm test                                                                 # adapter, Studio closed
BAMBU_AGENT_DATA_DIR=<DATA_DIR> node e2e/live-studio-check.mjs           # read tools, Studio running
BAMBU_AGENT_DATA_DIR=<DATA_DIR> node e2e/apply-and-slice-live-check.mjs  # changes; scratch copy of a project only
BAMBU_AGENT_DATA_DIR=<DATA_DIR> node e2e/filament-revert-handoff-live-check.mjs  # phase 4 tools; scratch copy only
```
