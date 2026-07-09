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
#include "keys.h"
}

#include "hud_rmlui.h"
#include "render_interface_gl.h"
#include "file_interface_vfs.h"
#include "game_data_model.h"

cvar_t hud_newhudeditor = {"hud_newhudeditor", "0"};
cvar_t hud_newhudeditor_doc = {"hud_newhudeditor_doc", "ui/rml/hud/hud.rml"};
/*
 * Icon lookup prefix used by the HUD documents (extensionless names, the
 * engine tries .tga/.png/.jpg). Leading '/' keeps RmlUi from joining the
 * path against the document directory. Community HUD packs installed the
 * standard way (qw/textures/wad/) can be selected with:
 *   hud_newhudeditor_iconset "/textures/wad/"
 */
cvar_t hud_newhudeditor_iconset = {"hud_newhudeditor_iconset", "/ui/rml/hud/icons/"};

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

/* ---- editor mode (hud_newhudeditor 2) state ---- */
struct EditorState {
	Rml::Element* dragging = nullptr;
	Rml::Vector2f grab_offset; // mouse - element top-left at grab time
	bool picker_open = false;  // visual iconset picker overlay
};
EditorState g_editor;

void StylePickerClose();      // defined below
void SaveLayout();            // defined below
void ApplyWidgetVisibility(); // defined below

/*
 * Style picker interactions: hovering a card live-previews the whole HUD
 * with that iconset; clicking applies it to the cvar; the close button
 * (or ESC) dismisses the overlay.
 */
class PickerEventListener : public Rml::EventListener {
public:
	void ProcessEvent(Rml::Event& event) override
	{
		Rml::Element* target = event.GetTargetElement();

		if (event.GetId() == Rml::EventId::Click) {
			for (Rml::Element* el = target; el; el = el->GetParentNode()) {
				if (el->GetId() == "styleclose") {
					StylePickerClose();
					return;
				}
			}
		}

		/* Find the card (element carrying the setpath attribute). */
		Rml::Element* card = target;
		while (card && !card->HasAttribute("setpath")) {
			card = card->GetParentNode();
		}
		if (!card) {
			return;
		}
		const Rml::String path = card->GetAttribute<Rml::String>("setpath", "");
		if (path.empty()) {
			return;
		}

		switch (event.GetId()) {
			case Rml::EventId::Click:
				Cvar_Set(&hud_newhudeditor_iconset, (char*)path.c_str());
				ezquake::rmlui::GameDataEndPreviewIconset();
				break;
			case Rml::EventId::Mouseover:
				ezquake::rmlui::GameDataPreviewIconset(path.c_str());
				break;
			case Rml::EventId::Mouseout:
				ezquake::rmlui::GameDataEndPreviewIconset();
				break;
			default:
				break;
		}
	}
};
PickerEventListener g_picker_listener;

/* Config panel: clicking a widget row toggles its visibility. */
class ConfigEventListener : public Rml::EventListener {
public:
	void ProcessEvent(Rml::Event& event) override
	{
		if (event.GetId() != Rml::EventId::Click) {
			return;
		}
		Rml::Element* row = event.GetTargetElement();
		while (row && !row->HasAttribute("wid")) {
			row = row->GetParentNode();
		}
		if (!row) {
			return;
		}
		const Rml::String id = row->GetAttribute<Rml::String>("wid", "");
		if (id.empty()) {
			return;
		}
		ezquake::rmlui::GameDataToggleWidget(id.c_str());
		ApplyWidgetVisibility();
		SaveLayout();
	}
};
ConfigEventListener g_config_listener;

bool RmlModeEnabled()
{
	return hud_newhudeditor.integer != 0;
}

bool EditorModeEnabled()
{
	return hud_newhudeditor.integer == 2;
}

bool ClassicModeEnabled()
{
	return !RmlModeEnabled();
}

void LoadLayout(); // defined below with the editor block

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
		LoadLayout();
		if (EditorModeEnabled()) {
			g_hud.document->SetClass("edit", true);
		}
		if (Rml::Element* picker = g_hud.document->GetElementById("stylepicker")) {
			picker->AddEventListener("click", &g_picker_listener);
			picker->AddEventListener("mouseover", &g_picker_listener);
			picker->AddEventListener("mouseout", &g_picker_listener);
		}
		if (Rml::Element* cfg = g_hud.document->GetElementById("configpanel")) {
			cfg->AddEventListener("click", &g_config_listener);
		}
		Com_Printf("RmlUI HUD: loaded document %s\n", path);
	}
	else {
		Com_Printf("RmlUI HUD: ERROR failed to load document %s\n", path);
	}
}

/* ---------------- editor: layout persistence ---------------- */

const char* LAYOUT_FILE = "rmlui_hud_layout.cfg";

/* Pin an element to absolute pixel coordinates, neutralising the anchors
 * the stylesheet may use (centering margins, right/bottom anchoring). */
void ApplyElementPosition(Rml::Element* element, float x, float y)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%dpx", (int)x);
	element->SetProperty("left", buf);
	snprintf(buf, sizeof(buf), "%dpx", (int)y);
	element->SetProperty("top", buf);
	element->SetProperty("right", "auto");
	element->SetProperty("bottom", "auto");
	element->SetProperty("margin-left", "0");
	element->SetProperty("margin-top", "0");
}

/* Draggable HUD widgets are the top-level elements whose id starts with
 * "w_"; overlays (picker, edit hint) are excluded. */
bool IsWidgetId(const Rml::String& id)
{
	return id.size() > 2 && id[0] == 'w' && id[1] == '_';
}

/* Show/hide each widget per the model's hidden set. */
void ApplyWidgetVisibility()
{
	if (!g_hud.document) {
		return;
	}
	const int n = ezquake::rmlui::GameDataWidgetCount();
	for (int i = 0; i < n; ++i) {
		const char* id = ezquake::rmlui::GameDataWidgetIdAt(i);
		if (Rml::Element* el = g_hud.document->GetElementById(id)) {
			el->SetClass("whidden", ezquake::rmlui::GameDataWidgetHidden(id));
		}
	}
}

void SaveLayout()
{
	if (!g_hud.document) {
		return;
	}

	Rml::String out;
	for (int i = 0; i < g_hud.document->GetNumChildren(); ++i) {
		Rml::Element* child = g_hud.document->GetChild(i);
		const Rml::String& id = child->GetId();
		if (!IsWidgetId(id)) {
			continue;
		}
		const Rml::Vector2f pos = child->GetAbsoluteOffset(Rml::BoxArea::Border);
		char line[128];
		snprintf(line, sizeof(line), "pos %s %d %d\n", id.c_str(), (int)pos.x, (int)pos.y);
		out += line;
	}

	/* Hidden widgets. */
	const int n = ezquake::rmlui::GameDataWidgetCount();
	for (int i = 0; i < n; ++i) {
		const char* id = ezquake::rmlui::GameDataWidgetIdAt(i);
		if (ezquake::rmlui::GameDataWidgetHidden(id)) {
			out += Rml::String("hide ") + id + "\n";
		}
	}

	vfsfile_t* f = FS_OpenVFS(LAYOUT_FILE, (char*)"wb", FS_GAME_OS);
	if (!f) {
		Com_Printf("RmlUI HUD: WARNING could not write %s\n", LAYOUT_FILE);
		return;
	}
	VFS_WRITE(f, out.c_str(), (int)out.size());
	VFS_CLOSE(f);
}

void LoadLayout()
{
	if (!g_hud.document) {
		return;
	}

	vfsfile_t* f = FS_OpenVFS(LAYOUT_FILE, (char*)"rb", FS_ANY);
	if (!f) {
		return; // no saved layout yet
	}

	const int len = (int)VFS_GETLEN(f);
	Rml::String text(len, '\0');
	vfserrno_t err = VFSERR_NONE;
	VFS_READ(f, &text[0], len, &err);
	VFS_CLOSE(f);

	size_t start = 0;
	int applied = 0;
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		if (end == Rml::String::npos) {
			end = text.size();
		}
		const Rml::String line = text.substr(start, end - start);
		char id[64];
		int x = 0, y = 0;
		if (sscanf(line.c_str(), "pos %63s %d %d", id, &x, &y) == 3) {
			if (Rml::Element* el = g_hud.document->GetElementById(id)) {
				ApplyElementPosition(el, (float)x, (float)y);
				++applied;
			}
		}
		else if (sscanf(line.c_str(), "hide %63s", id) == 1) {
			ezquake::rmlui::GameDataSetWidgetHidden(id, true);
		}
		start = end + 1;
	}
	ApplyWidgetVisibility();
	if (applied) {
		Com_Printf("RmlUI HUD: layout loaded (%d elements)\n", applied);
	}
}

/* ---------------- editor: dragging ---------------- */

/* The draggable unit is a direct child of the document body. */
Rml::Element* TopLevelFor(Rml::Element* element)
{
	if (!element || !g_hud.document) {
		return nullptr;
	}
	Rml::Element* body = g_hud.document;
	Rml::Element* walk = element;
	while (walk && walk->GetParentNode() != body) {
		walk = walk->GetParentNode();
	}
	return (walk && walk != body) ? walk : nullptr;
}

void EditorStartDrag(float mx, float my)
{
	Rml::Element* top = TopLevelFor(g_hud.context->GetHoverElement());
	if (!top || !IsWidgetId(top->GetId())) {
		return;
	}

	/* Freeze the element at its current visual position first, so
	 * switching anchors does not make it jump. */
	const Rml::Vector2f pos = top->GetAbsoluteOffset(Rml::BoxArea::Border);
	ApplyElementPosition(top, pos.x, pos.y);

	g_editor.dragging = top;
	g_editor.grab_offset = Rml::Vector2f(mx - pos.x, my - pos.y);
}

void EditorDragMove(float mx, float my)
{
	if (!g_editor.dragging) {
		return;
	}
	ApplyElementPosition(g_editor.dragging,
		mx - g_editor.grab_offset.x, my - g_editor.grab_offset.y);
}

void EditorEndDrag()
{
	if (g_editor.dragging) {
		g_editor.dragging = nullptr;
		SaveLayout();
	}
}

/* ---------------- editor: mode switching ---------------- */

void EditorEnter()
{
	if (!g_hud.initialized || !g_hud.context || !g_hud.document) {
		Com_Printf("RmlUI HUD: editor needs the HUD active (hud_newhudeditor 1 + a map)\n");
		return;
	}
	Cvar_SetValue(&hud_newhudeditor, 2);
	key_dest = key_hudeditor;
	g_hud.document->SetClass("edit", true);
	Com_Printf("RmlUI HUD: edit mode ON - drag elements with the mouse, ESC to leave\n");
}

void EditorExit()
{
	EditorEndDrag();
	if (g_hud.document) {
		g_hud.document->SetClass("edit", false);
	}
	SaveLayout();
	Cvar_SetValue(&hud_newhudeditor, 1);
	key_dest = key_game;
	Com_Printf("RmlUI HUD: edit mode OFF (layout saved to %s)\n", LAYOUT_FILE);
}

void StylePickerOpen()
{
	if (!g_hud.initialized || !g_hud.context || !g_hud.document) {
		Com_Printf("RmlUI HUD: style picker needs the HUD active (hud_newhudeditor 1 + a map)\n");
		return;
	}
	ezquake::rmlui::GameDataOpenStylePicker();
	g_editor.picker_open = true;
	key_dest = key_hudeditor;
	Com_Printf("RmlUI HUD: style picker - hover previews live, click applies, ESC closes\n");
}

void StylePickerClose()
{
	ezquake::rmlui::GameDataCloseStylePicker();
	g_editor.picker_open = false;
	if (!EditorModeEnabled()) {
		key_dest = key_game;
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

static void HUD_RmlUi_Edit_f(void)
{
	if (EditorModeEnabled()) {
		EditorExit();
	}
	else {
		if (!RmlModeEnabled()) {
			Cvar_SetValue(&hud_newhudeditor, 1);
		}
		EditorEnter();
	}
}

static void HUD_RmlUi_Style_f(void)
{
	if (g_editor.picker_open) {
		StylePickerClose();
	}
	else {
		if (!RmlModeEnabled()) {
			Cvar_SetValue(&hud_newhudeditor, 1);
		}
		StylePickerOpen();
	}
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

	/* Register in the HUD cvar group so cfg_save persists the player's
	 * style/document choice into their config (ungrouped cvars are not
	 * dumped). hud_newhudeditor itself is grouped in hud.c. */
	Cvar_SetCurrentGroup(CVAR_GROUP_HUD);
	Cvar_Register(&hud_newhudeditor_doc);
	Cvar_Register(&hud_newhudeditor_iconset);
	Cvar_ResetCurrentGroup();

	Cmd_AddCommand("hud_newhudeditor_reload", HUD_RmlUi_Reload_f);
	Cmd_AddCommand("hud_newhudeditor_edit", HUD_RmlUi_Edit_f);
	Cmd_AddCommand("hud_newhudeditor_style", HUD_RmlUi_Style_f);

	/* A saved value of 2 (edit mode) should not resurrect the editor on
	 * startup - the input routing is not set up here. Treat it as on. */
	if (hud_newhudeditor.integer == 2) {
		Cvar_SetValue(&hud_newhudeditor, 1);
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

int HUD_RmlUi_InEditorMode(void)
{
	return (g_hud.initialized && (EditorModeEnabled() || g_editor.picker_open)) ? 1 : 0;
}

void HUD_RmlUi_MouseEvent(void* mouse_state)
{
	mouse_state_t* ms = (mouse_state_t*)mouse_state;
	if (!ms || !g_hud.initialized || !g_hud.context ||
		!(EditorModeEnabled() || g_editor.picker_open)) {
		return;
	}

	const float mx = (float)ms->x;
	const float my = (float)ms->y;

	/* Feed the context first so hover state is current for the drag and
	 * the picker's mouseover/click events fire. */
	g_hud.context->ProcessMouseMove((int)mx, (int)my, 0);

	/* Dragging is an edit-mode gesture; while the style picker is open
	 * clicks belong to its cards (handled by the event listener). */
	const bool allow_drag = EditorModeEnabled() && !g_editor.picker_open;

	if (ms->button_down >= 1 && ms->button_down <= 3) {
		g_hud.context->ProcessMouseButtonDown(ms->button_down - 1, 0);
		if (allow_drag && ms->button_down == 1) {
			EditorStartDrag(mx, my);
		}
	}
	else if (ms->button_up >= 1 && ms->button_up <= 3) {
		g_hud.context->ProcessMouseButtonUp(ms->button_up - 1, 0);
		if (ms->button_up == 1) {
			EditorEndDrag();
		}
	}
	else {
		EditorDragMove(mx, my);
	}
}

void HUD_RmlUi_EditorKey(int key, int unichar, int down)
{
	(void)unichar;
	/* Active for both the drag editor and the (mode-1) style picker. */
	if (!g_hud.initialized || !g_hud.context ||
		!(EditorModeEnabled() || g_editor.picker_open)) {
		return;
	}
	if (!down) {
		return;
	}

	switch (key) {
		case K_ESCAPE:
			if (g_editor.picker_open) {
				StylePickerClose();
			}
			else {
				EditorExit();
			}
			break;
		case K_MWHEELUP:
			g_hud.context->ProcessMouseWheel(Rml::Vector2f(0.f, -1.f), 0);
			break;
		case K_MWHEELDOWN:
			g_hud.context->ProcessMouseWheel(Rml::Vector2f(0.f, 1.f), 0);
			break;
		default:
			break;
	}
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
