# Continue: ezQuake RmlUI HUD Port

Last updated: 2026-07-10

## Current Objective

Replace ezQuake's current `hud_editor`/classic HUD runtime with an embedded,
OpenGL-only RmlUI HUD. The first target is HUD only, not menus.

Official activation command for this project:

```text
hud_newhudeditor 1
```

This name is important. Use it everywhere when referring to the new system so
there is no ambiguity with the old `hud_editor` and classic HUD cvars.

## Status Summary (atualizado 2026-07-09, tarde)

Roadmap 1-7 concluído e o sistema evoluiu MUITO além disso. Estado atual,
CONFIRMADO EM JOGO pelo usuário ("tá tudo funcionando agora, parabéns"):

- `USE_RMLUI=ON` builda `ezquake.exe` (RmlUi 6.2 estático). HUD RmlUi renderiza
  ao vivo em partida, no espaço conwidth/conheight (bate com o cursor do mouse).
- **HUD completo** com ícones clássicos extraídos dos paks (162 TGA em
  `id1/ui/rml/hud/icons/`). Assets DEVEM ficar dentro do gamedir id1 (ver seção
  do fix de ícones) — a raiz não é procurada pelo `R_LoadImagePixels`.
- **Dois layouts selecionáveis:** `hud.rml` (Clássico: rosto + dígitos LCD) e
  `hud_print.rml` (Competitivo: números grandes coloridos, baseado no
  printquake.png). Trocáveis pelo style picker.
- **Style picker** (`hud_newhudeditor_style`): seção LAYOUT + CONJUNTO DE ÍCONES;
  navegação por setas + Enter (preview ao vivo), ou mouse; centralizado com
  `left:50%`+margin negativa. Cursor visível (fix de ordem de desenho).
- **Editor** (`hud_newhudeditor_edit`): arrasta cada widget; painel liga/desliga
  elementos; layout salvo POR DOCUMENTO em frações resolução-independentes.
- **`hud_newhudeditor_reset`**: recupera para os padrões (limpa layout + mostra tudo).
- **Kill feed editável** (w_deaths) alimentado pelo tracker; rosto e barra de
  armas presentes nos dois layouts.
- Cvars persistem via cfg_save (CVAR_GROUP_HUD): `hud_newhudeditor_doc`,
  `hud_newhudeditor_iconset`.

Histórico detalhado de cada fix está nas seções datadas no fim do arquivo.
PRÓXIMO: refinar composição do Competitivo vs printquake.png e o formato do kill
feed — precisa de screenshot do usuário (HUD só renderiza em jogo, ver Warnings).

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

### 2026-07-09 - CRITICAL FIX: render in conwidth space (PENDING acceptance)

Commit `39816d7d`. User loaded their real cfg (vid_conwidth 640 /
vid_conheight 360) and the HUD went "extremely misconfigured, nothing to
drag". Root cause: ezQuake's whole 2D layer AND the mouse cursor
(scr_pointer_state, scaled to conwidth in SCR_UpdateCursor) live in
conwidth/conheight space, but the HUD context was sized in real
framebuffer px (vid.width/height). Fix: feed HUD_RmlUi_Resize
vid.conwidth/vid.conheight; set context DensityIndependentPixelRatio =
conheight/480; render interface scales scissor rects from context space
to framebuffer px (scale_x/y from GL viewport); hud.rml rewritten
resolution-independent (%, dp, transform centering; drag clears the
transform). Lesson: anything sharing input/coords with ezQuake's 2D must
use conwidth/conheight, never vid.width/height. NOTE: exe must be
recopied to E:\trabalho\quake (was locked by the running game).

### 2026-07-09 - Split face/life/clock/fps into separate widgets (earlier)

Commit `d38daf8f`. User (with the classic-HUD reference screenshot):
separate armor/face/life/ammo and clock/fps. w_face split from w_health,
w_clock split from w_fps; both added to the config list. armor/ammo were
already separate. Reference composition confirmed = classic overlays
(kill feed/teamscores/teamoverlay/centerprint, now drawing) + the RmlUI
bottom sbar. Weapon bar and ammo counts kept as groups (user only asked
to split the four sbar stats and clock/fps).

### 2026-07-09 - Reverted to CLASSIC scoreboard/overlay (earlier)

Commit `3f1ec758`. User: the original ezQuake scoreboard is much better;
keep it, refine from there. Map (agent): TAB scoreboard lives inside
Sbar_Draw() (Sbar_DeathmatchOverlay/Sbar_TeamOverlay, sbar.c), which the
gate suppressed; but with +showscores held Sbar_Draw() skips its own
bottom bar. Fix: allow Sbar_Draw() through the gate when
sb_showscores||sb_showteamscores (cl_screen.c:838). Classic team overlay
(SCR_Draw_TeamInfo/scr_teaminfo) and kill feed (VX_TrackerThink/r_tracker)
already draw (never gated). Removed the custom RML w_scores. Next:
awaiting the user's reference-HUD screenshot to map the full competitive
composition; open question is widget granularity (split weapon bar / ammo
counts into individual draggables, or keep as groups).

### 2026-07-09 - Scoreboard redesign + edit config panel (superseded)

Commit `b8add5c4`. Scoreboard v3: header band (map title/short/clock),
column header, team-colour accent bar + frags coloured by player colour,
roomy columns, dimmed spectators. Edit-mode config panel (#configpanel):
clickable show/hide for every widget (model widgets[] + toggle/hidden
accessors, .whidden class, persisted as "hide <id>" lines in the layout
file). Answers "options for where death messages appear": w_notify
("Mensagens (mortes/chat)") is toggleable + draggable. Config panel uses
the same edit-mode input routing; clicks on it (a non-widget) fire the
listener without starting a drag.

### 2026-07-09 - Feedback: ESC fix, picker fits, per-element widgets (earlier)

Commit `3f73e776`. (1) ESC now exits the style picker (its key handler
was gated on edit-mode only; picker runs at mode 1). (2) Picker cards
shrunk + wrap, panel max-height/overflow so it no longer escapes the
screen. (3) hud.rml restructured: every element is an independent
top-level widget id "w_*"; the drag editor + layout persistence only
touch "w_*" ids, so each element drags/saves separately. Old
rmlui_hud_layout.cfg (pre-rename ids) is stale-safe (skipped) and was
cleared from the test dir.

### 2026-07-09 - VISUAL STYLE PICKER with live preview (earlier)

Commit `d6fa4048`. In-game gallery replacing console cvar juggling:
`hud_newhudeditor_style` opens cards for every installed iconset (scanned
from <basedir>/{id1,qw}/hudpacks + Classic), each showing real samples.
HOVER previews the whole HUD live with that set (iconpath binding
re-resolves every image instantly, no reload); CLICK applies the cvar;
ESC/Fechar closes. Active set marked. Input reuses the key_hudeditor
routing; drag suppressed while picker open.

### 2026-07-09 - HUD packs downloaded & installed for the user (earlier)

Commit `343f7c4d` (fallback). Downloaded the 4 requested packs from
gfx.quakeworld.nu, extracted (incl. foogs' pk3) and installed them
normalized (flat, lowercase) into E:\trabalho\quake\ID1\hudpacks\
{bugs5 24f, gnoffa 71f, foogs 85f, starjedi 25f}. LoadTexture gained an
iconset fallback: lumps missing from the active set fall back to the
classic extracted icons, so numbers-only packs work without holes.
Third-party art stays in the user's game dir only (not committed).
Switch via console: hud_newhudeditor_iconset "/hudpacks/<name>/" +
hud_newhudeditor_reload. Classic: "/ui/rml/hud/icons/".

### 2026-07-09 - Feedback round: dedup, scoreboard v2, iconsets (earlier)

Commit `f1589709`, answering user feedback (score ruim / AXE em texto /
informações duplicadas ao entrar em servidor / quer HUDs alternativos):
1. Duplication fixed: engine's Con_DrawNotify + SCR_CenterString_Draw are
   gated when the RmlUI HUD is on (it mirrors both itself).
2. Scoreboard v2: frag-sorted, spectators last, header, real quake team
   colours from gfx/palette.lmp (chip + coloured frags cell).
3. AXE text slot removed (original ibar shows weapons 2-8 only).
4. ICONSETS: hud_newhudeditor_iconset cvar + {{ iconpath }} binding on
   every image, extensionless names. To use community HUD packs from
   https://gfx.quakeworld.nu (bugs5-hud-numbers, gnoffa-simple-hud,
   foogs-hud, star-jedi-numbers etc.): extract the pack into
   E:\trabalho\quake\qw\textures\wad\ (standard install) and run
   `hud_newhudeditor_iconset "/textures/wad/"` + hud_newhudeditor_reload.
   Default set remains the extracted classics (/ui/rml/hud/icons/).

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
- **O HUD/picker RmlUi só renderiza DENTRO de partida (ca_active).** O contexto
  só é criado quando conectado; desconectado/menu não desenha (screenshot sai
  preto). A instalação de teste não tem servidor QW/progs (`map start` falha sem
  qwprogs), então NÃO dá pra renderizar/screenshot localmente. Antes de iterar em
  QUALQUER layout visual, PEDIR screenshot ao usuário (F12 →
  `qw/matchinfo/screenshots/`). Editar CSS às cegas já causou várias regressões.
- Deploy de assets: todo edit em `ui/` vai para `E:\trabalho\quake\id1\ui\` (e
  raiz por segurança): `cp -r <repo>/ui/. /e/trabalho/quake/id1/ui/`.
```

## 2026-07-09 — FIX: ícones sumiam com cfg do player ("só fica o relógio")

**Sintoma:** com `kilnovo.cfg` carregado, só o relógio aparecia (único widget de texto puro; todos os outros usam ícones).

**Causa raiz:** `R_LoadImagePixels` (carregador de imagem do ezQuake) só procura **dentro dos gamedirs** (id1/qw) + home, NÃO na raiz do Quake. Os ícones default (`ui/rml/hud/icons/`) estavam em `E:\trabalho\quake\ui` (raiz) → inalcançáveis pelo FS. RML/fontes carregavam pelo fallback stdio (cwd) do FileInterfaceVFS, mas ícones não têm esse fallback. Quando `kilnovo` roda `cfg_load`, reseta `hud_newhudeditor_iconset` pro default (pasta da raiz) → nenhum ícone carrega.

**Correção (só assets, sem recompilar):**
- Copiada a árvore `ui/` para `E:\trabalho\quake\id1\ui\` (gamedir sempre no search path do FS).
- Sincronizadas ambas as cópias (id1/ui e raiz/ui) com a versão mais recente do repo — a cópia deployada estava DESATUALIZADA (hud.rml antigo, não o resolução-independente).
- Path primário em LoadTexture já faz strip do `/` inicial → `ui/rml/hud/icons/X` → agora resolve em `id1/ui/...`. Fallback (linha 421-423) idem.

**IMPORTANTE p/ deploys futuros:** todo edit em `ui/` (rml/rcss/icons) deve ser deployado para `E:\trabalho\quake\id1\ui\` (e raiz por segurança). Comando: `cp -r <repo>/ui/. /e/trabalho/quake/id1/ui/`.

**PENDENTE:** usuário testar com kilnovo carregado — verificar se ícones aparecem e drag funciona no conwidth 640.

## 2026-07-09 — Editor via data-if + navegação por teclado no style picker

**Feedback:** "melhor mas style desconfigurado / editor não mostra nada / quero setas+Enter no seletor".

**Mudanças:**
1. **Painéis de edição por data-if (confiável):** `#configpanel` e `#edithint` agora aparecem via `data-if="editmode"` (mesmo mecanismo do model que já renderiza os widgets), em vez de depender do seletor CSS `body.edit`. Novo campo model `editmode` (bool), setado por `GameDataSetEditMode()` em EditorEnter/Exit. `body.edit .widget` (bordas) mantido como reforço cosmético.
2. **Style picker refeito como LISTA VERTICAL** (era grid de cards inline que estourava = "desconfigurado"). Cada linha: nome + mini-preview (num_1/num_2/armor2/shells/quad) + "em uso".
3. **Navegação por teclado:** setas ↑↓/←→ movem o destaque (com PREVIEW ao vivo do HUD inteiro), Enter aplica e fecha, ESC fecha. Model: `picker_sel` (path destacado), funcs `GameDataPickerMove(dir)` / `GameDataPickerApplySelected()`. Handler em `HUD_RmlUi_EditorKey` (K_UPARROW/K_DOWNARROW/K_LEFTARROW/K_RIGHTARROW/K_ENTER/KP_ENTER). Mouse (hover=preview, click=aplica) continua funcionando.

Build verde; exe + ui deployados (id1/ui e raiz). PENDENTE: usuário testar `hud_newhudeditor_style` (setas+Enter) e `hud_newhudeditor_edit` (painel aparece + arrastar).

## 2026-07-09 — FIX crítico: layout salvo em espaço de coord antigo escondia os widgets

**Descoberta:** `qw/rmlui_hud_layout.cfg` tinha posições ABSOLUTAS em pixels de uma resolução antiga (X até 940, Y até 506). Aplicado no load do documento (ApplyElementPosition), fixava quase todos os widgets FORA da tela de 640×360 (conwidth do kilnovo). Só `w_clock` (427,141) e poucos caíam dentro → **este era o motivo real de "só o relógio" e de "somem todos os itens no edit"** (não só o path dos ícones).

**Correções:**
1. Apagados todos os `rmlui_hud_layout.cfg` obsoletos.
2. **SaveLayout/LoadLayout agora fracionário** (posições como fração de conwidth/conheight, `%.5f`), resolução-independente. LoadLayout ignora valores >1 (formato legado em pixels) pra nunca mais empurrar widget pra fora.
3. **Centralização sem transform** (translateX/Y(-50%) não estava aplicando no RmlUi): `#stylepicker` via `left:0;right:0;margin:auto`; `#edithint` full-width text-center; `#w_weapons` full-width text-center; `#configpanel` top fixo. Removida classe `.center` do w_weapons.

Build verde; exe + ui deployados. PENDENTE testar: HUD completo aparecendo (todos os widgets on-screen), picker centralizado, edit mostrando widgets+painel e arrastar salvando fracionário.

## 2026-07-09 — Layout "Competitivo" selecionável (baseado no printquake.png)

Usuário colou `E:\trabalho\quake\printquake.png` (HUD clássico competitivo KTX de QW) como referência. Composição: kill feed (topo-esq), team overlay (esq), placar+timer (topo-centro), scoreboard por time (direita) — tudo engine clássico; base-centro = números GRANDES coloridos (armadura vermelha, vida verde `+100`, munição c/ ícone), ícone da arma ativa acima, grade 2×2 de munição.

**Entregue:**
- **`ui/rml/hud/hud_print.rml`** — layout "Competitivo": números grandes coloridos (`.bignum` 46dp), armadura colorida por tipo (ga/ya/ra), vida com `+` verde e cor por quantidade (mega/low), munição ícone+valor, arma ativa (inv2_*), grade de munição. Cada número é widget arrastável separado (w_armor/w_health/w_ammo/w_curweapon/w_ammocounts). Overlays clássicos continuam do engine.
- **Seleção de layout no picker:** model ganhou `layouts` (Clássico=hud.rml, Competitivo=hud_print.rml) + `layoutpath` (doc atual, p/ highlight). Picker (nos dois docs) agora tem seção "LAYOUT" no topo + "CONJUNTO DE ÍCONES" embaixo. Clicar num layout troca `hud_newhudeditor_doc`.
- **Troca de doc segura:** clique seta `g_pending_doc`; aplicado no início do `HUD_RmlUi_Frame` (Cvar_Set + LoadHudDocument), NUNCA no meio do dispatch do evento (evitaria unload do doc durante o próprio clique). Model (edit/picker/iconsets) persiste, então o novo doc já sobe com o picker aberto.
- `hud_newhudeditor_doc` externado no header p/ o model ler o doc atual.

Build verde; exe + ui + hud_print.rml deployados. PENDENTE testar: `hud_newhudeditor_style` → seção LAYOUT → clicar "Competitivo" → HUD vira números grandes; conferir cores/posições vs printquake.png e ajustar.

## 2026-07-09 — Diagnóstico "parou tudo" + FIX matemática do save fracionário

**Diagnóstico via -condebug (qconsole.log em qw/):** rodei o jogo eu mesmo. Log provou:
- `hud.rml` E `hud_print.rml` carregam SEM erro ("loaded document ...").
- Model criado sem erro. Fontes OK. => código base NÃO está quebrado.
- Achei a causa real: arquivo `qw/rmlui_hud_layout.cfg` com frações > 1 (`w_speed 1.392`, `w_face 1.319`), gerado pelo meu save fracionário BUGADO.

**Bug do save:** `GetAbsoluteOffset` retorna coordenadas em **dp** (density-independent), mas eu dividia por dimensões em **px físico** → fração inflada por 1/dp_ratio (ex. 1.33× em conheight 360) → passa de 1 → LoadLayout pula (ou empurra) o widget → some da tela. Como o arquivo acumulou saves de vários builds/resoluções, ficou totalmente corrompido.

**Correções:**
- SaveLayout agora converte dp→px físico ANTES da fração: `fx = (pos.x * dp_ratio) / width`, `dp_ratio = height/480`. Clamp [0,1] pra manter on-screen.
- Apagado o `rmlui_hud_layout.cfg` corrompido (todas as cópias) na instalação do usuário.
- LoadLayout mantém guarda: ignora valores fora de [0,1] (legado px/bugado).

**NÃO reproduzível localmente:** o mapa `start` não sobe sem qwprogs.qvm (sem mod/progs/demos na instalação), então não consegui screenshot da HUD ao vivo — o usuário joga em servidor. Log é a evidência.

Build verde; exe + ui deployados; layout corrompido apagado. PENDENTE: usuário **reiniciar do zero** (doc volta pro default hud.rml, sem layout salvo) e testar no servidor. Se persistir "nada aparece" ao vivo, próximo passo = logar contagem de draw calls do RmlUi por frame pra ver se renderiza.

## 2026-07-09 — Style picker recompactado (cortado/desarmonizado)

Feedback do usuário: "voltou a funcionar, só a tela do modelo dos huds corta tudo, desarmonizado".

**Causa raiz:** o container usava `width:440dp` fixo + `max-width:86%` + `margin:auto`, e cada linha de iconset amontoava name(32%) + prev(múltiplas imgs 20×25dp) + scur na MESMA LINHA — em conwidth pequeno estourava e cortava. `max-height:86%` do body deixava a parte inferior fora da área visível.

**Correção (RCSS-only, nos dois documentos):**
- Container agora é `top:8%; bottom:8%; left:12%; right:12%` — sempre proporcional à tela, nunca corta.
- Cada iconset row virou **duas linhas** (nome em cima, preview embaixo) em vez de tentar caber tudo horizontalmente.
- Fontes e imagens reduzidas (18/13/11dp; icons 16×20dp) pra caber mais em telas pequenas.
- Padding e margens apertadas.

Sem recompilar (RCSS-only); RML deployado nos dois locais (id1/ui e raiz).

## 2026-07-09 — Picker OK + FIX cursor invisível no editor/picker

Usuário confirmou (com screenshot ezquake000.jpg, HUD ao vivo funcionando): style picker OK depois de reescrever com a técnica `left:50% + margin-left:-140dp` (mesma da sbar). Picker agora minimalista: caixa 280dp centralizada, tudo empilhado, só texto, classe única `.row` pra layout+iconsets. Sem `%`/`margin:auto`/`right`/`bottom` (era o que quebrava).

**Cursor invisível — causa:** ordem de desenho. `SCR_DrawCursor()` roda dentro de `SCR_DrawElements()`, e o `HUD_RmlUi_Render()` desenha DEPOIS (cl_screen.c ~1038) → o painel do picker (fundo semi-opaco) cobre o cursor.

**Fix:** em `SCR_UpdateScreenHudOnly`, após `HUD_RmlUi_Render()`, se `HUD_RmlUi_InEditorMode()`, redesenha `SCR_DrawCursor()` + `R_FlushImageDraw()` — cursor por cima do RmlUi. scr_pointer_state já está em conwidth (bate com onde o RmlUi acha o mouse).

Build verde; exe deployado. PENDENTE: usuário testar cursor no `hud_newhudeditor_style` e `hud_newhudeditor_edit`.

### Nota de processo (importante)
Não consigo renderizar o HUD/picker localmente: contexto RmlUi só é criado em jogo (ca_active), e a instalação não tem servidor QW/progs pra subir mapa (`map start` falha sem qwprogs). Editar CSS às cegas causou várias regressões. REGRA: pedir screenshot ao usuário (F12 → qw/matchinfo/screenshots/) antes de iterar em layout visual do HUD.

## 2026-07-09 — Layout por documento + comando de reset

Avançando no que dá pra fazer sem ver a tela (lógica pura, sem ajuste visual):

1. **Layout por documento:** `LAYOUT_FILE` fixo virou `LayoutFile()`, que deriva o nome do doc atual — `rmlui_layout_hud.cfg` (Clássico) e `rmlui_layout_hud_print.cfg` (Competitivo). Antes os dois layouts compartilhavam um arquivo → arrastar num vazava posições pro outro. Agora cada layout tem posições independentes. Órfão `rmlui_hud_layout.cfg` apagado.

2. **`hud_newhudeditor_reset`** (comando novo): esvazia o arquivo de layout do doc atual, mostra todos os widgets, recarrega → HUD volta pros padrões do RCSS. Recuperação rápida se o layout ficar bagunçado (dado o histórico de dores com isso).

Build verde; exe deployado. Cursor no editor/picker AGUARDANDO teste do usuário (fix da ordem de desenho da mensagem anterior).

## 2026-07-09 — Kill feed editável + rosto/armas no Competitivo

Feedback: "faltam elementos no hud editor — rosto, armas, informação de morte de forma clara".

1. **Kill feed como widget editável (w_deaths):** nova função `VX_TrackerExportLine(index, buf, size)` em vx_tracker.c lê as mensagens ativas do tracker (mortes, armazenadas independente de r_tracker; imagens de arma viram " >> "). Model ganhou vetor `deaths` (até 8 linhas), populado no ReadEngineState, bind "deaths". Widget `#w_deaths` (canto sup. esq., vermelho) nos DOIS layouts. Adicionado ao WIDGET_DEFS ("Mortes (kill feed)"). Notify separado agora é só "Mensagens / chat".
2. **Tracker clássico gateado:** `VX_TrackerThink` só DESENHA o tracker clássico se `HUD_RmlUi_ShouldDrawClassicHud()` (senão duplicaria com nosso widget). Armazenamento/expiração continua rodando, alimentando o widget.
3. **Competitivo completo:** removido w_curweapon (só arma ativa), adicionada **barra de armas completa** (w_weapons, destaca a ativa) + **rosto** (w_face) + kill feed. Agora bate com o catálogo do editor.

Build verde; exe + RML deployados. PENDENTE testar em jogo: kill feed aparecendo ao matar/morrer, rosto e barra de armas no Competitivo, todos arrastáveis no editor. Pedir screenshot pra ajustar posição/estilo do kill feed.

## 2026-07-10 — Layouts novos (Brutalist/Minimal/Visor) do repo + fix de edição/resize

Após puxar do `origin/feat` os 2 commits do tibazera (kill feed/competitivo + os 3
layouts novos Brutalist/Minimal/Visor), rodada de correção dos layouts novos que
não deixavam editar/arrastar nem voltar pros outros pelo picker.

**Causa raiz (input morto nos layouts novos):** eram os ÚNICOS layouts com
dependências externas herdadas do vkQuake, e cada um quebrava o input de um jeito:
- **Brutalist** carregava `<link>` p/ `base.rcss` + `hud.rcss` (reset global,
  `div{display:block}`, `font-effect:outline`, propriedades `reticle-*`
  desconhecidas). O picker não recebia clique e o drag não pegava → "fica fixo".
- **Visor** tinha `body.visor-hud { pointer-events: none }` no `visor_hud.rcss`,
  que DESLIGA todo o input do documento (nem drag nem clique no picker).
- Prova: existia `rmlui_layout_hud.cfg`/`_hud_print`/`_hud_visor` mas NENHUM
  `_hud_brutalist.cfg` — o SaveLayout do editor nunca chegou a rodar no Brutalist.

**Correções:**
1. **hud_brutalist.rml** — reescrito autossuficiente (0 `<link>`, CSS inline),
   estrutura do `<body>` IDÊNTICA ao hud.rml (só cores diferem: preto/vermelho +
   Space Grotesk). Diff do body vs hud.rml = 1 palavra num comentário.
2. **minimal.rml** — era um `#panel` monolítico (id não-`w_`, o editor só arrasta
   ids `w_*`). Renomeado `panel`→`w_panel` (painel inteiro vira 1 widget
   arrastável); posições px→% (resolução-independente); realce de edição.
3. **hud_visor.rml** — reescrito autossuficiente (sem `pointer-events:none`, CSS
   inline), blocos viraram widgets `w_*` top-level arrastáveis; brackets/crosshair
   ficam fixos; paletas por tier de vida/powerup mantidas.
4. **RESIZE por alça de canto (novo, hud_rmlui.cpp):** cada widget do Visor tem
   uma alça `.rgrip` (canto ↘, só no edit). Puxá-la escala o widget via
   `transform: scale()` com origin top-left. Detecção pela CLASSE da alça sob o
   cursor (`IsOnResizeGrip`), não por geometria → imune à ambiguidade dp/px.
   Escala salva por widget/por documento (linhas `scale <id> <f>` no layout cfg,
   lidas ANTES das `pos` pra ApplyElementPosition já emitir o transform).
   `g_widget_scale` limpo ao trocar de doc. Resize só no Visor por ora (as alças
   só existem lá); adicionar aos outros é 1 `<span class="rgrip">` por widget.

Build verde; exe + RML deployados (id1/ui + raiz); `rmlui_layout_hud_visor.cfg`
antigo (2 widgets) apagado. PENDENTE testar em jogo: Brutalist/Minimal/Visor
arrastáveis + voltar pros outros pelo picker; alça de resize no Visor. Limitação
conhecida: o painel show/hide usa a lista global de widgets (ids clássicos), então
no Visor só alterna os comuns (vida/armadura/kill feed/mensagens/avisos) — drag e
resize funcionam em todos os blocos. Pedir screenshot pra calibrar posições/tamanho.

### 2026-07-10 (tarde) — Visor: mira removida + layout restaurado; drag mais robusto

Feedback: "desconfigurou tudo na tela, inclusive a mira, ela ficou torta / minimal
ainda não arrasta". Eu tinha reescrito o Visor com posições em `%` ESTIMADAS às
cegas (violando a regra do arquivo) e adicionado uma mira própria.

- **Mira removida do Visor:** o ezQuake já desenha o crosshair nativo do jogador;
  a segunda mira (RmlUi) brigava com ela = "torta". `#crosshair-container` fora.
- **Layout restaurado à composição original:** desfiz as posições `%` estimadas.
  Voltei aos GRUPOS do visor_hud.rcss com flex interno e posições dp originais
  (header/left-col/keys/vitals/scores/weapon), agora cada grupo = 1 widget `w_*`
  arrastável (safe-zone dropada; grupos são filhos diretos do body p/ o SaveLayout
  pegar). Alça de resize em cada grupo.
- **`TopLevelFor` robusto (hud_rmlui.cpp):** era "filho direto do body"; agora
  resolve p/ o próprio elemento ou o ancestral mais próximo com id `w_*`
  (superconjunto do antigo). Cobre widgets aninhados e o w_panel do Minimal.

Build verde; exe + visor deployados; `rmlui_layout_hud_visor.cfg` apagado.
PENDENTE (pedido ao usuário): screenshot do Minimal e do Visor em modo edição p/
confirmar drag e calibrar posições — não dá pra renderizar localmente.
