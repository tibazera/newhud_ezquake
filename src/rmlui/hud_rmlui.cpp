/*
 * ezQuake RmlUI HUD bridge.
 *
 * RmlUI (6.x) lifecycle: system/render/file interfaces are installed at
 * init, the "hud" context is created lazily once the video dimensions are
 * known, the GameDataModel (game_data_model.cpp) publishes engine state to
 * documents, and Update()/Render() run from the engine 2D frame path
 * through the OpenGL render interface.
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
#include "file_interface_vfs.h"
#include "game_data_model.h"

cvar_t hud_newhudeditor = {"hud_newhudeditor", "0"};
cvar_t hud_newhudeditor_doc = {"hud_newhudeditor_doc", "ui/rml/hud/hud.rml"};

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

	SystemInterfaceEz* system_interface = nullptr;
	ezquake::rmlui::RenderInterfaceGL* render_interface = nullptr;
	ezquake::rmlui::FileInterfaceVFS* file_interface = nullptr;
	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	bool fonts_loaded = false;
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

void LoadFonts()
{
	if (g_hud.fonts_loaded) {
		return;
	}
	/* Paths resolve through RmlUi's file interface, relative to the client
	 * working directory - the ui/ tree must be present in the game dir. */
	const bool regular = Rml::LoadFontFace("ui/fonts/LatoLatin-Regular.ttf");
	Rml::LoadFontFace("ui/fonts/LatoLatin-Bold.ttf");
	if (!regular) {
		Com_Printf("RmlUI HUD: WARNING could not load ui/fonts/LatoLatin-Regular.ttf\n");
	}
	g_hud.fonts_loaded = true;
}

void LoadHudDocument()
{
	if (!g_hud.context) {
		return;
	}
	if (g_hud.document) {
		g_hud.context->UnloadDocument(g_hud.document);
		g_hud.document = nullptr;
	}

	const char* path = hud_newhudeditor_doc.string;
	g_hud.document = g_hud.context->LoadDocument(path);
	if (g_hud.document) {
		g_hud.document->Show();
		Com_Printf("RmlUI HUD: loaded document %s\n", path);
	}
	else {
		Com_Printf("RmlUI HUD: ERROR failed to load document %s\n", path);
	}
}

void EnsureContext()
{
	if (g_hud.context || !g_hud.initialized || g_hud.width <= 0 || g_hud.height <= 0) {
		return;
	}

	g_hud.context = Rml::CreateContext("hud", Rml::Vector2i(g_hud.width, g_hud.height));
	if (g_hud.context) {
		Com_Printf("RmlUI HUD: context created (%dx%d)\n", g_hud.width, g_hud.height);
		if (!ezquake::rmlui::GameDataCreate(g_hud.context)) {
			Com_Printf("RmlUI HUD: ERROR failed to create data model\n");
		}
		LoadHudDocument();
	}
	else {
		Com_Printf("RmlUI HUD: ERROR failed to create context\n");
	}
}

} // namespace

extern "C" {

static void HUD_RmlUi_Reload_f(void)
{
	if (!g_hud.initialized) {
		Com_Printf("RmlUI HUD: not initialised\n");
		return;
	}
	if (!g_hud.context) {
		Com_Printf("RmlUI HUD: no context yet (enable hud_newhudeditor 1)\n");
		return;
	}
	LoadHudDocument();
}

void HUD_RmlUi_Init(void)
{
	if (g_hud.initialized) {
		return;
	}

	g_hud.system_interface = new SystemInterfaceEz();
	g_hud.render_interface = new ezquake::rmlui::RenderInterfaceGL();
	g_hud.file_interface = new ezquake::rmlui::FileInterfaceVFS();

	Rml::SetSystemInterface(g_hud.system_interface);
	Rml::SetRenderInterface(g_hud.render_interface);
	Rml::SetFileInterface(g_hud.file_interface);

	if (!Rml::Initialise()) {
		Com_Printf("RmlUI HUD: ERROR Rml::Initialise() failed\n");
		delete g_hud.file_interface;
		delete g_hud.render_interface;
		delete g_hud.system_interface;
		g_hud = RmlHudState{};
		return;
	}

	LoadFonts();

	Cvar_Register(&hud_newhudeditor_doc);
	Cmd_AddCommand("hud_newhudeditor_reload", HUD_RmlUi_Reload_f);

	g_hud.initialized = true;
	Com_Printf("RmlUI HUD: initialised (RmlUi %s). Set hud_newhudeditor 1 to use it.\n",
		Rml::GetVersion().c_str());
}

void HUD_RmlUi_Shutdown(void)
{
	if (!g_hud.initialized) {
		return;
	}

	/*
	 * Rml::Shutdown destroys all contexts/documents, releasing their GL
	 * resources through the render interface - so it must run while the GL
	 * context is still current, and before the interface's own teardown.
	 */
	Rml::Shutdown();
	ezquake::rmlui::GameDataReset();

	if (g_hud.render_interface) {
		g_hud.render_interface->Shutdown();
	}

	delete g_hud.file_interface;
	delete g_hud.render_interface;
	delete g_hud.system_interface;
	g_hud = RmlHudState{};
}

void HUD_RmlUi_VidShutdown(int restart)
{
	if (!g_hud.initialized) {
		return;
	}

	if (!restart) {
		HUD_RmlUi_Shutdown();
		return;
	}

	/*
	 * vid_restart: the GL context is about to be destroyed and recreated.
	 * Release every GL-backed resource now, while the old context is still
	 * current. RmlUi re-generates textures and re-compiles geometry on
	 * demand, and the render interface re-initialises lazily, so contexts
	 * and documents survive the restart untouched.
	 */
	Rml::ReleaseCompiledGeometry(g_hud.render_interface);
	Rml::ReleaseTextures(g_hud.render_interface);
	g_hud.render_interface->OnContextLost();
	Com_Printf("RmlUI HUD: GL resources released for video restart\n");
}

void HUD_RmlUi_Frame(double dt)
{
	(void)dt;
	if (!g_hud.initialized || !RmlModeEnabled()) {
		return;
	}

	EnsureContext();
	if (g_hud.context) {
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

void HUD_RmlUi_SyncGameState(void)
{
	/*
	 * The GameDataModel reads the engine globals (cl/cls/cvars) directly
	 * and dirties only the variables that changed. Runs before Frame()'s
	 * Context::Update() in the 2D frame path.
	 */
	if (!g_hud.initialized || !RmlModeEnabled() || !g_hud.context) {
		return;
	}
	ezquake::rmlui::GameDataSync();
}

void HUD_RmlUi_CenterPrint(const char* str)
{
	ezquake::rmlui::GameDataCenterPrint(str);
}

void HUD_RmlUi_CenterPrintClear(void)
{
	ezquake::rmlui::GameDataCenterPrintClear();
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
