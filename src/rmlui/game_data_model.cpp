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

#include <cmath>

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

namespace {

struct PlayerRow {
	Rml::String name;
	Rml::String team;
	int frags = 0;
	int ping = 0;
	int pl = 0;
	bool spectator = false;
	int topcolor = 0;
	int bottomcolor = 0;
};

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

	// -- scoreboard (connected, non-spectating players) --
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
		m.players.push_back(row);
	}
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

	if (auto row = constructor.RegisterStruct<PlayerRow>()) {
		row.RegisterMember("name", &PlayerRow::name);
		row.RegisterMember("team", &PlayerRow::team);
		row.RegisterMember("frags", &PlayerRow::frags);
		row.RegisterMember("ping", &PlayerRow::ping);
		row.RegisterMember("pl", &PlayerRow::pl);
		row.RegisterMember("spectator", &PlayerRow::spectator);
		row.RegisterMember("topcolor", &PlayerRow::topcolor);
		row.RegisterMember("bottomcolor", &PlayerRow::bottomcolor);
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
	// events
	constructor.RegisterArray<Rml::Vector<Rml::String>>();
	constructor.Bind("centerprint", &data.centerprint);
	constructor.Bind("centerprint_visible", &data.centerprint_visible);
	constructor.Bind("notify_lines", &data.notify_lines);
	// scoreboard
	constructor.Bind("showscores", &data.showscores);
	constructor.Bind("showteamscores", &data.showteamscores);
	constructor.Bind("players", &data.players);

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

} // namespace rmlui
} // namespace ezquake
