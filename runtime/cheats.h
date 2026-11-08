// Local cheat actions (runtime/cheats.c): give items, rupees, warps. The panel (ui/cheat_panel.mm) and the console
// commands queue requests from any thread; they run on the game's thread (cheats_game_tick; warps on the presenting thread), only while a
// world is loaded and the game is not paused. Game addresses are for BOTW 1.9.0 only; other versions refuse.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { const char *category, *actor, *name; int value; } CheatItem;   // value: weapon durability x100
typedef struct { const char *category, *name; float x, y, z, yaw; } CheatPlace;
// A map marker (cheats/map.tsv): kind is "shrine", "tower", "korok", "beast"; found / done come from save flags.
typedef struct { const char *kind, *name; float x, y, z; const char *found_flag, *done_flag; int found, done; } CheatMark;

// The catalog (<data root>/cheats/items.tsv and places.tsv, made by tools/cheat_catalog.py from the player's files).
int cheat_item_count(void);
const CheatItem *cheat_item(int i);
int cheat_place_count(void);
const CheatPlace *cheat_place(int i);
const CheatItem *cheat_item_find(const char *actor_or_name);
const CheatPlace *cheat_place_find(const char *name);  // case-insensitive substring of the English name

int cheat_mark_count(void);
const CheatMark *cheat_mark(int i);   // found / done: 1 yes, 0 no, -1 not read yet
void cheat_marks_want(void);          // the map is on screen: keep found / done fresh (a few markers per frame)
bool cheat_link_position(float out[3]);
typedef struct { const char *id, *name; float end[3], entrance[3]; float chest[8][3]; int nchest; } CheatShrine;
// The shrine Link is in: from the game's current stage (CDungeon/DungeonNNN), else the guess above; NULL outside.
const CheatShrine *cheat_current_shrine(void);
void cheat_current_stage(char *type, char *name, int n);  // the game's current stage ("MainField", "A-1"...)
bool cheat_shrine_warp(int what);  // 0: the end (in front of the monk), 1: the entrance, 2 + i: chest i
int cheat_inside_shrine(void);       // map marker index of the shrine Link seems to be in, or -1 (a guess)


bool cheats_supported(void);   // the loaded game is the verified build
bool cheats_world_ready(void); // a world is loaded and the game is not paused (queued requests run then)

// Requests (any thread). false: the queue is full or the request is invalid.
bool cheat_give(const char *actor, int value, int count);
bool cheat_rupees_add(int delta);        // clamps to 0..999999
bool cheat_rupees_set(int value);
bool cheat_warp(float x, float y, float z);
bool cheat_warp_spot(float x, float y, float z);
// The reliable warp: make (x, y, z) the DLC Travel Medallion's destination, so the map's fast travel to the medallion
// takes Link there with the game's own loading screen. Needs the medallion (DLC 1). Overworld spots only.
bool cheat_medallion_set(float x, float y, float z, float yaw_deg);
// A real fast travel (the game's own scene change, with its loading screen): to a named start position
// ("MainField/A-1", "StartPos_CDungeon118"), or with pos "WarpIcon" to the Travel Medallion's destination.
bool cheat_scene_change(const char *map, const char *pos);
// Fast travel to any overworld spot: the medallion's destination is set there, then the scene changes to it.
bool cheat_travel_to(float x, float y, float z, float yaw_deg);  // via the nearest ground point (cliffs, trees, tower tops)
bool cheat_flag_s32_set(const char *name, int value);
int cheat_pending(void);

// The last thing done or refused, for the panel and the console (copied into buf).
void cheat_status(char *buf, int n);
// Rupees as last read on the game thread (-1: unknown).
int cheat_rupees_cached(void);

// Once per presented frame (runtime/control.c control_frame).
void cheats_frame(uint64_t frame);
// Once a frame on the game's thread (runtime/hle/input.c rt_cam_reverse).
void cheats_game_tick(void);
void cheats_init(void);  // console commands

#ifdef __cplusplus
}
#endif
