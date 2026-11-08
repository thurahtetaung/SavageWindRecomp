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
int cheat_inside_shrine(void);       // map marker index of the shrine Link seems to be in, or -1 (a guess)


bool cheats_supported(void);   // the loaded game is the verified build
bool cheats_world_ready(void); // a world is loaded and the game is not paused (queued requests run then)

// Requests (any thread). false: the queue is full or the request is invalid.
bool cheat_give(const char *actor, int value, int count);
bool cheat_rupees_add(int delta);        // clamps to 0..999999
bool cheat_rupees_set(int value);
bool cheat_warp(float x, float y, float z);
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
