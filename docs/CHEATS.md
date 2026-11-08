# Local cheat panel (BOTW 1.9.0)

A local, experimental addition on the `local-assists` branch. Open it with **F9**, **L3 + R3** on a controller,
or **Options > Cheats (local)**. Esc or B closes it. Everything it does is queued and runs on the game's thread
once you are back in the game (menus closed, no cutscene or loading screen).

| Tab | What it does |
| --- | --- |
| Player | The five assists (stamina, health, equipment, arrows, food); rupees (+100 to +100,000, max, exact value); +10 Korok Seeds, +4 Spirit Orbs |
| Items | Search or browse every pouch item by category (weapons, bows, arrows, shields, armor by upgrade level, materials, roasted/frozen food, safe key items), pick a quantity, Give (or double-click) |
| Warp | Search or browse shrines, towers, towns, stables and Divine Beasts; Warp (or double-click). Overworld only |
| World | Set the time of day (slider or presets) and freeze it |

The F12 console has the same actions: `give <item> [count]`, `rupees [n | +n | -n | max]`, `warp <place>`,
`cheatmenu`, and `cheatstate` (why queued requests are waiting).

## Item and place lists

The lists come from your own game files and are never committed. The build helper creates them when given the
folder that holds `romfs` (and optionally `dlc1`/`aoc1`):

```powershell
powershell -ExecutionPolicy Bypass -File tools/local_assists_build.ps1 -ExeFSPath 'E:\MyGame\exefs' -GameRoot 'E:\MyGame'
```

or directly: `python tools/cheat_catalog.py <game folder> <data root>\cheats`. It reads `ActorInfo.product.sbyml`,
the English message archive, and `Map/MainField/Static.smubin` (`tools/botw_formats.py`: Yaz0, SARC, BYML, MSBT).
Weapons get their full durability; warp points use the game's own fast-travel start positions.

## How it works

`runtime/cheats.c` calls the game's own functions, so inventory and save data stay consistent:

| Function (1.9.0) | Found by |
| --- | --- |
| `PauseMenuDataMgr::itemGet(name, value, modifier)` at `0x7000d921bc` | its `"PouchGet"` play report |
| GameData increase-s32 `(delta, name, debug)` at `0x7000d34b30` | the rupee helper's callee; adds (verified live) |
| `TriggerParam::getS32(param, &out, name, check, 1)` at `0x7001137684` | a `CurrentRupee` reader |

Their entry words are checked before the first call; on any other build the panel says it is disabled. Calls run
from the camera controller's once-a-frame flag read (`rt_cam_reverse`), a game-thread point where the game itself
reads game data. Warps reuse the control layer's teleport on the presenting thread.

## Validated (2026-11-08, disposable 100% save)

- Rupees: read, add (+1,234, +250) and set (5,000).
- Items: Korok Seeds 459 -> 462, Apples 230 -> 235, arrows capped at 999, Hylian Hood added.
- Full weapon pouch: the game refuses the weapon and the panel says so.
- Warp: Riverside Stable -> Kakariko Village, world loaded normally.
- Panel opened over gameplay; layout checked.

## Limits and not yet done

- Equipment can only be added while that pouch tab has room. Cooked dishes and quest/progress key items are left out.
- Warps target the overworld; using one inside a shrine, Divine Beast or other interior is unsupported.
- Not implemented yet: horse stamina, amiibo, weather, map helper.
