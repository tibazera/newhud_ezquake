/*
 * ezQuake RmlUI HUD bridge.
 *
 * Real RmlUI (6.x) lifecycle: system + render interfaces are installed at
 * init, the "hud" context is created lazily once the video dimensions are
 * known, and Update()/Render() run from the engine 2D frame path. The
 * render interface is still a safe stub (no GL yet), so rendering is a
 * no-op by design - next slice implements the OpenGL path.
 */

/* RmlUi first: pure C++ headers, keep them clear of engine macros. */
#include <RmlUi/Core.h>

/*
 * Engine headers inside extern "C": cvar.h/quakedef.h assume q_shared.h
 * types (qbool, byte) and every declaration must keep C linkage here.
 */
extern "C" {
#include "quakedef.h"
}

#include "hud_rmlui.h"
#include "render_interface_gl.h"

cvar_t hud_newhudeditor = {"hud_newhudeditor", "0"};

namespace {

class SystemInterfaceEz : public Rml::SystemInterface {
public:
	double GetElapsedTime() override
	{
		return Sys_DoubleTime();
	}

	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
	{
		const char* prefix = (type <= Rml::Log::LT_ERROR) ? "ERROR " : "";
		Com_Printf("RmlUI: %s%s\n", prefix, message.c_str());
		return true;
	}
};

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
	Rml::String map_name_str; // bindable mirror of map_name for the data model

	SystemInterfaceEz* system_interface = nullptr;
	ezquake::rmlui::RenderInterfaceGL* render_interface = nullptr;
	Rml::Context* context = nullptr;
	Rml::DataModelHandle data_model;
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

void CreateGameDataModel()
{
	/*
	 * GameDataModel: exposes the synced game state to RML documents as the
	 * "hud" data model, so markup can read {{health}}, {{armor}}, etc.
	 * Bound directly to the RmlHudState fields (global lifetime).
	 */
	Rml::DataModelConstructor constructor = g_hud.context->CreateDataModel("hud");
	if (!constructor) {
		Com_Printf("RmlUI HUD: ERROR failed to create data model\n");
		return;
	}

	constructor.Bind("health", &g_hud.health);
	constructor.Bind("armor", &g_hud.armor);
	constructor.Bind("ammo", &g_hud.ammo);
	constructor.Bind("weapon", &g_hud.active_weapon);
	constructor.Bind("items", &g_hud.items);
	constructor.Bind("intermission", &g_hud.intermission);
	constructor.Bind("gametype", &g_hud.gametype);
	constructor.Bind("maxclients", &g_hud.maxclients);
	constructor.Bind("time", &g_hud.game_time);
	constructor.Bind("map", &g_hud.map_name_str);

	g_hud.data_model = constructor.GetModelHandle();
}

void EnsureContext()
{
	if (g_hud.context || !g_hud.initialized || g_hud.width <= 0 || g_hud.height <= 0) {
		return;
	}

	g_hud.context = Rml::CreateContext("hud", Rml::Vector2i(g_hud.width, g_hud.height));
	if (g_hud.context) {
		Com_Printf("RmlUI HUD: context created (%dx%d)\n", g_hud.width, g_hud.height);
		CreateGameDataModel();
	}
	else {
		Com_Printf("RmlUI HUD: ERROR failed to create context\n");
	}
}

} // namespace

extern "C" {

void HUD_RmlUi_Init(void)
{
	if (g_hud.initialized) {
		return;
	}

	g_hud.system_interface = new SystemInterfaceEz();
	g_hud.render_interface = new ezquake::rmlui::RenderInterfaceGL();

	Rml::SetSystemInterface(g_hud.system_interface);
	Rml::SetRenderInterface(g_hud.render_interface);

	if (!Rml::Initialise()) {
		Com_Printf("RmlUI HUD: ERROR Rml::Initialise() failed\n");
		delete g_hud.render_interface;
		delete g_hud.system_interface;
		g_hud = RmlHudState{};
		return;
	}

	g_hud.initialized = true;
	Com_Printf("RmlUI HUD: initialised (RmlUi %s). Set hud_newhudeditor 1 to use it.\n",
		Rml::GetVersion().c_str());
}

void HUD_RmlUi_Shutdown(void)
{
	if (!g_hud.initialized) {
		return;
	}

	/* Release GL resources while the context is still current. */
	if (g_hud.render_interface) {
		g_hud.render_interface->Shutdown();
	}

	/* Rml::Shutdown destroys all contexts. */
	Rml::Shutdown();
	delete g_hud.render_interface;
	delete g_hud.system_interface;
	g_hud = RmlHudState{};
}

void HUD_RmlUi_Frame(double dt)
{
	(void)dt;
	if (!g_hud.initialized || !RmlModeEnabled()) {
		return;
	}

	EnsureContext();
	if (g_hud.context) {
		/* Publish the latest synced game state to the data model. */
		if (g_hud.data_model) {
			g_hud.data_model.DirtyAllVariables();
		}
		g_hud.context->Update();
	}
}

void HUD_RmlUi_Render(void)
{
	if (!g_hud.initialized || !RmlModeEnabled() || !g_hud.context) {
		return;
	}

	/*
	 * Render through RenderInterfaceGL. BeginFrame/EndFrame set up and
	 * restore GL state around the RmlUI draw calls so the engine's own
	 * rendering is not disturbed.
	 */
	g_hud.render_interface->BeginFrame(g_hud.width, g_hud.height);
	g_hud.context->Render();
	g_hud.render_interface->EndFrame();
}

void HUD_RmlUi_Resize(int width, int height)
{
	g_hud.width = width;
	g_hud.height = height;

	if (g_hud.context) {
		g_hud.context->SetDimensions(Rml::Vector2i(width, height));
	}
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
		g_hud.map_name_str = g_hud.map_name;
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
