# CLAUDE.md

This file provides guidance to Kscc (claudexxxxxx.ai/code) when working with code in this repository.

## Project Overview

VSCode-EmmyLua is a VSCode extension providing Lua language support (completion, diagnostics, debugging) for the Sword5/JX3 game project. It has three layers: a TypeScript VSCode extension, a C++ language server, and native debugger libraries.

## Build Commands

```bash
# Install TypeScript dependencies
npm install

# Compile TypeScript to out/
npm run compile

# Watch mode for development
npm run watch

# Download debugger binaries (required before packaging)
node ./build/prepare.js

# Package extension as .vsix
vsce package -o VSCode-EmmyLua.vsix

# Full build (all steps, Windows)
build.bat
```

### C++ Language Server Build

```bash
cd emmylua-ls/build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release

# Or on Linux/Mac:
cmake .. && cmake --build . --config Release

# Run C++ tests
./Release/emmylua-ls-test   # Windows
./emmylua-ls-test            # Linux/Mac

# Copy binary to server/ directory
copy emmylua-ls\build\Release\emmylua-ls.exe server\emmylua-ls.exe
```

C++ dependencies are auto-fetched by CMake via FetchContent: nlohmann-json v3.11.3, spdlog v1.14.1, doctest v2.4.11.

Versions for debugger binaries are pinned in `build/config.js`.

## Architecture

### TypeScript Extension (`src/`)

- `extension.ts` — Entry point: starts language server, registers debugger providers and commands. Server binary is `server/emmylua-ls.exe` (Windows) or `server/emmylua-ls` (Linux/Mac).
- `annotator.ts` — Requests `emmy/annotator` from server for color-coded decorations (params, globals, upvalues, unused vars).
- `emmyConfigWatcher.ts` — Watches `emmy.config.json` files, sends `emmy/updateConfig` to server.
- `luaContext.ts` — Extension context container holding the LanguageClient instance.
- `notifications.ts` — Interface definitions for custom notifications (`IProgressReport`, `AnnotatorType`, etc.).
- `languageConfiguration.ts` — Lua language config (brackets, comments, folding).
- `src/debugger/` — Debug adapters (completely independent of language server). Three types: `emmylua_new` (connect), `emmylua_attach` (PID), `emmylua_launch` (launch). Protocol in `EmmyDebugProto.ts`.

### C++ Language Server (`emmylua-ls/`)

Replaces the former Java-based EmmyLua-LS. Communicates via JSON-RPC 2.0 over stdio (or TCP port 5007 in dev mode).

- `src/main.cpp` — Entry point, `--stdio` / `--tcp <port>` modes
- `src/lsp/` — Transport (Content-Length framing on stdin/stdout), Dispatcher (method routing)
- `src/parser/` — Hand-written recursive descent Lua parser (5.1/5.2/5.3) + EmmyLua annotation parser
- `src/types/` — Type system (Class, Union, Array, Table, Function, Literal, Generic)
- `src/index/` — SymbolIndex (global), DocumentIndex (per-file AST cache), WorkspaceManager (file discovery, incremental updates)
- `src/config/` — Settings from `emmylua.*` namespace + `emmy.config.json` parser
- `src/util/` — URI handling, JSON helpers, logging (stderr only), platform I/O

### Custom LSP Requests (emmy/* prefix)

| Method | Direction | Purpose |
|--------|-----------|---------|
| `emmy/annotator` | C→S | Scope-aware color ranges (Param/Global/DocType/Upvalue/NotUse + hints) |
| `emmy/updateConfig` | C→S | Re-parse changed emmy.config.json |
| `emmy/reportAPI` | C→S | Inject API type docs from external tools |
| `emmy/view_syntax_tree` | C→S | Serialize AST as JSON for PSI viewer |
| `emmy/view_psi_select` | C→S | Return AST node info at position |
| `emmy/progressReport` | S→C | `{text, percent}` for indexing progress |
| `emmy/indexingDone` | S→C | Workspace indexing complete notification |

Full protocol details: `LANGUAGE_SERVER_INTERFACE.md`

### Debugger (Independent of Language Server)

The debugger module (`src/debugger/`) communicates directly with Lua processes via TCP socket — it does not go through the language server. Native binaries in `debugger/emmy/` are platform-specific shared libraries from EmmyLuaDebugger v1.4.0. The Lua-side module `debugger/Emmy.lua` is injected into target applications.

### Standard Library Definitions

`res/std/` contains Lua type definition files for standard library modules (string, table, math, io, os, etc.) using EmmyLua annotation syntax. The language server parses these at startup for built-in type info.

## Development Mode

Set `EMMY_DEV=true` environment variable. The extension connects to the language server via TCP (localhost:5007) instead of spawning it via stdio, allowing running/debugging the C++ server from an IDE.

## Extension Settings

All in `emmylua.*` namespace. Key ones: `emmylua.server.path` (custom server binary path), `emmylua.source.roots` (extra index dirs), `emmylua.completion.caseSensitive`, `emmylua.codeLens`, inspection levels for undeclared variables/fields/parameters/assignments. Full list in `package.json`.

## Design Documents

- `DESIGN.md` — C++ language server implementation plan with phased rollout
- `LANGUAGE_SERVER_INTERFACE.md` — Complete extension↔server protocol spec
