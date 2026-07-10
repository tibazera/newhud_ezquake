/*
 * ezQuake RmlUI HUD bridge.
 *
 * C-facing API used by the engine. The implementation is C++ so RmlUI can be
 * integrated without leaking C++ into the rest of ezQuake.
 */

#ifndef EZQUAKE_HUD_RMLUI_H
#define EZQUAKE_HUD_RMLUI_H

/*
 * NOTE: this header does NOT include engine headers on purpose. cvar.h
 * cannot be compiled standalone (it needs qbool/byte from q_shared.h), so
 * consumers must include the engine headers first:
 *   - C consumers (hud.c, cl_screen.c) already do via quakedef.h.
 *   - The C++ implementation (hud_rmlui.cpp) includes quakedef.h inside
 *     extern "C" before this header.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern cvar_t hud_newhudeditor;
extern cvar_t hud_newhudeditor_iconset;
extern cvar_t hud_newhudeditor_doc;

void HUD_RmlUi_Init(void);
void HUD_RmlUi_Shutdown(void);

/*
 * Video/GL teardown hook. Call from VID_Shutdown() BEFORE R_Shutdown() /
 * SDL_GL_DeleteContext(), while the GL context is still current.
 *   restart != 0 (vid_restart): releases every GL resource (RmlUi textures
 *     and compiled geometry, plus the render interface's own objects) but
 *     keeps RmlUi, the context and documents alive; everything is lazily
 *     recreated against the new GL context.
 *   restart == 0 (full quit): performs the full HUD_RmlUi_Shutdown().
 */
void HUD_RmlUi_VidShutdown(int restart);
void HUD_RmlUi_Frame(double dt);
void HUD_RmlUi_Render(void);
void HUD_RmlUi_Resize(int width, int height);

/*
 * Publishes the current game state to the "hud" data model. The model
 * reads the engine globals (cl/cls/cvars) directly; call once per frame
 * from the 2D path, before HUD_RmlUi_Frame().
 */
void HUD_RmlUi_SyncGameState(void);

int HUD_RmlUi_IsEnabled(void);
int HUD_RmlUi_ShouldDrawClassicHud(void);

/*
 * Event push hooks (safe to call regardless of HUD state).
 * Called from hud_centerprint.c so centerprint messages reach the data
 * model with the classic timing semantics.
 */
void HUD_RmlUi_CenterPrint(const char* str);
void HUD_RmlUi_CenterPrintClear(void);

/*
 * Editor mode (hud_newhudeditor 2): drag-and-drop layout editing.
 * Input arrives through the classic key_hudeditor routing:
 *  - HUD_RmlUi_MouseEvent from Mouse_EventDispatch (keys.c), receives the
 *    global mouse_state_t (cast as void* to keep this header light).
 *  - HUD_RmlUi_EditorKey from Key_EventEx's key_hudeditor cases; handles
 *    ESC (exit to mode 1) and the mouse wheel keynums.
 * HUD_RmlUi_InEditorMode tells keys.c whether to route to us instead of
 * the classic HUD_Editor.
 */
void HUD_RmlUi_MouseEvent(void* mouse_state);
void HUD_RmlUi_EditorKey(int key, int unichar, int down);
int HUD_RmlUi_InEditorMode(void);

#ifdef __cplusplus
}
#endif

#endif /* EZQUAKE_HUD_RMLUI_H */
