# Continue: ezQuake RmlUI HUD Port

Last updated: 2026-07-08

## Current Objective

Replace ezQuake's current `hud_editor`/classic HUD runtime with an embedded,
OpenGL-only RmlUI HUD. The first target is HUD only, not menus.

Official activation command for this project:

```text
hud_newhudeditor 1
```

This name is important. Use it everywhere when referring to the new system so
there is no ambiguity with the old `hud_editor` and classic HUD cvars.

The repository at:

```text
C:\Users\Negociador\Documents\newhud_ezquake
```

has intentionally been converted from the old vkQuake/RmlUI fork into an
ezQuake-based port repository.

## Important Local Paths

```text
C:\Users\Negociador\Documents\newhud_ezquake
  main repo, now ezQuake source + RmlUI port scaffold

C:\Users\Negociador\Documents\ezquake-source
  clean ezQuake clone used as source base

C:\Users\Negociador\Documents\newhud_ezquake_rmlui_backup
  backup of old vkQuake/RmlUI UI assets, docs, and src bridge

C:\Users\Negociador\Documents\qw-webhud
  clone of Xerialen/qw-webhud for protocol/layout/editor reference
```

## GitHub / Repository State

Remote:

```text
origin https://github.com/tibazera/newhud_ezquake.git
```

Branch:

```text
master
```

This repo was originally the vkQuake/RmlUI fork. It has now intentionally been
rebased in-place as an ezQuake source tree plus an OpenGL-only RmlUI HUD port
scaffold. Expect a very large diff that deletes vkQuake files and adds ezQuake
files. That is deliberate.

## What Has Been Done

- Replaced the project tree with current ezQuake source while keeping this
  repo's `.git`.
- Restored reusable RML/RCSS/font assets:
  - `ui/`
  - `ui_lab/`
- Added port planning docs:
  - `PORTING_EZQUAKE.md`
  - `EZQUAKE_HUD_EDITOR_RMLUI_BRIDGE.md`
- Added HUD registry extraction helper:
  - `tools/extract_ezquake_hud_registers.ps1`
- Added initial OpenGL-only RmlUI scaffold:
  - `src/rmlui/hud_rmlui.h`
  - `src/rmlui/hud_rmlui.cpp`
  - `src/rmlui/render_interface_gl.h`
  - `src/rmlui/render_interface_gl.cpp`
- Added CMake option:
  - `USE_RMLUI`, default `OFF`
- Added `hud_newhudeditor` cvar in the scaffold:
  - `0`: old HUD/editor path
  - `1`: new RmlUI HUD/editor path; classic HUD draw is skipped
- Hooked the scaffold into:
  - `src/hud.c`
  - `src/cl_screen.c`

## Current Code State

The scaffold is not a real RmlUI integration yet. It stores basic game state and
creates the engine hook points. `HUD_RmlUi_Render()` is still intentionally empty.

Next implementation step is to add the real RmlUI dependency and implement
`RenderInterface_GL`.

Current important files:

```text
CMakeLists.txt
  Adds USE_RMLUI option and compiles src/rmlui/*.cpp when enabled.

src/hud.c
  Registers hud_newhudeditor and skips classic HUD draw when the new path is on.

src/cl_screen.c
  Calls HUD_RmlUi_Resize, HUD_RmlUi_SyncGameState, HUD_RmlUi_Frame,
  and HUD_RmlUi_Render from the 2D/HUD frame path.

src/rmlui/hud_rmlui.h
src/rmlui/hud_rmlui.cpp
  C/C++ bridge stub and hud_newhudeditor cvar.

src/rmlui/render_interface_gl.h
src/rmlui/render_interface_gl.cpp
  Placeholder for the real Rml::RenderInterface implementation.

ui/
ui_lab/
  RML/RCSS/font assets copied from the previous vkQuake/RmlUI work.
```

Build validation note: `cmake` is not available in the current Windows PATH, and
the bundled Codex runtime does not include CMake/Ninja. A configure/build check
has not been run yet.

## Hard Requirement

This port must be fully OpenGL.

Do not port or depend on vkQuake's Vulkan RmlUI backend.
Do not introduce a browser overlay as the runtime target.
Do not use Node/Electron/OBS as the runtime architecture.

Primary renderer references in ezQuake:

```text
src/r_hud.c
src/r_draw_image.c
src/glm_draw.c
src/glc_draw.c
src/gl_program.c
src/gl_state.c
src/r_program.h
src/r_renderer_structure.h
```

## qw-webhud Findings

Xerialen/qw-webhud is useful, but as a design/reference project:

- It uses one full JSON snapshot per rendered frame.
- It keeps raw QuakeWorld values and derives HUD semantics in one shared layer.
- Its `PROTOCOL.md` is a good checklist for our `GameDataModel`.
- Its `elements.js` is a useful first element catalog.
- Its editor/spec model is useful for the future replacement of `hud_editor`.

Do not turn our target into an external browser overlay. Our target is embedded
RmlUI rendered inside ezQuake through OpenGL.

See:

```text
docs/qw-webhud-notes.md
```

Local clone:

```text
C:\Users\Negociador\Documents\qw-webhud
```

Most useful references there:

```text
PROTOCOL.md
src/public/js/qw-constants.js
src/public/js/elements.js
src/public/js/editor.js
src/public/specs/
```

Use the protocol as a GameDataModel checklist, not as a transport requirement.

## Target Architecture

The intended architecture is:

```text
ezQuake client state
  -> HUD_RmlUi_SyncGameState()
  -> C++ GameDataModel
  -> RmlUI documents in ui/rml/hud/
  -> OpenGL RenderInterface_GL
  -> ezQuake frame
```

The old `hud_editor` should eventually be replaced by an RmlUI editor UI, but
first the runtime HUD must work.

## Compatibility Strategy

Use the old HUD system as a compatibility map:

- `HUD_Register(...)` calls define canonical element names and defaults.
- `hud_*_show`, `hud_*_place`, `hud_*_align_*`, `hud_*_x`, `hud_*_y` can later
  be imported into an RmlUI layout/spec.
- The new runtime should not mutate hundreds of classic HUD cvars as its primary
  storage model.

Helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\extract_ezquake_hud_registers.ps1 -Csv
```

That script extracts the old HUD element registry from an ezQuake source tree.

## Suggested Next Steps

1. Install/provide CMake/Ninja or use a machine/toolchain where they exist.

2. Run a configure/build check with:

```powershell
cmake -S . -B build-rmlui -DUSE_RMLUI=ON -DRENDERER_MODERN_OPENGL=ON
cmake --build build-rmlui --config Debug
```

3. Fix compile errors from the C/C++ boundary.

4. Add RmlUI as a dependency:
   - likely `lib/rmlui` submodule or CMake FetchContent;
   - require Freetype;
   - keep Lua disabled.

5. Replace `RenderInterfaceGL` placeholder with a real `Rml::RenderInterface`.

6. Implement `GameDataModel` using the qw-webhud snapshot fields as the first
   binding checklist.

7. Load `ui/rml/hud/hud.rml` and show a minimal HUD:
   - health
   - armor
   - ammo
   - weapon
   - items/powerups
   - speed
   - clock/map

8. Only after the embedded HUD works, design the RmlUI replacement for the
   visual editor.

## Suggested First Real Commit After This Scaffold

The next development commit should probably be:

```text
Add embedded RmlUI dependency and OpenGL render interface
```

Expected contents:

- RmlUI library added to CMake.
- `RenderInterfaceGL` derives from `Rml::RenderInterface`.
- texture load/release works through ezQuake OpenGL texture helpers.
- compiled geometry renders simple colored/textured triangles.
- scissor works.
- `HUD_RmlUi_Render()` renders an empty or trivial RML document without crashing.

## Useful Commands

Extract old ezQuake HUD elements:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\extract_ezquake_hud_registers.ps1 -Csv
```

Inspect current changed files:

```powershell
git status --short
```

## Warnings

- The working tree intentionally has massive changes because the base was
  replaced from vkQuake to ezQuake.
- Do not revert this replacement unless the user explicitly asks.
- Do not use Vulkan as a shortcut.
- Keep `USE_RMLUI` optional until the first real RmlUI HUD works.
- The current branch may already contain one scaffold commit. Check `git log -1`
  before continuing.
