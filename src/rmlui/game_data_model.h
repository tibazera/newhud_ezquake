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

} // namespace rmlui
} // namespace ezquake

#endif /* EZQUAKE_RMLUI_GAME_DATA_MODEL_H */
