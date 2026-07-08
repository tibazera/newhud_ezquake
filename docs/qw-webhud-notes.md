# Notes from Xerialen/qw-webhud

Reference repository:

```text
https://github.com/Xerialen/qw-webhud
Local clone: C:\Users\Negociador\Documents\qw-webhud
```

## What Helps This Port

`qw-webhud` is not an embedded HUD. It is an external browser overlay fed by
ezQuake state over UDP/WebSocket. Still, it contains useful design work for our
embedded RmlUI port.

Useful pieces:

| qw-webhud file | Use for this project |
| --- | --- |
| `PROTOCOL.md` | First data contract checklist for `GameDataModel` |
| `src/public/js/qw-constants.js` | QuakeWorld weapon/item constants and derivations |
| `src/public/js/elements.js` | Initial element catalog: health, armor, ammo, weapons, teaminfo, killfeed |
| `src/public/js/render.js` | Layout/spec concepts for future editor |
| `src/public/js/editor.js` | UX reference for replacing `hud_editor` later |
| `src/public/specs/*.json` | Example saved layout format |

## Key Design Lesson

The strongest idea is:

```text
one complete HUD snapshot per rendered frame
```

For our embedded RmlUI version, that maps to:

```text
ezQuake state
-> HUD_RmlUi_SyncGameState()
-> RmlUI GameDataModel dirty variables
-> RML/RCSS render through OpenGL
```

No external transport is needed.

## Candidate First GameDataModel Shape

Based on `PROTOCOL.md`, start with:

```text
me.health
me.armor
me.armortype
me.ammo.shells
me.ammo.nails
me.ammo.rockets
me.ammo.cells
me.weapon
me.weapon_ammo
me.items
me.frags
me.ping
me.speed

match.map
match.gametime
match.timelimit
match.standby
match.countdown

teams[]
players[]
teaminfo[]
events.messages[]
events.centerprint
events.last_damage
```

This should become C++ structs in the embedded RmlUI bridge, not JSON transport.

## What Not To Copy

- Do not use the UDP/WebSocket bridge for the embedded HUD.
- Do not require Node, Electron, OBS, or a browser overlay.
- Do not treat the external overlay as approved competitive functionality.

The useful part is the model/editor thinking, not the runtime architecture.
