# Porting RmlUI HUD/Menu Layer to ezQuake

This repository currently contains a vkQuake fork with an RML/RCSS/Lua UI layer.
The goal is to port the reusable UI system to ezQuake without treating it as a
plain asset copy. The important part is the engine adapter around RmlUI.

## What Must Be Ported

- `ui/`
  - Runtime RML documents, RCSS styles, fonts, and Lua tests/scripts.
- `ui_lab/`
  - Example mod showing a more advanced HUD implementation.
- `src/ui_manager.h`
  - Public C API used by the C engine code.
- `src/ui_manager.cpp`
  - Document lifecycle, menu stack, HUD visibility, input modes, hot reload.
- `src/types/`
  - Small shared data structures used by the C/C++ boundary.
- `src/internal/`
  - RmlUI renderer, data models, cvar binding, notification model, menu actions,
    file interface, Lua bridge, and custom reticle elements.
- Build integration
  - RmlUI submodule/library, Freetype dependency, optional Lua dependency, and
    a `USE_RMLUI` compile flag.

## Engine Hook Points

The vkQuake integration is spread behind `#ifdef USE_RMLUI`. ezQuake needs
equivalent hook points, not necessarily identical file names.

| Area | vkQuake file | RmlUI calls |
| --- | --- | --- |
| Startup/shutdown | `Quake/host.c` | `UI_Init`, command registration, `UI_Shutdown` |
| Renderer setup | `Quake/gl_vidsdl.c` | vkQuake-specific Vulkan setup; replace with ezQuake OpenGL setup |
| Frame overlay | `Quake/gl_screen.c` | `UI_ProcessPending`, `UI_Update`, `UI_Render`, notifications |
| HUD game state | `Quake/sbar.c` | `UI_ShowHUD`, `UI_HideHUD`, `UI_SyncGameState`, scoreboard |
| Input | `Quake/in_sdl2.c`, `Quake/in_sdl3.c`, `Quake/keys.c` | `UI_KeyEvent`, `UI_CharEvent`, mouse events, Escape handling |
| Menu compatibility | `Quake/menu.c` | delegate menu open/close/save-slot data |
| Cvars/commands | `src/internal/quake_cvar_provider.cpp`, `quake_command_executor.cpp` | bridge to ezQuake cvar and command systems |
| Notifications | `Quake/gl_screen.c`, console paths | `UI_NotifyCenterPrint`, `UI_NotifyPrint` |

## First ezQuake Port Strategy

1. Add the UI layer as an optional build feature.
   - Keep `USE_RMLUI` or rename it only after the first successful compile.
   - Build RmlUI and Freetype first, with Lua disabled until the core HUD works.

2. Port the C API and non-rendering internals.
   - Bring over `src/ui_manager.h`, `src/ui_manager.cpp`, `src/types/`, and
     non-Vulkan-specific `src/internal/` files.
   - Stub the render interface if needed so data/model/input compilation can
     be solved separately.

3. Adapt ezQuake cvars and commands.
   - Replace `quake_cvar_provider.*` and `quake_command_executor.*` calls with
     ezQuake equivalents.
   - Start with HUD-only cvars before attempting full options menus.

4. Sync gameplay state into the HUD.
   - Map ezQuake's player stats, items, active weapon, ammo, match state,
     scoreboard players, chat buffer, and centerprint data into `GameState`.
   - Validate classic Quake bit flags first; ezQuake multiplayer fields will
     likely need extra bindings.

5. Integrate input.
   - Route keyboard, text, mouse buttons, movement, and wheel into RmlUI only
     while menus are active.
   - Preserve Escape semantics and console behavior.

6. Integrate rendering.
   - Implement a new RmlUI render interface for ezQuake's OpenGL renderer.
   - Do not port the vkQuake Vulkan backend.
   - Start with modern OpenGL; keep the classic HUD as fallback where needed.

7. Enable the HUD before full menus.
   - Load `ui/rml/hud/hud.rml`, `notify.rml`, `centerprint.rml`, and `chat.rml`.
   - Keep the original ezQuake HUD behind a cvar fallback until feature parity
     is good.

## Main Risks

- Renderer mismatch: the current vkQuake implementation is Vulkan-specific and
  must be replaced by an OpenGL RmlUI backend for ezQuake.
- ezQuake HUD/multiplayer state is richer than vanilla Quake, so the data
  contract will need extra fields.
- Input handling is easy to break around Escape, console, chat, and key binds.
- Menus touch many cvars and commands; HUD-first is lower risk than porting all
  menus first.
- RML assets are portable, but the engine bridges are not.

## Suggested First Milestone

Get a minimal ezQuake build that:

- Compiles with `USE_RMLUI`.
- Loads RmlUI and fonts.
- Shows only `hud.rml` over the game.
- Syncs health, armor, ammo, active weapon, items, map name, and centerprint.
- Leaves original ezQuake menus untouched.

After that, add scoreboard, chat overlay, notify lines, and finally menus.
