/*
 * ezQuake RmlUI HUD bridge.
 *
 * Initial OpenGL-only scaffold. The next step is to replace the internal stub
 * with RmlUI Context + RenderInterface_GL wiring.
 */

#include "hud_rmlui.h"

extern "C" {
#include "quakedef.h"
}

cvar_t hud_newhudeditor = {"hud_newhudeditor", "0"};

namespace {

struct RmlHudState {
	bool initialized = false;
	int width = 0;
	int height = 0;
	int health = 0;
	int armor = 0;
	int ammo = 0;
	int active_weapon = 0;
	int items = 0;
	int intermission = 0;
	int gametype = 0;
	int maxclients = 0;
	double game_time = 0.0;
	char map_name[64] = {0};
};

RmlHudState g_hud;

bool RmlModeEnabled()
{
	return hud_newhudeditor.integer != 0;
}

bool ClassicModeEnabled()
{
	return !RmlModeEnabled();
}

} // namespace

extern "C" {

void HUD_RmlUi_Init(void)
{
	g_hud.initialized = true;
	Com_Printf("RmlUI HUD: OpenGL bridge scaffold initialized. Set hud_newhudeditor 1 to use it.\n");
}

void HUD_RmlUi_Shutdown(void)
{
	g_hud = RmlHudState{};
}

void HUD_RmlUi_Frame(double dt)
{
	(void)dt;
	if (!g_hud.initialized || !RmlModeEnabled()) {
		return;
	}

	/* Future: update RmlUI data models and animations here. */
}

void HUD_RmlUi_Render(void)
{
	if (!g_hud.initialized || !RmlModeEnabled()) {
		return;
	}

	/*
	 * Future: Rml::Context::Render() through RenderInterface_GL.
	 * This intentionally does not touch the vkQuake Vulkan renderer.
	 */
}

void HUD_RmlUi_Resize(int width, int height)
{
	g_hud.width = width;
	g_hud.height = height;
}

void HUD_RmlUi_SyncGameState(
	const int* stats,
	int stats_count,
	int items,
	int intermission,
	int gametype,
	int maxclients,
	const char* map_name,
	double game_time)
{
	if (!stats || stats_count <= 0) {
		return;
	}

	g_hud.items = items;
	g_hud.intermission = intermission;
	g_hud.gametype = gametype;
	g_hud.maxclients = maxclients;
	g_hud.game_time = game_time;

	if (map_name) {
		strlcpy(g_hud.map_name, map_name, sizeof(g_hud.map_name));
	}

	if (STAT_HEALTH < stats_count) {
		g_hud.health = stats[STAT_HEALTH];
	}
	if (STAT_ARMOR < stats_count) {
		g_hud.armor = stats[STAT_ARMOR];
	}
	if (STAT_AMMO < stats_count) {
		g_hud.ammo = stats[STAT_AMMO];
	}
	if (STAT_ACTIVEWEAPON < stats_count) {
		g_hud.active_weapon = stats[STAT_ACTIVEWEAPON];
	}
}

int HUD_RmlUi_IsEnabled(void)
{
	return RmlModeEnabled() ? 1 : 0;
}

int HUD_RmlUi_ShouldDrawClassicHud(void)
{
	return ClassicModeEnabled() ? 1 : 0;
}

} // extern "C"
