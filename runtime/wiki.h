// In-game guide (runtime/wiki.c): plain-text pages from Zelda Wiki (zeldawiki.wiki, the community's independent
// wiki) through its MediaWiki API, fetched on a background thread only when the player asks, cached on disk in
// <data root>/cheats/wiki/. Pages are made "smart" for Breath of the Wild: the BotW section of a multi-game page comes
// first, other games' sections are dropped, and solution / walkthrough sections are marked as spoilers.
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *heading;  // "" for the introduction
    const char *text;
    int level;            // 2 for ==, 3 for ===, ...
    bool spoiler;         // a solution / walkthrough: hidden until asked
} WikiSection;

enum { WIKI_IDLE, WIKI_LOADING, WIKI_READY, WIKI_FAILED };

// Ask for a page. `hint` is what kind of thing it is ("shrine", "item", "place", "enemy", "" ...): it picks the title
// (e.g. armor levels are dropped, Korok spots go to "Korok Seed") and the fallback search.
void wiki_open(const char *name, const char *hint);
// Live title suggestions for a typed query (the result arrives later; wiki_suggestions reads it).
void wiki_suggest(const char *query);

// Snapshot of the current page (render thread). Valid until the next wiki_lock_page / wiki_unlock_page pair ends.
int wiki_lock_page(const char **title, const char **status, const WikiSection **sections, int *count);
void wiki_unlock_page(void);
int wiki_suggestions(char out[][96], int max);  // copies the latest suggestions
// The page's address, for "open in browser" (Zelda Wiki or Zelda Dungeon).
void wiki_url(int site, char *out, int n);
bool wiki_available(void);  // this platform can fetch pages

#ifdef __cplusplus
}
#endif
