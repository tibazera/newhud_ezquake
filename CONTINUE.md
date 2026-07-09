# Continue: ezQuake RmlUI HUD Port

Last updated: 2026-07-09

## Current Objective

Replace ezQuake's current `hud_editor`/classic HUD runtime with an embedded,
OpenGL-only RmlUI HUD. The first target is HUD only, not menus.

Official activation command for this project:

```text
hud_newhudeditor 1
```

This name is important. Use it everywhere when referring to the new system so
there is no ambiguity with the old `hud_editor` and classic HUD cvars.

## Status Summary (2026-07-09)

The CONTINUE.md roadmap **steps 1-7 are done and VISUALLY CONFIRMED ON SCREEN**
(2026-07-09). The client builds, links, and renders a live RmlUI HUD inside
ezQuake.

- `USE_RMLUI=ON` builds `ezquake.exe` with RmlUi 6.2 statically linked.
- Verified on the user's machine (E:\trabalho\quake, map "Introduction"): the
  minimal document draws bottom-left with a rounded translucent panel, correct
  premultiplied-alpha blending, LatoLatin text, and LIVE data - the RmlUI panel
  showed "HP 100 / AM 22" matching the classic HUD's independent 100/22,
  proving SyncGameState -> "hud" data model -> document binding end to end.
- KNOWN COSMETIC ISSUE: the classic/new HUD still draws underneath. Our
  `hud_newhudeditor 1` only early-returns from `HUD_Draw()` (sbar path) in
  `hud.c`; the bottom HUD elements come from `SCR_DrawNewHudElements()` /
  `SCR_DrawElements()` in `cl_screen.c`, which we do not gate yet. Gate those on
  `!HUD_RmlUi_ShouldDrawClassicHud()` to show the new HUD alone.

## Important Local Paths

NOTE: the original CONTINUE.md was written for a different machine
(`C:\Users\Negociador\Documents\...`) and referenced neighbour clones
(`ezquake-source`, `qw-webhud`, an rmlui backup) that are NOT present in this
checkout. Current reality:

```text
C:\Users\Felipe\OneDrive\Desktop\qw_tiba
  main repo (this checkout): ezQuake source + working RmlUI OpenGL HUD.

C:\Users\Felipe\OneDrive\Desktop\qw_tiba\aiox-core
  UNRELATED project accidentally cloned inside the working copy. Git-ignored
  (/aiox-core/ in .gitignore). Do not commit it.
```

The `qw-webhud` reference clone is not present here; use its public repo
(Xerialen/qw-webhud) if needed as a GameDataModel checklist.

## GitHub / Repository State

Remote:

```text
origin https://github.com/tibazera/newhud_ezquake.git
```

Branches:

```text
master                      base (scaffold commit 66444c70)
feat/rmlui-opengl-build      ACTIVE - all port work below lives here, unpushed
```

Submodules: the vkQuake->ezQuake conversion lost the submodule gitlinks. They
were reconstructed as plain clones for the build and are currently UNTRACKED
(not committed): `src/qwprot` (QW-Group/qwprot) and `vcpkg`
(microsoft/vcpkg @ tag 2026.06.24). Restoring proper submodule references is a
separate follow-up.

## Build Instructions (Windows / MSVC) - WORKING

Toolchain used (installed via winget on a previously bare machine): CMake,
Ninja, VS Build Tools 2022 (MSVC 14.44).

The `msbuild-x64` preset does NOT work (it pins generator "Visual Studio 18
2026"). Use `msvc-x64` (Ninja Multi-Config) from a shell with MSVC env loaded:

```bat
:: from a Developer prompt, or after calling vcvars64.bat:
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\Program Files\CMake\bin;%PATH%"

cmake --preset msvc-x64 -DUSE_RMLUI=ON
cmake --build build-msvc-x64 --config Release
:: -> build-msvc-x64/Release/ezquake.exe
```

First configure builds all vcpkg dependencies (SDL2, curl, freetype, rmlui,
etc.) - ~13 min once, then binary-cached. If submodules are missing, recreate
them first:

```bat
git clone https://github.com/QW-Group/qwprot.git src/qwprot
git clone --branch 2026.06.24 --depth 1 https://github.com/microsoft/vcpkg.git vcpkg
vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

## How To Test On Screen (for the user)

1. Copy the repo `ui/` tree into your Quake game directory (next to where
   `ezquake.exe` runs). Fonts/RML are loaded via RmlUi's default file interface,
   resolved relative to the working directory.
2. Run `build-msvc-x64/Release/ezquake.exe`.
3. In the console: `hud_newhudeditor 1` -> a panel should appear (bottom-left)
   showing live HP / AR / AM / map from the game state.
4. Iterate on the layout: edit `ui/rml/hud/minimal.rml`, then run
   `hud_newhudeditor_reload` to reload without restarting.
5. Diagnostics are printed to the console (`RmlUI HUD: ...`, `RmlUI GL: ...`).

## Progress Log

### 2026-07-09 - VISUAL VERIFICATION PASSED

Ran on the user's Quake install (E:\trabalho\quake, exe + ui\ copied there,
run-newhud.bat -> +map start +hud_newhudeditor 1). The RmlUI HUD rendered
correctly on the "Introduction" map: bottom-left translucent rounded panel,
crisp LatoLatin text, live "HP 100 / AM 22" and map/time. No shader/GL/coord
issues surfaced. Only open item: classic HUD still visible underneath (separate
draw path, see Status Summary).

### 2026-07-08/09 - build bootstrap + roadmap steps 1-7 (branch feat/rmlui-opengl-build)

Steps 1-3 - make it build (commit `367ffc50`):
- Installed the whole toolchain (CMake/Ninja/MSVC) on a bare machine.
- Reconstructed the lost `src/qwprot` and `vcpkg` submodules.
- Fixed the C/C++ boundary so the engine headers compile as C++ for the first
  time (the rmlui `.cpp` files are the first C++ TUs in the tree):
  - `src/q_shared.h`: `typedef enum {false,true} qbool` is illegal in C++;
    guard with `__cplusplus` -> `typedef int qbool` (int, to preserve the enum
    ABI in structs shared across the boundary).
  - `src/cl_screen.c`: RmlUI frame hook used non-existent `host_frametime` ->
    `cls.frametime`.
  - `CMakeLists.txt`: newer vcpkg minizip port exports the include root while
    `fs.h` includes `"unzip.h"` -> resolve the minizip include dir in vcpkg
    mode too; add `src/` to the include path for the rmlui TU.

Step 4 - RmlUI dependency (commit `45e7ca50`):
- `vcpkg.json`: added `rmlui` (6.2; Lua disabled, freetype pulled in).
- `CMakeLists.txt`: `find_package(RmlUi CONFIG)` + link `RmlUi::RmlUi` behind
  `USE_RMLUI`.
- `render_interface_gl.*`: derives from `Rml::RenderInterface` with safe stubs.
- `hud_rmlui.cpp`: real RmlUi lifecycle (SystemInterface via `Sys_DoubleTime` +
  `Com_Printf` logging, `Rml::Initialise`, lazy "hud" context, Update/Render).

Step 5 - real OpenGL render interface (commit `ddb86a97`):
- `render_interface_gl.cpp`: self-contained modern-GL renderer. Loads GL 2.0+/
  VAO entry points via `SDL_GL_GetProcAddress` (the engine's GL pointers are
  static-per-file and not reusable); own shader, per-geometry VAO/VBO/IBO,
  textures via `glTexImage2D`, premultiplied-alpha blend
  `(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`, scissor with Y-flip, and full GL state
  save/restore around each frame (`BeginFrame`/`EndFrame`).
- `LoadTexture` (external image files) is still a documented stub; fonts and
  solid colours arrive through `GenerateTexture`.

Step 6 - GameDataModel (commit `9b041a61`):
- `hud_rmlui.cpp`: binds game state to the "hud" data model - `health`, `armor`,
  `ammo`, `weapon`, `items`, `intermission`, `gametype`, `maxclients`, `time`,
  `map`. Marked dirty each frame before `Context::Update()`.

Step 7 - fonts + document (commit `c093afa4`):
- Loads LatoLatin font faces at init.
- `ui/rml/hud/minimal.rml`: self-contained proof-of-life document bound to
  `data-model="hud"`, showing live HP/AR/AM/map with a low-health style.
- `hud_newhudeditor_doc` cvar (document path) + `hud_newhudeditor_reload`
  command for live iteration.

## Current Code State

Real integration. Key files:

```text
CMakeLists.txt
  USE_RMLUI: find_package(RmlUi), links RmlUi::RmlUi, adds src/ include for the
  rmlui TU, and fixes the vcpkg minizip include dir.

vcpkg.json
  Declares the rmlui dependency.

src/hud.c
  Registers hud_newhudeditor, calls HUD_RmlUi_Init(), skips classic HUD draw
  when the new path is on. (unchanged this session)

src/cl_screen.c
  Calls HUD_RmlUi_Resize / SyncGameState / Frame / Render from the 2D/HUD frame
  path. (frametime fix)

src/rmlui/hud_rmlui.{h,cpp}
  C/C++ bridge: RmlUi lifecycle, SystemInterface, "hud" GameDataModel, font +
  document loading, hud_newhudeditor / hud_newhudeditor_doc cvars and the
  hud_newhudeditor_reload command.

src/rmlui/render_interface_gl.{h,cpp}
  Real Rml::RenderInterface implementation over raw modern OpenGL.

ui/rml/hud/minimal.rml
  Minimal HUD document wired to the current data model. (The legacy hud.rml uses
  data-model="game" with many not-yet-synced fields and does not match the
  current model - future work.)

ui/, ui_lab/
  RML/RCSS/font assets from the previous vkQuake/RmlUI work.
```

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

Engine APIs useful for deeper integration (from the renderer survey):
- Textures from RGBA memory: `R_LoadTexture(id,w,h,data,mode,bpp)` /
  `R_LoadTexturePixels(...)`, `R_DeleteTexture(&ref)`, `renderer.TextureUnitBind`.
  (`texture_ref` = `{ unsigned int index; }`, `R_TextureReferenceIsValid`.)
- Scissor with correct Y-flip/console scale: `Draw_EnableScissorRectangle`,
  `Draw_DisableScissor`.
- 2D state: `R_Set2D()`, `R_OrthographicProjection(...)`, `R_Cache2DMatrix()`.
- The engine's modern-GL function pointers are `static` per-file and NOT
  reusable from a new TU; new GL code must load its own pointers via
  `SDL_GL_GetProcAddress` (this is what render_interface_gl.cpp does).

## Target Architecture

```text
ezQuake client state
  -> HUD_RmlUi_SyncGameState()
  -> C++ GameDataModel ("hud")
  -> RmlUI documents in ui/rml/hud/
  -> OpenGL RenderInterfaceGL
  -> ezQuake frame
```

Implemented and verified end to end on screen (2026-07-09). The old `hud_editor`
should eventually be replaced by an RmlUI editor UI.

## qw-webhud Findings

Xerialen/qw-webhud is useful as a design/reference project (not a transport):
its `PROTOCOL.md` is a good GameDataModel checklist and its `elements.js` a
first element catalog. Do not turn the target into an external browser overlay.
See `docs/qw-webhud-notes.md`.

## Compatibility Strategy

Use the old HUD system as a compatibility map:
- `HUD_Register(...)` calls define canonical element names and defaults.
- `hud_*_show/_place/_align_*/_x/_y` can later be imported into an RmlUI spec.
- The new runtime should not mutate hundreds of classic HUD cvars as its primary
  storage model.

Helper: `powershell -ExecutionPolicy Bypass -File .\tools\extract_ezquake_hud_registers.ps1 -Csv`

## Suggested Next Steps

1. DONE (2026-07-09): visual verification passed - see Progress Log.

2. Gate the classic/new HUD on `!HUD_RmlUi_ShouldDrawClassicHud()` in
   `cl_screen.c` (`SCR_DrawNewHudElements` / `SCR_DrawElements`) so the new HUD
   can be shown alone. Currently both draw simultaneously.

4. FileInterface over ezQuake's VFS (`FS_OpenVFS` / `VFS_READ` / ...), so fonts
   and RML load from the game filesystem/paks instead of cwd-relative paths.

5. Expand the GameDataModel to the full field set the legacy `ui/rml/hud/hud.rml`
   expects (armor_type, per-type ammo shells/nails/rockets/cells, weapon
   ownership flags, notify lines, level stats, speed, clock). Use qw-webhud
   PROTOCOL.md as the checklist. Then switch the default document to hud.rml.

6. Wire `RenderInterfaceGL::LoadTexture` for external image files (via the VFS
   file interface), for RML `<img>`/`decorator: image(...)`.

7. Input routing / editor: only after the runtime HUD is solid, design the RmlUI
   replacement for the visual `hud_editor`.

## Warnings

- The working tree intentionally has massive changes because the base was
  replaced from vkQuake to ezQuake. Do not revert this replacement.
- Do not use Vulkan as a shortcut.
- `/aiox-core/` inside the working copy is an unrelated project - git-ignored,
  never commit it.
- Port work is on `feat/rmlui-opengl-build`, unpushed. `master` still points at
  the scaffold commit. Check `git log --oneline -8` before continuing.
- `src/qwprot` and `vcpkg` are untracked reconstructed submodules - do not
  `git add` them as plain directories; restore proper submodule links instead.
```
