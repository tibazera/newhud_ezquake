/*
 * GameDataModel - the "hud" RmlUi data model implementation.
 *
 * Field sources were mapped against the classic HUD (see CONTINUE.md):
 * stats via HUD_Stats()/cl.stats, item/weapon bits in STAT_ITEMS, armor
 * type from IT_ARMOR1/2/3 priority, speed from cl.simvel (XY magnitude),
 * players from cl.players[], match state from cl.gametime/standby/
 * countdown, fps from cls.fps, and the short map name from host_mapname.
 *
 * Only variables whose values changed are dirtied, using a snapshot of
 * the previous frame.
 */

/* RmlUi/STL first: engine macros (min/max) poison <algorithm>. */
#include <RmlUi/Core.h>

#include "game_data_model.h"

#include <algorithm>
#include <cmath>
#include <set>

/* Engine headers inside extern "C"; they assume q_shared.h types. */
extern "C" {
#include "quakedef.h"
#include "console.h"

/* Helpers defined in C translation units. */
int HUD_Stats(int stat_num);
char* SecondsToMinutesString(int print_time);
int Cam_TrackNum(void);
extern cvar_t host_mapname;

/* Console notify timestamps (console.c) - not exported by console.h. */
extern float con_times[16];

/* Scoreboard toggles (sbar.c). */
extern qbool sb_showscores, sb_showteamscores;
}

#include "hud_rmlui.h" // hud_newhudeditor_iconset (self-guarded extern "C")

namespace {

struct IconsetRow {
	Rml::String name;
	Rml::String path;
};

struct WidgetRow {
	Rml::String id;
	Rml::String label;
	bool visible = true;
};

struct PlayerRow {
	Rml::String name;
	Rml::String team;
	int frags = 0;
	int ping = 0;
	int pl = 0;
	bool spectator = false;
	int topcolor = 0;
	int bottomcolor = 0;
	Rml::String tcolor; // "#rrggbb" from the quake palette (top colour)
	Rml::String bcolor; // "#rrggbb" (bottom colour)
};

/* Quake player colour (0..13) -> "#rrggbb", from gfx/palette.lmp
 * (classic Sbar_ColorForMap: palette index colour*16+8). */
Rml::String PlayerColorRGB(int colour)
{
	static byte palette[768];
	static bool loaded = false;
	if (!loaded) {
		vfsfile_t* f = FS_OpenVFS("gfx/palette.lmp", (char*)"rb", FS_ANY);
		if (f) {
			vfserrno_t err = VFSERR_NONE;
			VFS_READ(f, palette, sizeof(palette), &err);
			VFS_CLOSE(f);
		}
		loaded = true;
	}
	if (colour < 0) colour = 0;
	if (colour > 13) colour = 13;
	const int idx = colour * 16 + 8;
	char buf[8];
	snprintf(buf, sizeof(buf), "#%02x%02x%02x",
		palette[idx * 3 + 0], palette[idx * 3 + 1], palette[idx * 3 + 2]);
	return Rml::String(buf);
}

/* All bound scalars. The model binds directly into `data`; `prev` is the
 * last synced snapshot used to dirty only what changed. */
struct HudModel {
	// me
	int health = 0;
	int armor = 0;
	int armor_type = 0;        // 0 none, 1 GA, 2 YA, 3 RA
	int ammo = 0;              // active weapon ammo
	int weapon = 0;            // raw IT_* bit of the active weapon
	int weapon_num = 0;        // 1..8 (1=axe), 0 = none
	Rml::String weapon_label;  // "AXE", "SG", ..., "LG"
	bool has_axe = false;
	bool has_shotgun = false;
	bool has_super_shotgun = false;
	bool has_nailgun = false;
	bool has_super_nailgun = false;
	bool has_grenade_launcher = false;
	bool has_rocket_launcher = false;
	bool has_lightning = false;
	int shells = 0;
	int nails = 0;
	int rockets = 0;
	int cells = 0;
	int items = 0;             // raw STAT_ITEMS bitmask
	bool quad = false;
	bool pent = false;
	bool ring = false;
	bool suit = false;
	bool key1 = false;
	bool key2 = false;
	bool sigil1 = false;
	bool sigil2 = false;
	bool sigil3 = false;
	bool sigil4 = false;
	Rml::String face_icon;     // classic face lump for current state (face1..face5, face_quad, ...)
	Rml::String ammo_icon;     // ammo-box lump for the active weapon (sb_shells...)
	// Classic LCD digit lumps (num_*/anum_* when low); "" = digit hidden.
	Rml::String health_d100, health_d10, health_d1;
	Rml::String armor_d100, armor_d10, armor_d1;
	Rml::String ammo_d100, ammo_d10, ammo_d1;
	Rml::String name;
	Rml::String team;
	int frags = 0;
	int ping = 0;              // per-player scoreboard ping
	int pl = 0;                // packet loss %
	int speed = 0;             // XY velocity magnitude
	// match
	Rml::String map;           // short name (dm3)
	Rml::String map_title;     // descriptive name (cl.levelname)
	Rml::String match_time;    // MM:SS game clock
	double time = 0.0;         // raw cl.time seconds
	int gametype = 0;
	int teamplay = 0;
	int deathmatch = 0;
	int timelimit = 0;
	int fraglimit = 0;
	bool standby = false;
	bool countdown = false;
	int intermission = 0;
	bool paused = false;
	// client
	int fps = 0;
	bool spectator = false;
	bool demo_playback = false;
	int mvd = 0;               // 0 no, 1 MVD, 2 QTV
	// presentation
	Rml::String iconpath;      // icon lookup prefix (hud_newhudeditor_iconset)
	bool stylepicker = false;  // visual iconset picker overlay
	Rml::Vector<IconsetRow> iconsets;
	Rml::Vector<WidgetRow> widgets; // edit-mode show/hide list
	// events
	Rml::String centerprint;   // current centerprint text ('\n' separated)
	bool centerprint_visible = false;
	Rml::Vector<Rml::String> notify_lines;
	// scoreboard
	bool showscores = false;
	bool showteamscores = false;
	Rml::Vector<PlayerRow> players;
};

HudModel data;
HudModel prev;
Rml::DataModelHandle model_handle;
bool model_ready = false;

/* Centerprint state pushed by the engine hook (survives model recreation). */
Rml::String cp_text;
double cp_stamp = -1.0; // cl.time when received; < 0 = none

/* Style-picker hover preview: overrides the cvar-driven iconpath. */
Rml::String preview_path;
bool preview_active = false;

/* Widget catalog (id + label) and persistent hidden set. */
struct WidgetDef { const char* id; const char* label; };
const WidgetDef WIDGET_DEFS[] = {
	{"w_face",       "Rosto"},
	{"w_health",     "Vida"},
	{"w_armor",      "Armadura"},
	{"w_ammo",       "Munição (arma ativa)"},
	{"w_ammocounts", "Munição (por tipo)"},
	{"w_weapons",    "Barra de armas"},
	{"w_items",      "Itens / Powerups"},
	{"w_frags",      "Frags"},
	{"w_speed",      "Velocidade"},
	{"w_clock",      "Relógio"},
	{"w_fps",        "FPS / Ping"},
	{"w_notify",     "Mensagens (mortes / chat)"},
	{"w_centerprint","Avisos centrais"},
};
const int WIDGET_COUNT = (int)(sizeof(WIDGET_DEFS) / sizeof(WIDGET_DEFS[0]));

std::set<Rml::String> g_hidden_widgets;

void RebuildWidgetRows()
{
	data.widgets.clear();
	for (int i = 0; i < WIDGET_COUNT; ++i) {
		WidgetRow row;
		row.id = WIDGET_DEFS[i].id;
		row.label = WIDGET_DEFS[i].label;
		row.visible = g_hidden_widgets.find(row.id) == g_hidden_widgets.end();
		data.widgets.push_back(row);
	}
}

const char* WeaponLabel(int weapon_num)
{
	switch (weapon_num) {
		case 1: return "AXE";
		case 2: return "SG";
		case 3: return "SSG";
		case 4: return "NG";
		case 5: return "SNG";
		case 6: return "GL";
		case 7: return "RL";
		case 8: return "LG";
		default: return "";
	}
}

int WeaponNumFromBit(int active_weapon)
{
	if (active_weapon == IT_AXE) {
		return 1;
	}
	for (int i = 0; i < 7; ++i) {
		if (active_weapon == (IT_SHOTGUN << i)) {
			return 2 + i;
		}
	}
	return 0;
}

int ArmorType(int stat_items)
{
	// Priority mirrors the classic HUD (hud_armor.c): RA > YA > GA.
	if (stat_items & IT_ARMOR3) return 3;
	if (stat_items & IT_ARMOR2) return 2;
	if (stat_items & IT_ARMOR1) return 1;
	return 0;
}

/* Classic face selection (sbar.c): powerup faces first, then one of
 * face1 (healthy) .. face5 (near death) by health/20. Pain variants
 * (face_pN, need last-damage timing) are a later refinement. */
const char* FaceIcon(int health, int stat_items)
{
	if ((stat_items & IT_INVISIBILITY) && (stat_items & IT_INVULNERABILITY)) {
		return "face_inv2";
	}
	if (stat_items & IT_INVULNERABILITY) {
		return "face_invul2";
	}
	if (stat_items & IT_INVISIBILITY) {
		return "face_invis";
	}
	if (stat_items & IT_QUAD) {
		return "face_quad";
	}
	int f = health / 20;
	if (f < 0) f = 0;
	if (f > 4) f = 4;
	static const char* faces[5] = {"face5", "face4", "face3", "face2", "face1"};
	return faces[f];
}

/* Classic sbar LCD digits: up to 3 digits, white num_* normally and the
 * red/gold anum_* variant when the value is low, exactly like Sbar_DrawNum. */
void SetDigits(int value, bool low,
	Rml::String& d100, Rml::String& d10, Rml::String& d1)
{
	if (value < 0) value = 0;
	if (value > 999) value = 999;
	const char* set = low ? "anum_" : "num_";

	d100 = (value >= 100) ? (Rml::String(set) + char('0' + (value / 100) % 10)) : "";
	d10 = (value >= 10) ? (Rml::String(set) + char('0' + (value / 10) % 10)) : "";
	d1 = Rml::String(set) + char('0' + value % 10);
}

/* Ammo box icon for the active weapon's ammo type. */
const char* AmmoIcon(int weapon_num)
{
	switch (weapon_num) {
		case 2: case 3: return "sb_shells";
		case 4: case 5: return "sb_nails";
		case 6: case 7: return "sb_rocket";
		case 8: return "sb_cells";
		default: return "";
	}
}

/* The player slot shown on screen: self, or the tracked player when
 * spectating/chasecam (same idiom as hud_speed.c / cl_view.c). */
int DisplayedPlayerSlot()
{
	if (cl.spectator) {
		int tracked = Cam_TrackNum();
		if (tracked >= 0) {
			return tracked;
		}
	}
	return cl.playernum;
}

void ReadEngineState(HudModel& m)
{
	const int stat_items = HUD_Stats(STAT_ITEMS);
	const int active_weapon = HUD_Stats(STAT_ACTIVEWEAPON);
	const int slot = DisplayedPlayerSlot();

	// -- me --
	m.health = HUD_Stats(STAT_HEALTH);
	m.armor = HUD_Stats(STAT_ARMOR);
	m.armor_type = ArmorType(stat_items);
	m.ammo = HUD_Stats(STAT_AMMO);
	m.weapon = active_weapon;
	m.weapon_num = WeaponNumFromBit(active_weapon);
	m.weapon_label = WeaponLabel(m.weapon_num);
	m.has_axe = (stat_items & IT_AXE) != 0;
	m.has_shotgun = (stat_items & IT_SHOTGUN) != 0;
	m.has_super_shotgun = (stat_items & IT_SUPER_SHOTGUN) != 0;
	m.has_nailgun = (stat_items & IT_NAILGUN) != 0;
	m.has_super_nailgun = (stat_items & IT_SUPER_NAILGUN) != 0;
	m.has_grenade_launcher = (stat_items & IT_GRENADE_LAUNCHER) != 0;
	m.has_rocket_launcher = (stat_items & IT_ROCKET_LAUNCHER) != 0;
	m.has_lightning = (stat_items & IT_LIGHTNING) != 0;
	m.shells = HUD_Stats(STAT_SHELLS);
	m.nails = HUD_Stats(STAT_NAILS);
	m.rockets = HUD_Stats(STAT_ROCKETS);
	m.cells = HUD_Stats(STAT_CELLS);
	m.items = stat_items;
	m.quad = (stat_items & IT_QUAD) != 0;
	m.pent = (stat_items & IT_INVULNERABILITY) != 0;
	m.ring = (stat_items & IT_INVISIBILITY) != 0;
	m.suit = (stat_items & IT_SUIT) != 0;
	m.key1 = (stat_items & IT_KEY1) != 0;
	m.key2 = (stat_items & IT_KEY2) != 0;
	m.sigil1 = (stat_items & IT_SIGIL1) != 0;
	m.sigil2 = (stat_items & IT_SIGIL2) != 0;
	m.sigil3 = (stat_items & IT_SIGIL3) != 0;
	m.sigil4 = (stat_items & IT_SIGIL4) != 0;
	m.face_icon = FaceIcon(m.health, stat_items);
	m.ammo_icon = AmmoIcon(m.weapon_num);
	SetDigits(m.health, m.health <= 25, m.health_d100, m.health_d10, m.health_d1);
	SetDigits(m.armor, m.armor <= 25, m.armor_d100, m.armor_d10, m.armor_d1);
	SetDigits(m.ammo, m.ammo <= 10, m.ammo_d100, m.ammo_d10, m.ammo_d1);

	if (slot >= 0 && slot < MAX_CLIENTS) {
		const player_info_t& info = cl.players[slot];
		m.name = info.name;
		m.team = info.team;
		m.frags = info.frags;
		m.ping = info.ping;
		m.pl = info.pl;
	}
	m.speed = (int)std::sqrt(cl.simvel[0] * cl.simvel[0] + cl.simvel[1] * cl.simvel[1]);

	// -- match --
	m.map = host_mapname.string ? host_mapname.string : "";
	m.map_title = cl.levelname;
	m.time = cl.time;
	m.gametype = cl.gametype;
	m.teamplay = cl.teamplay;
	m.deathmatch = cl.deathmatch;
	m.timelimit = cl.timelimit;
	m.fraglimit = cl.fraglimit;
	m.standby = cl.standby != 0;
	m.countdown = cl.countdown != 0;
	m.intermission = cl.intermission;
	m.paused = cl.paused != 0;

	// Game clock, mirroring hud_clock.c: fixed timelimit while waiting,
	// remaining/elapsed time once running (SecondsToMinutesString has a
	// static buffer - copied into the Rml::String immediately).
	{
		int seconds;
		if (cl.countdown || cl.standby) {
			seconds = 60 * cl.timelimit;
		}
		else {
			seconds = (int)std::fabs(60 * cl.timelimit - cl.gametime);
		}
		m.match_time = SecondsToMinutesString(seconds);
	}

	// -- client --
	m.fps = (int)(cls.fps + 0.25);
	m.spectator = cl.spectator != 0;
	m.demo_playback = cls.demoplayback != 0;
	m.mvd = cls.mvdplayback;

	// -- events: centerprint (classic semantics: scr_centertime seconds,
	// kept visible during intermission - hud_centerprint.c) --
	{
		static cvar_t* centertime = nullptr;
		if (!centertime) {
			centertime = Cvar_Find("scr_centertime");
		}
		const double duration = centertime ? centertime->value : 2.0;
		m.centerprint = cp_text;
		m.centerprint_visible = cp_stamp >= 0.0 && !cp_text.empty() &&
			(cl.intermission || (cl.time - cp_stamp) < duration);
	}

	// -- events: console notify lines (mirrors SCR_DrawNotify's scan of
	// con_times against con_notifytime / _con_notifylines) --
	{
		static cvar_t* notifytime = nullptr;
		static cvar_t* notifylines = nullptr;
		if (!notifytime) {
			notifytime = Cvar_Find("con_notifytime");
		}
		if (!notifylines) {
			notifylines = Cvar_Find("_con_notifylines");
		}
		const float timeout = notifytime ? notifytime->value : 3.0f;
		int rows = notifylines ? notifylines->integer : 4;
		if (rows < 0) rows = 0;
		if (rows > 16) rows = 16;

		m.notify_lines.clear();
		if (con.text && con_totallines > 0 && con_linewidth > 0) {
			for (int i = con.current - rows + 1; i <= con.current; ++i) {
				if (i < 0) {
					continue;
				}
				const float stamp = con_times[i % 16];
				if (stamp == 0.0f || cls.realtime - stamp > timeout) {
					continue;
				}

				const wchar* row = con.text + (i % con_totallines) * con_linewidth;
				int len = con_linewidth;
				while (len > 0 && (row[len - 1] & 0xFF) == ' ') {
					--len;
				}
				Rml::String line;
				line.reserve(len);
				for (int c = 0; c < len; ++c) {
					// Quake console glyphs: 128+ are the "brown" variants of
					// the same character; map to readable ASCII.
					int ch = row[c] & 0xFF;
					if (ch >= 128 + 32) {
						ch -= 128;
					}
					line += (ch >= 32 && ch < 127) ? static_cast<char>(ch) : ' ';
				}
				if (!line.empty()) {
					m.notify_lines.push_back(line);
				}
			}
		}
	}

	// -- scoreboard toggles (+showscores / +showteamscores) --
	m.showscores = sb_showscores != 0;
	m.showteamscores = sb_showteamscores != 0;

	// -- presentation --
	if (preview_active) {
		m.iconpath = preview_path; // live hover preview wins over the cvar
	}
	else {
		m.iconpath = hud_newhudeditor_iconset.string ? hud_newhudeditor_iconset.string : "";
	}

	// -- scoreboard: players sorted by frags (spectators last) --
	m.players.clear();
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		const player_info_t& info = cl.players[i];
		if (!info.name[0]) {
			continue;
		}
		PlayerRow row;
		row.name = info.name;
		row.team = info.team;
		row.frags = info.frags;
		row.ping = info.ping;
		row.pl = info.pl;
		row.spectator = info.spectator != 0;
		row.topcolor = info.topcolor;
		row.bottomcolor = info.bottomcolor;
		row.tcolor = PlayerColorRGB(info.topcolor);
		row.bcolor = PlayerColorRGB(info.bottomcolor);
		m.players.push_back(row);
	}
	std::sort(m.players.begin(), m.players.end(),
		[](const PlayerRow& a, const PlayerRow& b) {
			if (a.spectator != b.spectator) {
				return !a.spectator; // players before spectators
			}
			if (a.frags != b.frags) {
				return a.frags > b.frags;
			}
			return a.name < b.name;
		});
}

bool PlayersEqual(const Rml::Vector<PlayerRow>& a, const Rml::Vector<PlayerRow>& b)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		const PlayerRow& x = a[i];
		const PlayerRow& y = b[i];
		if (x.frags != y.frags || x.ping != y.ping || x.pl != y.pl ||
			x.spectator != y.spectator || x.topcolor != y.topcolor ||
			x.bottomcolor != y.bottomcolor || x.name != y.name || x.team != y.team) {
			return false;
		}
	}
	return true;
}

template <typename T>
void DirtyIfChanged(const char* variable, const T& current, const T& previous)
{
	if (!(current == previous)) {
		model_handle.DirtyVariable(variable);
	}
}

} // namespace

namespace ezquake {
namespace rmlui {

bool GameDataCreate(Rml::Context* context)
{
	Rml::DataModelConstructor constructor = context->CreateDataModel("hud");
	if (!constructor) {
		return false;
	}

	if (auto row = constructor.RegisterStruct<IconsetRow>()) {
		row.RegisterMember("name", &IconsetRow::name);
		row.RegisterMember("path", &IconsetRow::path);
	}
	constructor.RegisterArray<Rml::Vector<IconsetRow>>();

	if (auto row = constructor.RegisterStruct<WidgetRow>()) {
		row.RegisterMember("id", &WidgetRow::id);
		row.RegisterMember("label", &WidgetRow::label);
		row.RegisterMember("visible", &WidgetRow::visible);
	}
	constructor.RegisterArray<Rml::Vector<WidgetRow>>();

	if (auto row = constructor.RegisterStruct<PlayerRow>()) {
		row.RegisterMember("name", &PlayerRow::name);
		row.RegisterMember("team", &PlayerRow::team);
		row.RegisterMember("frags", &PlayerRow::frags);
		row.RegisterMember("ping", &PlayerRow::ping);
		row.RegisterMember("pl", &PlayerRow::pl);
		row.RegisterMember("spectator", &PlayerRow::spectator);
		row.RegisterMember("topcolor", &PlayerRow::topcolor);
		row.RegisterMember("bottomcolor", &PlayerRow::bottomcolor);
		row.RegisterMember("tcolor", &PlayerRow::tcolor);
		row.RegisterMember("bcolor", &PlayerRow::bcolor);
	}
	constructor.RegisterArray<Rml::Vector<PlayerRow>>();

	// me
	constructor.Bind("health", &data.health);
	constructor.Bind("armor", &data.armor);
	constructor.Bind("armor_type", &data.armor_type);
	constructor.Bind("ammo", &data.ammo);
	constructor.Bind("weapon", &data.weapon);
	constructor.Bind("weapon_num", &data.weapon_num);
	constructor.Bind("weapon_label", &data.weapon_label);
	constructor.Bind("has_axe", &data.has_axe);
	constructor.Bind("has_shotgun", &data.has_shotgun);
	constructor.Bind("has_super_shotgun", &data.has_super_shotgun);
	constructor.Bind("has_nailgun", &data.has_nailgun);
	constructor.Bind("has_super_nailgun", &data.has_super_nailgun);
	constructor.Bind("has_grenade_launcher", &data.has_grenade_launcher);
	constructor.Bind("has_rocket_launcher", &data.has_rocket_launcher);
	constructor.Bind("has_lightning", &data.has_lightning);
	constructor.Bind("shells", &data.shells);
	constructor.Bind("nails", &data.nails);
	constructor.Bind("rockets", &data.rockets);
	constructor.Bind("cells", &data.cells);
	constructor.Bind("items", &data.items);
	constructor.Bind("quad", &data.quad);
	constructor.Bind("pent", &data.pent);
	constructor.Bind("ring", &data.ring);
	constructor.Bind("suit", &data.suit);
	constructor.Bind("key1", &data.key1);
	constructor.Bind("key2", &data.key2);
	constructor.Bind("sigil1", &data.sigil1);
	constructor.Bind("sigil2", &data.sigil2);
	constructor.Bind("sigil3", &data.sigil3);
	constructor.Bind("sigil4", &data.sigil4);
	constructor.Bind("face_icon", &data.face_icon);
	constructor.Bind("ammo_icon", &data.ammo_icon);
	constructor.Bind("health_d100", &data.health_d100);
	constructor.Bind("health_d10", &data.health_d10);
	constructor.Bind("health_d1", &data.health_d1);
	constructor.Bind("armor_d100", &data.armor_d100);
	constructor.Bind("armor_d10", &data.armor_d10);
	constructor.Bind("armor_d1", &data.armor_d1);
	constructor.Bind("ammo_d100", &data.ammo_d100);
	constructor.Bind("ammo_d10", &data.ammo_d10);
	constructor.Bind("ammo_d1", &data.ammo_d1);
	constructor.Bind("name", &data.name);
	constructor.Bind("team", &data.team);
	constructor.Bind("frags", &data.frags);
	constructor.Bind("ping", &data.ping);
	constructor.Bind("pl", &data.pl);
	constructor.Bind("speed", &data.speed);
	// match
	constructor.Bind("map", &data.map);
	constructor.Bind("map_title", &data.map_title);
	constructor.Bind("match_time", &data.match_time);
	constructor.Bind("time", &data.time);
	constructor.Bind("gametype", &data.gametype);
	constructor.Bind("teamplay", &data.teamplay);
	constructor.Bind("deathmatch", &data.deathmatch);
	constructor.Bind("timelimit", &data.timelimit);
	constructor.Bind("fraglimit", &data.fraglimit);
	constructor.Bind("standby", &data.standby);
	constructor.Bind("countdown", &data.countdown);
	constructor.Bind("intermission", &data.intermission);
	constructor.Bind("paused", &data.paused);
	// client
	constructor.Bind("fps", &data.fps);
	constructor.Bind("spectator", &data.spectator);
	constructor.Bind("demo_playback", &data.demo_playback);
	constructor.Bind("mvd", &data.mvd);
	// presentation
	constructor.Bind("iconpath", &data.iconpath);
	constructor.Bind("stylepicker", &data.stylepicker);
	constructor.Bind("iconsets", &data.iconsets);
	constructor.Bind("widgets", &data.widgets);
	// events
	constructor.RegisterArray<Rml::Vector<Rml::String>>();
	constructor.Bind("centerprint", &data.centerprint);
	constructor.Bind("centerprint_visible", &data.centerprint_visible);
	constructor.Bind("notify_lines", &data.notify_lines);
	// scoreboard
	constructor.Bind("showscores", &data.showscores);
	constructor.Bind("showteamscores", &data.showteamscores);
	constructor.Bind("players", &data.players);

	RebuildWidgetRows();

	model_handle = constructor.GetModelHandle();
	model_ready = true;

	/* First sync fills the model; everything starts dirty by creation. */
	ReadEngineState(data);
	prev = data;
	return true;
}

void GameDataSync()
{
	if (!model_ready) {
		return;
	}

	ReadEngineState(data);

	DirtyIfChanged("health", data.health, prev.health);
	DirtyIfChanged("armor", data.armor, prev.armor);
	DirtyIfChanged("armor_type", data.armor_type, prev.armor_type);
	DirtyIfChanged("ammo", data.ammo, prev.ammo);
	DirtyIfChanged("weapon", data.weapon, prev.weapon);
	DirtyIfChanged("weapon_num", data.weapon_num, prev.weapon_num);
	DirtyIfChanged("weapon_label", data.weapon_label, prev.weapon_label);
	DirtyIfChanged("has_axe", data.has_axe, prev.has_axe);
	DirtyIfChanged("has_shotgun", data.has_shotgun, prev.has_shotgun);
	DirtyIfChanged("has_super_shotgun", data.has_super_shotgun, prev.has_super_shotgun);
	DirtyIfChanged("has_nailgun", data.has_nailgun, prev.has_nailgun);
	DirtyIfChanged("has_super_nailgun", data.has_super_nailgun, prev.has_super_nailgun);
	DirtyIfChanged("has_grenade_launcher", data.has_grenade_launcher, prev.has_grenade_launcher);
	DirtyIfChanged("has_rocket_launcher", data.has_rocket_launcher, prev.has_rocket_launcher);
	DirtyIfChanged("has_lightning", data.has_lightning, prev.has_lightning);
	DirtyIfChanged("shells", data.shells, prev.shells);
	DirtyIfChanged("nails", data.nails, prev.nails);
	DirtyIfChanged("rockets", data.rockets, prev.rockets);
	DirtyIfChanged("cells", data.cells, prev.cells);
	DirtyIfChanged("items", data.items, prev.items);
	DirtyIfChanged("quad", data.quad, prev.quad);
	DirtyIfChanged("pent", data.pent, prev.pent);
	DirtyIfChanged("ring", data.ring, prev.ring);
	DirtyIfChanged("suit", data.suit, prev.suit);
	DirtyIfChanged("key1", data.key1, prev.key1);
	DirtyIfChanged("key2", data.key2, prev.key2);
	DirtyIfChanged("sigil1", data.sigil1, prev.sigil1);
	DirtyIfChanged("sigil2", data.sigil2, prev.sigil2);
	DirtyIfChanged("sigil3", data.sigil3, prev.sigil3);
	DirtyIfChanged("sigil4", data.sigil4, prev.sigil4);
	DirtyIfChanged("face_icon", data.face_icon, prev.face_icon);
	DirtyIfChanged("ammo_icon", data.ammo_icon, prev.ammo_icon);
	DirtyIfChanged("health_d100", data.health_d100, prev.health_d100);
	DirtyIfChanged("health_d10", data.health_d10, prev.health_d10);
	DirtyIfChanged("health_d1", data.health_d1, prev.health_d1);
	DirtyIfChanged("armor_d100", data.armor_d100, prev.armor_d100);
	DirtyIfChanged("armor_d10", data.armor_d10, prev.armor_d10);
	DirtyIfChanged("armor_d1", data.armor_d1, prev.armor_d1);
	DirtyIfChanged("ammo_d100", data.ammo_d100, prev.ammo_d100);
	DirtyIfChanged("ammo_d10", data.ammo_d10, prev.ammo_d10);
	DirtyIfChanged("ammo_d1", data.ammo_d1, prev.ammo_d1);
	DirtyIfChanged("name", data.name, prev.name);
	DirtyIfChanged("team", data.team, prev.team);
	DirtyIfChanged("frags", data.frags, prev.frags);
	DirtyIfChanged("ping", data.ping, prev.ping);
	DirtyIfChanged("pl", data.pl, prev.pl);
	DirtyIfChanged("speed", data.speed, prev.speed);
	DirtyIfChanged("map", data.map, prev.map);
	DirtyIfChanged("map_title", data.map_title, prev.map_title);
	DirtyIfChanged("match_time", data.match_time, prev.match_time);
	DirtyIfChanged("time", data.time, prev.time);
	DirtyIfChanged("gametype", data.gametype, prev.gametype);
	DirtyIfChanged("teamplay", data.teamplay, prev.teamplay);
	DirtyIfChanged("deathmatch", data.deathmatch, prev.deathmatch);
	DirtyIfChanged("timelimit", data.timelimit, prev.timelimit);
	DirtyIfChanged("fraglimit", data.fraglimit, prev.fraglimit);
	DirtyIfChanged("standby", data.standby, prev.standby);
	DirtyIfChanged("countdown", data.countdown, prev.countdown);
	DirtyIfChanged("intermission", data.intermission, prev.intermission);
	DirtyIfChanged("paused", data.paused, prev.paused);
	DirtyIfChanged("fps", data.fps, prev.fps);
	DirtyIfChanged("spectator", data.spectator, prev.spectator);
	DirtyIfChanged("demo_playback", data.demo_playback, prev.demo_playback);
	DirtyIfChanged("mvd", data.mvd, prev.mvd);
	DirtyIfChanged("iconpath", data.iconpath, prev.iconpath);
	DirtyIfChanged("centerprint", data.centerprint, prev.centerprint);
	DirtyIfChanged("centerprint_visible", data.centerprint_visible, prev.centerprint_visible);
	DirtyIfChanged("showscores", data.showscores, prev.showscores);
	DirtyIfChanged("showteamscores", data.showteamscores, prev.showteamscores);

	if (data.notify_lines != prev.notify_lines) {
		model_handle.DirtyVariable("notify_lines");
	}

	if (!PlayersEqual(data.players, prev.players)) {
		model_handle.DirtyVariable("players");
	}

	prev = data;
}

void GameDataReset()
{
	model_handle = Rml::DataModelHandle();
	model_ready = false;
}

void GameDataCenterPrint(const char* str)
{
	cp_text = str ? str : "";
	cp_stamp = cl.time;
}

void GameDataCenterPrintClear()
{
	cp_text.clear();
	cp_stamp = -1.0;
}

void GameDataOpenStylePicker()
{
	data.iconsets.clear();
	data.iconsets.push_back({"Clássico", "/ui/rml/hud/icons/"});

	/* Scan <basedir>/<gamedir>/hudpacks/ for installed icon packs. */
	const char* gamedirs[] = {"id1", "qw"};
	for (const char* gd : gamedirs) {
		char path[MAX_OSPATH];
		snprintf(path, sizeof(path), "%s/%s/hudpacks", com_basedir, gd);
		dir_t dir = Sys_listdir(path, ".*", SORT_BY_NAME);
		for (int i = 0; i < dir.numfiles; ++i) {
			if (!dir.files[i].isdir) {
				continue;
			}
			const char* name = dir.files[i].name;
			if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..")) {
				continue;
			}
			bool duplicate = false;
			for (const IconsetRow& row : data.iconsets) {
				if (row.name == name) {
					duplicate = true;
					break;
				}
			}
			if (!duplicate) {
				IconsetRow row;
				row.name = name;
				row.path = Rml::String("/hudpacks/") + name + "/";
				data.iconsets.push_back(row);
			}
		}
	}

	data.stylepicker = true;
	if (model_ready) {
		model_handle.DirtyVariable("iconsets");
		model_handle.DirtyVariable("stylepicker");
	}
}

void GameDataCloseStylePicker()
{
	GameDataEndPreviewIconset();
	data.stylepicker = false;
	if (model_ready) {
		model_handle.DirtyVariable("stylepicker");
	}
}

bool GameDataStylePickerOpen()
{
	return data.stylepicker;
}

void GameDataPreviewIconset(const char* path)
{
	if (!path || !path[0]) {
		return;
	}
	preview_path = path;
	preview_active = true;
	/* iconpath updates (and dirties) on the next Sync. */
}

void GameDataEndPreviewIconset()
{
	preview_active = false;
	preview_path.clear();
}

void GameDataToggleWidget(const char* id)
{
	if (!id) {
		return;
	}
	const Rml::String key = id;
	if (g_hidden_widgets.count(key)) {
		g_hidden_widgets.erase(key);
	}
	else {
		g_hidden_widgets.insert(key);
	}
	RebuildWidgetRows();
	if (model_ready) {
		model_handle.DirtyVariable("widgets");
	}
}

void GameDataSetWidgetHidden(const char* id, bool hidden)
{
	if (!id) {
		return;
	}
	const Rml::String key = id;
	if (hidden) {
		g_hidden_widgets.insert(key);
	}
	else {
		g_hidden_widgets.erase(key);
	}
	RebuildWidgetRows();
	if (model_ready) {
		model_handle.DirtyVariable("widgets");
	}
}

bool GameDataWidgetHidden(const char* id)
{
	return id && g_hidden_widgets.count(id) > 0;
}

int GameDataWidgetCount()
{
	return WIDGET_COUNT;
}

const char* GameDataWidgetIdAt(int index)
{
	return (index >= 0 && index < WIDGET_COUNT) ? WIDGET_DEFS[index].id : "";
}

} // namespace rmlui
} // namespace ezquake
