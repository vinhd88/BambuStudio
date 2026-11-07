# Agent bridge live checks (developer only)

Scripts that test the agent bridge against a **running** custom Studio build. Users and AI apps do not need any of this, or Node: AI apps connect with `BambuStudio --mcp-stdio` (see `docs/ai-agent-bambu-studio-setup-guide.md` in the parent project).

The C++ unit tests (`tests/agent_bridge/*.cpp`, target `agent_bridge_tests`) cover everything that does not need a running Studio. These scripts cover the rest: real UI-thread calls, presets, slicing, dialogs.

## Setup

Node 20+, then `npm install` in this folder once. Start the dev build on its own data folder and a **scratch copy** of a project:

```sh
build/arm64/BambuStudio/BambuStudio.app/Contents/MacOS/BambuStudio --datadir <DATA_DIR> <scratch>.3mf
```

`<DATA_DIR>` below is that same folder. Scripts that launch the MCP helper use `build/arm64/src/BambuStudio.app/Contents/MacOS/BambuStudio` unless `BAMBU_STUDIO_BINARY` (or a path argument) says otherwise.

## Scripts

| Script | Changes the project? | What it checks |
|---|---|---|
| `live-studio-check.mjs` | no | Read tools through `--mcp-stdio`; sizes, timings, discovery file mode |
| `mcp-stdio-live-check.mjs` | no | Each read tool via `--mcp-stdio` equals a direct bridge call |
| `apply-and-slice-live-check.mjs` | yes, reverted | Settings changes at each level, live UI, optional slice (`--slice`) |
| `filament-revert-handoff-live-check.mjs` | yes, reverted | Filament/process presets, object filament, revert order, skip; `--handoff` opens a sliced copy in the official Studio |

Run any of them with `BAMBU_AGENT_DATA_DIR=<DATA_DIR> node <script>`. Close dialogs in Studio first; scripts that change things need spiral mode off and no unsaved edits.

`npm test` runs `bridge-client.test.js`, a unit test of the small client these scripts use (no Studio needed).
