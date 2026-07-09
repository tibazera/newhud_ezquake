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

### 2026-07-09 - DRAG EDITOR shipped (hud_newhudeditor 2, PENDING acceptance)

Commit `dbe045e3`. The drag-and-drop editor, pulled forward by user
request. Reuses the classic hud_editor input plumbing (key_dest =
key_hudeditor -> mouse tracking/cursor/game-block for free): keys.c's
key_hudeditor dispatch sites route to the RmlUI bridge when
HUD_RmlUi_InEditorMode(). Mouse1 grabs the top-level element under the
cursor (frozen at its absolute position, anchors neutralised), drag pins
left/top, drop saves. Layout persists in rmlui_hud_layout.cfg (game dir,
"id x y" lines) and is applied on every document load. ESC exits to mode
1. hud.rml shows outlines + an instruction banner in edit mode.

How to use: `hud_newhudeditor_edit` in the console (needs the HUD live) -
drag the blocks - ESC. To reset a layout, delete rmlui_hud_layout.cfg.

Known limits (next iterations): no snapping/grid, no keyboard nudge, no
per-element hide toggle, scoreboard/centerprint drag while invisible not
possible (they only exist when shown).

### 2026-07-09 - M3.5 v3: faithful classic sbar (PENDING acceptance)

User: "o HUD tá pior que o original". Fix round (commit `9efecf00`):
GL_NEAREST on LoadTexture (pixel art was blurred by linear filtering) and
hud.rml v3 recreating the REAL sbar/ibar layout with the original LCD
digit lumps (num_/anum_ via derived digit fields) instead of modern boxes
with font numbers. run-newhud.bat now launches fullscreen.
Next feature (already agreed): the drag editor.

### 2026-07-09 - COURSE CORRECTION from user feedback + M3.5 (earlier round)

User rejected the text-first M3 look ("só texto, sem imagem... não to
gostando") and asked about click-and-drag editing. Agreed replan:
1. M3.5 NOW: classic-icon HUD - 162 original sbar graphics extracted from
   the user's PAK0.PAK gfx.wad into ui/rml/hud/icons/ (offline Node
   extractor: PACK -> WAD2 qpic lumps -> 32-bit TGA, palette + index-255
   transparency). hud.rml v2 is icon-driven (inv/inv2 weapon pics, ammo
   boxes, armor icons, health faces via derived face_icon, powerups/keys/
   sigils). Model gained face_icon/ammo_icon/sigil1-4.
2. NEXT: the drag editor (old M6) is PULLED FORWARD - input routing,
   click-and-drag of HUD elements, layout persistence - before M4/M5.
   For the user, "editable" means dragging on screen; treat it as core.

M3.5 acceptance: icons visible everywhere (weapon bar with real pics,
lit when active; face changes with health/powerups; ammo boxes; armor
icon by type; powerup/key icons), plus everything that already passed.

### 2026-07-09 - M3 real HUD document (superseded by M3.5 above)

Commit `592155ac`. Design decision (user): modern text-first look.
- LoadTexture over R_LoadImagePixels (VFS: tga/png/jpg from paks),
  premultiplied upload - RML <img>/decorators now work for any shipped
  image.
- Centerprint push hooks in hud_centerprint.c (SCR_CenterPrint funnel);
  notify lines polled from con.text/con_times with classic timing;
  showscores/showteamscores bound.
- New text-first ui/rml/hud/hud.rml (replaces legacy vkQuake-era doc):
  trio armor/health/ammo with type colours, 8-slot weapon bar, per-type
  ammo, frags, clock, fps/ping, speed, powerups/keys, centerprint,
  notify, +showscores scoreboard from players[].
- Default document is now hud.rml (hud_newhudeditor_doc).
- Parity reference: docs/hud-element-registry.csv (84 elements; radar/
  tracker/itemsclock/TP-teaminfo/groups deliberately post-M3).

M3 acceptance test (user): run with hud_newhudeditor 1 -
(a) full layout visible (trio bottom-center, weapon bar, ammo block
bottom-right, clock top-right, speed bottom-left);
(b) weapon bar highlights the active weapon and lights up owned ones;
(c) centerprint messages appear centered and expire (~2s);
(d) console/game messages appear top-left and fade after ~3s;
(e) TAB (+showscores) shows the scoreboard with map name and players.

### 2026-07-09 - M2 ACCEPTED by the user on screen ("TUDO CERTO")

Weapon labels/ammo switching, per-type ammo, speed, fps, clock, armor
colours and powerups all verified live. Next: M3 (real HUD documents).

### 2026-07-09 - M2 GameDataModel complete (accepted above)

Commits `eaa2a611` (FILEVERSION fix) + M2 feature commit. The "hud" data
model now carries the full contract in src/rmlui/game_data_model.*:
me-fields (health/armor/armor_type/weapons/has_*/per-type ammo/powerups/
keys/name/team/frags/ping/pl/speed), match (map short+title, MM:SS clock
with standby/countdown semantics, gametype/teamplay/limits/intermission/
paused), client (fps/spectator/demo/mvd) and a players[] array for
data-for scoreboards. Reads engine globals directly (HUD_Stats, cl.simvel,
cl.players, cls.fps, host_mapname per the field survey); per-variable
dirty tracking against a snapshot (no DirtyAllVariables). The C hook is
now HUD_RmlUi_SyncGameState(void). minimal.rml shows the expanded fields.
Deferral (deliberate): TP teaminfo -> M3 with its documents.

Build fix discovered: FILEVERSION in ezQuake.rc got the git hash as a
component (no upstream tags here); hashes matching <digits>e<digits>
even crash RC. Components are now sanitized to integers in CMakeLists.

NOTE: ui/CLAUDE.md is legacy documentation from the old vkQuake bridge -
its commands (ui_reload/ui_debugger) and the "game"/"cvars" models do NOT
exist in this port (ours: "hud" model, hud_newhudeditor_reload). Its RCSS
constraint notes (rgba alpha 0-255 etc.) remain useful. The legacy
hud.rml expects data-model="game"; M3 will reconcile naming.

M2 acceptance test (user, on screen): run a map with hud_newhudeditor 1 -
the panel must show weapon label + ammo switching as you change weapons
(keys 1-8), per-type ammo counts, speed changing as you move, fps, the
match clock ticking, armor coloured green/yellow/red by type, and QUAD/
PENT/RING appearing when picked up.

### 2026-07-09 - M1 ACCEPTED by the user on screen

After the GL state-cache fix: fullscreen works, hud_newhudeditor 0<->1
toggles correctly (classic HUD returns/disappears), no black screen, no
crash. M1 acceptance criteria met. Next: M2 (complete GameDataModel).

### 2026-07-09 - M1 foundation hardening (code complete, accepted above)

Commits `df353bbe` (code) + submodule gitlinks commit. All five M1 items:
1. vid_restart survival: HUD_RmlUi_VidShutdown() hooked in VID_Shutdown()
   before R_Shutdown - releases RmlUi compiled geometry/textures and the
   render interface's GL objects (new OnContextLost()) while the old context
   is current; GL entry points reload lazily against the new context;
   contexts/documents survive the restart.
2. Full-quit shutdown wired through the same hook (restart==false ->
   HUD_RmlUi_Shutdown(), reordered: Rml::Shutdown first while GL is alive).
3. FileInterfaceVFS (src/rmlui/file_interface_vfs.*): RML/RCSS/fonts load
   through the quake VFS (game dirs + paks, FS_ANY) with stdio fallback for
   loose cwd files. Gotchas: include RmlUi/STL BEFORE engine headers
   (q_shared macros poison <algorithm>); do not name anything OpenFile
   (winbase.h clash).
4. Classic HUD gated: both Sbar_Draw call sites + SCR_DrawNewHudElements
   skip when the RmlUI HUD is active; crosshair/pause/net/teaminfo/
   centerprint/intermission intentionally keep drawing.
5. Submodule gitlinks restored (src/qwprot @ master, vcpkg @ 2026.06.24).

M1 fix round 1 (commit `3ba4f86f`): user reported all-black screen after
maximizing. Root cause class: ezQuake caches GL state (gl_state.c,
opengl.rendering_state) and skips redundant GL calls; our pass changed raw
state the cache did not know about. EndFrame restored enables/bindings but
NOT the blend function (premultiplied), scissor rect or unpack alignment -
now all saved/restored exactly (glBlendFuncSeparate). White-texture creation
also restores the previously bound texture. LESSON for all future GL work
here: every raw GL change must be restored EXACTLY, or the engine's state
cache desyncs and skips its own re-sets.

M1 acceptance test (user, on screen): with hud_newhudeditor 1 -
(a) classic bottom status bar must be GONE (only the RmlUI panel);
(b) vid_restart in the console -> no crash, HUD comes back;
(c) toggle fullscreen/resolution -> no crash, HUD correct;
(d) quit cleanly. ui/ can now also live inside the gamedir (e.g. id1/ui/)
or a pak, not only next to the exe.

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

## Milestone Roadmap (agreed 2026-07-09)

Goal restated: the proposal is only "done" when the RmlUI HUD can genuinely
replace the classic HUD in real play, and later the editor. Quality-first: each
milestone has acceptance criteria and gets on-screen verification by the user
before moving on. No shortcuts.

### M1 - Harden the foundation (robustness, not features)
1. Survive `vid_restart`/GL context recreation: RenderInterfaceGL must
   invalidate and recreate its shader/VAOs/textures (currently they become
   dead handles -> crash/corruption risk). Most serious latent bug.
2. Wire `HUD_RmlUi_Shutdown()` into the engine shutdown path (Host_Shutdown).
3. RmlUi FileInterface over ezQuake's VFS (`FS_OpenVFS`/`VFS_READ`/...), so
   RML/RCSS/fonts load from the game filesystem/paks instead of cwd-relative.
4. Gate `SCR_DrawNewHudElements`/`SCR_DrawElements` in `cl_screen.c` on
   `!HUD_RmlUi_ShouldDrawClassicHud()` (carefully - console/menus stay).
5. Repo hygiene: restore proper submodule gitlinks for src/qwprot and vcpkg.
Accept: resolution/fullscreen switch with HUD on = no crash; ui/ loads from
game dir; `hud_newhudeditor 1` shows ONLY the new HUD.

### M2 - Complete GameDataModel (the data contract)
Full field set using qw-webhud PROTOCOL.md as the checklist: armor_type,
per-type ammo (shells/nails/rockets/cells), owned-weapons bitmask + has_*
flags, active weapon with derived label, items/powerups (quad/pent/ring/suit/
keys/sigils), match state (countdown/standby/intermission, formatted time,
gametype), player (name/team/frags), scoreboard player array, teaminfo, speed,
clock, ping/packetloss/fps. One derived-semantics layer (raw + derived like
weapon_label/armor_class). Correct dirty tracking (only what changed - not
DirtyAllVariables every frame).
Accept: every field the legacy hud.rml expects exists and updates in real play.

### M3 - Real HUD documents
`LoadTexture` via the VFS file interface (icons/images for <img>/decorators);
asset-strategy decision (classic pak graphics vs. own assets in ui/); rebuild
`ui/rml/hud/hud.rml` against the M2 model with element-by-element parity vs.
the classic HUD_Register registry (tools/extract_ezquake_hud_registers.ps1);
notify/centerprint documents.
Accept: play a full match with only the new HUD and miss nothing.

### M4 - Real-play validation + performance
Multiplayer, MVD/QTV demo playback, spectator, multiview. Frame-cost
measurement (confirm compiled geometry is cached across frames). Edge cases:
conwidth/console scaling, DPI, alt-tab, in-game resolution changes.
Accept: no perceptible FPS regression; stable in all play modes.

### M5 - Config integration
Cvar persistence in configs, layout/document selection, sane defaults,
cfg_save compatibility.
Accept: settings survive client restart and cfg_save/cfg_load.

### M6 - The editor (only after M1-M4 are solid)
Input routing (mouse/keyboard to RmlUI in editor mode, preserving
Escape/console semantics); RmlUI editing UI (select/drag elements, property
panel, save layout spec); importer for classic hud_* cvars.
Accept: the old hud_editor can be retired.

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
