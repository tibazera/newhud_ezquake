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
#include <map>
#include <string>
#include <cmath>

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
	Rml::Element* resizing = nullptr;   // widget being scaled by its corner grip
	float resize_start_scale = 1.0f;    // scale when the resize gesture began
	Rml::Vector2f resize_anchor;        // widget top-left (layout offset), stable
	float resize_start_dist = 1.0f;     // anchor->mouse distance at grab time
	bool picker_open = false;  // visual iconset picker overlay
};
EditorState g_editor;

/* Per-widget scale factor (id -> scale), applied as transform: scale() from the
 * top-left corner. Populated from the layout file, edited by the corner-grip
 * resize gesture, persisted per document. Reset when a new document loads. */
std::map<std::string, float> g_widget_scale;

float WidgetScale(const Rml::String& id)
{
	auto it = g_widget_scale.find(id.c_str());
	return (it != g_widget_scale.end()) ? it->second : 1.0f;
}

/* Layout-preset switch requested from the picker; applied at the start of the
 * next Frame() so the current document is not unloaded mid event-dispatch. */
Rml::String g_pending_doc;

void StylePickerClose();      // defined below
void LoadHudDocument();       // defined below
void StylePickerOpen();       // defined below
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
				/* Layout preset row: switch the HUD document. Deferred to
				 * the next frame so we never unload this document while its
				 * own click event is still being dispatched. */
				if (el->HasAttribute("setdoc")) {
					const Rml::String doc = el->GetAttribute<Rml::String>("setdoc", "");
					if (!doc.empty()) {
						g_pending_doc = doc;
					}
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
	/* Used by the Brutalist/Visor layouts (vkQuake ui_lab palette/type). */
	Rml::LoadFontFace("ui/fonts/SpaceGrotesk-Bold.ttf");
	Rml::LoadFontFace("ui/fonts/SpaceMono-Regular.ttf");
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
	/* Per-widget scales are per document; LoadLayout repopulates from the new
	 * document's layout file. */
	g_widget_scale.clear();

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

/* Layout is saved PER DOCUMENT so the Classic and Competitive layouts keep
 * independent element positions (they share widget ids but want different
 * placements). Derived from the document basename, e.g.
 * "ui/rml/hud/hud_print.rml" -> "rmlui_layout_hud_print.cfg". */
const char* LayoutFile()
{
	static char name[MAX_OSPATH];
	const char* doc = hud_newhudeditor_doc.string ? hud_newhudeditor_doc.string : "hud";
	const char* slash = strrchr(doc, '/');
	const char* base = slash ? slash + 1 : doc;
	char stem[64];
	size_t i = 0;
	for (; base[i] && base[i] != '.' && i + 1 < sizeof(stem); ++i) {
		stem[i] = base[i];
	}
	stem[i] = '\0';
	snprintf(name, sizeof(name), "rmlui_layout_%s.cfg", stem[0] ? stem : "hud");
	return name;
}

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
	/* Some widgets center via transform: translateX(-50%); once pinned to
	 * absolute px that must be cleared. Reuse the transform to carry the
	 * per-widget resize scale (default 1 = no visible change), anchored to the
	 * top-left so scaling grows toward the bottom-right (where the grip is). */
	const float s = WidgetScale(element->GetId());
	if (s > 1.001f || s < 0.999f) {
		char t[32];
		snprintf(t, sizeof(t), "scale(%.4f)", s);
		element->SetProperty("transform", t);
		element->SetProperty("transform-origin", "0px 0px");
	}
	else {
		element->SetProperty("transform", "none");
	}
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
	/* Scale lines first, so LoadLayout has the factor before it pins each
	 * position (ApplyElementPosition reads WidgetScale to emit the transform). */
	for (int i = 0; i < g_hud.document->GetNumChildren(); ++i) {
		Rml::Element* child = g_hud.document->GetChild(i);
		const Rml::String& id = child->GetId();
		if (!IsWidgetId(id)) {
			continue;
		}
		const float s = WidgetScale(id);
		if (s > 1.001f || s < 0.999f) {
			char line[128];
			snprintf(line, sizeof(line), "scale %s %.4f\n", id.c_str(), s);
			out += line;
		}
	}
	for (int i = 0; i < g_hud.document->GetNumChildren(); ++i) {
		Rml::Element* child = g_hud.document->GetChild(i);
		const Rml::String& id = child->GetId();
		if (!IsWidgetId(id)) {
			continue;
		}
		const Rml::Vector2f pos = child->GetAbsoluteOffset(Rml::BoxArea::Border);
		/* GetAbsoluteOffset returns density-independent px (dp); the context is
		 * sized in physical px. Convert dp -> physical px (dp * dp_ratio, the
		 * same ratio fed to SetDensityIndependentPixelRatio) BEFORE taking the
		 * fraction, otherwise the fraction is inflated by 1/dp_ratio and can
		 * exceed 1, pinning widgets off-screen. Store as fractions of the
		 * context so the layout survives any resolution / vid_conwidth. */
		const float dp_ratio = (g_hud.height > 0) ? (g_hud.height / 480.0f) : 1.0f;
		float fx = (g_hud.width > 0) ? (pos.x * dp_ratio) / (float)g_hud.width : 0.0f;
		float fy = (g_hud.height > 0) ? (pos.y * dp_ratio) / (float)g_hud.height : 0.0f;
		/* Keep widgets on-screen even if dragged past an edge. */
		fx = (fx < 0.0f) ? 0.0f : (fx > 1.0f ? 1.0f : fx);
		fy = (fy < 0.0f) ? 0.0f : (fy > 1.0f ? 1.0f : fy);
		char line[128];
		snprintf(line, sizeof(line), "pos %s %.5f %.5f\n", id.c_str(), fx, fy);
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

	vfsfile_t* f = FS_OpenVFS(LayoutFile(), (char*)"wb", FS_GAME_OS);
	if (!f) {
		Com_Printf("RmlUI HUD: WARNING could not write %s\n", LayoutFile());
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

	vfsfile_t* f = FS_OpenVFS(LayoutFile(), (char*)"rb", FS_ANY);
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
		float fx = 0.0f, fy = 0.0f;
		float fscale = 1.0f;
		if (sscanf(line.c_str(), "scale %63s %f", id, &fscale) == 2) {
			/* Populated before the pos lines; clamp to the resize range. */
			if (fscale >= 0.4f && fscale <= 4.0f) {
				g_widget_scale[id] = fscale;
			}
		}
		else if (sscanf(line.c_str(), "pos %63s %f %f", id, &fx, &fy) == 3) {
			/* Fractions of the context. Legacy files stored absolute pixels
			 * (values > 1); ignore those so they can't pin widgets off a
			 * smaller screen - the element keeps its stylesheet position. */
			if (fx >= 0.0f && fx <= 1.0f && fy >= 0.0f && fy <= 1.0f) {
				if (Rml::Element* el = g_hud.document->GetElementById(id)) {
					ApplyElementPosition(el, fx * (float)g_hud.width, fy * (float)g_hud.height);
					++applied;
				}
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

/* ---------------- editor: resizing (corner grip) ---------------- */

/* True if the element, or any ancestor up to the widget, is the resize grip.
 * Detecting the grip element (rather than a geometric corner zone) keeps this
 * independent of the dp/px unit ambiguity in the editor's coordinate handling. */
bool IsOnResizeGrip(Rml::Element* el)
{
	for (Rml::Element* e = el; e; e = e->GetParentNode()) {
		if (e->IsClassSet("rgrip")) {
			return true;
		}
	}
	return false;
}

void EditorStartResize(Rml::Element* w, float mx, float my)
{
	/* Freeze the current position first so the top-left anchor is stable while
	 * scaling (same reason the drag freezes it). */
	const Rml::Vector2f pos = w->GetAbsoluteOffset(Rml::BoxArea::Border);
	g_editor.resizing = w;
	g_editor.resize_start_scale = WidgetScale(w->GetId());
	g_editor.resize_anchor = pos;
	const float dx = mx - pos.x, dy = my - pos.y;
	float dist = std::sqrt(dx * dx + dy * dy);
	g_editor.resize_start_dist = (dist < 1.0f) ? 1.0f : dist;
	ApplyElementPosition(w, pos.x, pos.y);
}

void EditorResizeMove(float mx, float my)
{
	if (!g_editor.resizing) {
		return;
	}
	const float dx = mx - g_editor.resize_anchor.x, dy = my - g_editor.resize_anchor.y;
	const float dist = std::sqrt(dx * dx + dy * dy);
	float s = g_editor.resize_start_scale * (dist / g_editor.resize_start_dist);
	if (s < 0.4f) s = 0.4f;
	if (s > 4.0f) s = 4.0f;
	g_widget_scale[g_editor.resizing->GetId().c_str()] = s;
	/* Re-pin at the same anchor; ApplyElementPosition now emits scale(s). */
	ApplyElementPosition(g_editor.resizing, g_editor.resize_anchor.x, g_editor.resize_anchor.y);
}

void EditorEndResize()
{
	if (g_editor.resizing) {
		g_editor.resizing = nullptr;
		SaveLayout();
	}
}

/* Decide the gesture under the cursor: corner grip -> resize, body -> drag. */
void EditorStartGesture(float mx, float my)
{
	Rml::Element* hover = g_hud.context->GetHoverElement();
	Rml::Element* top = TopLevelFor(hover);
	if (!top || !IsWidgetId(top->GetId())) {
		return;
	}
	if (IsOnResizeGrip(hover)) {
		EditorStartResize(top, mx, my);
	}
	else {
		EditorStartDrag(mx, my);
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
	ezquake::rmlui::GameDataSetEditMode(true); // drives the edit panels via data-if
	Com_Printf("RmlUI HUD: edit mode ON - drag elements with the mouse, ESC to leave\n");
}

void EditorExit()
{
	EditorEndDrag();
	EditorEndResize();
	if (g_hud.document) {
		g_hud.document->SetClass("edit", false);
	}
	ezquake::rmlui::GameDataSetEditMode(false);
	SaveLayout();
	Cvar_SetValue(&hud_newhudeditor, 1);
	key_dest = key_game;
	Com_Printf("RmlUI HUD: edit mode OFF (layout saved to %s)\n", LayoutFile());
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
		/* Density-independent px: author the HUD against a 480-tall
		 * reference so dp sizes stay physically consistent across any
		 * conheight the player's cfg sets. */
		g_hud.context->SetDensityIndependentPixelRatio(g_hud.height / 480.0f);
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

/* Recovery: wipe the current layout's saved positions, show every widget,
 * and reload so the HUD returns to its stylesheet defaults. */
static void HUD_RmlUi_Reset_f(void)
{
	if (!g_hud.initialized || !g_hud.context) {
		Com_Printf("RmlUI HUD: not active (enable hud_newhudeditor 1 in a game)\n");
		return;
	}
	/* Truncate the per-document layout file (opening "wb" empties it). */
	if (vfsfile_t* f = FS_OpenVFS(LayoutFile(), (char*)"wb", FS_GAME_OS)) {
		VFS_CLOSE(f);
	}
	const int n = ezquake::rmlui::GameDataWidgetCount();
	for (int i = 0; i < n; ++i) {
		ezquake::rmlui::GameDataSetWidgetHidden(ezquake::rmlui::GameDataWidgetIdAt(i), false);
	}
	LoadHudDocument();
	Com_Printf("RmlUI HUD: layout reset to defaults (%s)\n", LayoutFile());
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
	Cmd_AddCommand("hud_newhudeditor_reset", HUD_RmlUi_Reset_f);
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

	/* Apply a layout-preset switch requested from the picker last frame.
	 * The model (edit/picker state, iconsets) persists across the reload, so
	 * the new document comes up showing whatever overlay was open. */
	if (!g_pending_doc.empty()) {
		if (g_pending_doc != (hud_newhudeditor_doc.string ? hud_newhudeditor_doc.string : "")) {
			Cvar_Set(&hud_newhudeditor_doc, (char*)g_pending_doc.c_str());
			LoadHudDocument();
		}
		g_pending_doc.clear();
	}

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
		g_hud.context->SetDensityIndependentPixelRatio(height / 480.0f);
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
			/* Corner grip -> resize, widget body -> drag. */
			EditorStartGesture(mx, my);
		}
	}
	else if (ms->button_up >= 1 && ms->button_up <= 3) {
		g_hud.context->ProcessMouseButtonUp(ms->button_up - 1, 0);
		if (ms->button_up == 1) {
			EditorEndDrag();
			EditorEndResize();
		}
	}
	else if (g_editor.resizing) {
		EditorResizeMove(mx, my);
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
		/* Style picker: arrows move the highlight (live preview), Enter
		 * applies. Player picks a HUD skin without touching the mouse. */
		case K_UPARROW:
		case K_LEFTARROW:
			if (g_editor.picker_open) {
				ezquake::rmlui::GameDataPickerMove(-1);
			}
			break;
		case K_DOWNARROW:
		case K_RIGHTARROW:
			if (g_editor.picker_open) {
				ezquake::rmlui::GameDataPickerMove(+1);
			}
			break;
		case K_ENTER:
		case KP_ENTER:
			if (g_editor.picker_open) {
				ezquake::rmlui::GameDataPickerApplySelected();
				StylePickerClose();
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
