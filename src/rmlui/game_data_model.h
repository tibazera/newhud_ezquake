/*
 * GameDataModel - the "hud" RmlUi data model.
 *
 * Exposes the full game-state contract to RML documents (fields modelled on
 * the classic HUD and the qw-webhud PROTOCOL.md checklist). Reads engine
 * globals (cl/cls/cvars) directly and dirties only the variables that
 * actually changed each frame.
 */

#ifndef EZQUAKE_RMLUI_GAME_DATA_MODEL_H
#define EZQUAKE_RMLUI_GAME_DATA_MODEL_H

namespace Rml {
class Context;
}

namespace ezquake {
namespace rmlui {

// Create the "hud" data model on the context. Returns false on failure.
bool GameDataCreate(Rml::Context* context);

// Read the engine state and dirty the changed model variables.
// Call once per frame, before Context::Update().
void GameDataSync();

// Forget the model handle (context destroyed).
void GameDataReset();

// Centerprint push hooks (called from the engine via the C bridge).
void GameDataCenterPrint(const char* str);
void GameDataCenterPrintClear();

// Visual iconset picker: opens (scanning hudpacks/ for available sets),
// closes, and drives the live full-HUD preview while hovering a card.
void GameDataOpenStylePicker();
void GameDataCloseStylePicker();
bool GameDataStylePickerOpen();
void GameDataPreviewIconset(const char* path); // hover: temporary override
void GameDataEndPreviewIconset();              // hover out: back to the cvar

// Keyboard navigation of the style picker: move the highlighted set
// (dir -1/+1, live preview) and apply the highlighted set to the cvar.
void GameDataPickerMove(int dir);
void GameDataPickerApplySelected();

// Edit-mode flag exposed to the model so panels show via data-if
// (more reliable than a document CSS class).
void GameDataSetEditMode(bool on);

// Widget config (edit mode): per-widget show/hide. State persists across
// document reloads (kept outside the bound model).
void GameDataToggleWidget(const char* id);
void GameDataSetWidgetHidden(const char* id, bool hidden);
bool GameDataWidgetHidden(const char* id);
int GameDataWidgetCount();
const char* GameDataWidgetIdAt(int index);

} // namespace rmlui
} // namespace ezquake

#endif /* EZQUAKE_RMLUI_GAME_DATA_MODEL_H */
