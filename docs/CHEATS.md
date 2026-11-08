# Local cheat panel (BOTW 1.9.0)

A local, experimental addition on the `local-assists` branch. Open it with **F9**, **L3 + R3** on a controller,
or **Options > Cheats (local)**. Esc or B closes it. Actions are queued and run on the game's thread once you are
back in the game (menus closed, no cutscene or loading screen).

| Tab | What it does |
| --- | --- |
| Player | Assists (stamina, health, equipment, arrows, food, horse stamina); rupees; Korok Seeds and Spirit Orbs |
| Items | Search or browse every pouch item by category, pick a quantity, Give (or double-click) |
| Warp | Shrine solver (inside a shrine: the monk, the entrance, each chest), then shrines, towers, towns, stables and Divine Beasts (their nearby town): Warp is a real fast travel |
| Map | Hyrule with shrines, towers, Divine Beasts and all 900 Korok spots coloured by your save; nearest missing; Warp here and Guide |
| Guide | Zelda Wiki pages suggested from where you are (the shrine you are in, the nearest unfinished shrine, the area); search with live suggestions; solutions hidden until asked |
| Amiibo | A virtual amiibo reader: place any Zelda figure (Wolf Link, Epona's Link, Champions...), then use the amiibo rune |
| World | Time of day and freezing it |

Console (F12) equivalents: `give <item> [count]`, `rupees [n | +n | -n | max]`, `warp <place>`, `travel <place | x y z>`,
`shrine [end | entrance | chest n]`, `flag <name> [value]`, `assist <name> [on|off]`, `cheatmenu`, `cheatstate`.

## Lists from your own files

The item, place, map, shrine and fast-travel lists come from your game files and are never committed:

```powershell
powershell -ExecutionPolicy Bypass -File tools/local_assists_build.ps1 -ExeFSPath 'E:\MyGame\exefs' -GameRoot 'E:\MyGame'
```

or `python tools/cheat_catalog.py <folder with romfs and dlc1> <data root>\cheats`. It writes `items.tsv`,
`places.tsv`, `map.tsv`, `shrines.tsv` and `starts.tsv` (`tools/botw_formats.py` reads Yaz0, SARC, BYML and MSBT).

## How it works

Everything calls the game's own code (1.9.0; entry words are checked before the first call, other builds refuse):

| Feature | Game code |
| --- | --- |
| Give items | `PauseMenuDataMgr::itemGet` (0x7000d921bc) |
| Rupees, flags | GameData increase-s32 (0x7000d34b30), TriggerParam getS32 / getBool |
| Warp | `ksys::StageInfo::changeScene` (0x70012082e0, through `sChangeScene`) to the nearest ground start position, then a short hop |
| Shrine solver | The current stage (`StageInfo` map type / name, 0x7001dae3a0 / 0x7001dae3d8) and the shrine's own map data |
| Horse stamina | A verified patch site skips the spur's charge store (0x6b0d2c) |
| Amiibo | `nfp:user` answers as a reader holding the chosen figure (runtime/services/nfp.c) |
| Guide | Zelda Wiki's MediaWiki API over WinHTTP, only when you open a page; cached in `cheats/wiki/` |

Calls run from the camera controller's once-a-frame flag read (`rt_cam_reverse`), on the game thread.

## Validated on the disposable 100% save (2026-11-08)

- Items, rupees, Korok Seeds; full-pouch refusal reported.
- Horse stamina on and off in open field (user-tested); Map and Guide (user-tested); shrine solver (user-tested).
- Fast travel: a shrine pad by Kakariko, Gerudo Town, Vah Rudania's town, the Shrine of Resurrection, and a
  Hebra Korok spot (fast travel to the nearest shrine, then a hop): all loaded with the game's own loading screen.

## Limits

- Equipment can be given only while that pouch tab has room. Cooked dishes and quest key items are not offered.
- Warps target the overworld. The instant position warp (`teleport`, kept for short hops) can leave the world
  loading forever on long jumps or mid-air targets; the panel's Warp no longer uses it for those.
- Amiibo: needs Options > Gameplay > amiibo on. Each scan counts as a new figure.
- Weather control and TOTK are not done yet.
