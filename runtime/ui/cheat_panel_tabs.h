// The cheat panel's Map and Guide tabs (cheat_panel_tabs.mm), used by cheat_panel.mm.
#pragma once

enum { CHEAT_TAB_PLAYER, CHEAT_TAB_ITEMS, CHEAT_TAB_WARP, CHEAT_TAB_MAP, CHEAT_TAB_GUIDE, CHEAT_TAB_AMIIBO, CHEAT_TAB_WORLD };
extern int g_cheat_select_tab;  // a tab to switch to on the next frame, or -1

void cheat_tab_map(float scale);
void cheat_tab_guide(float scale, bool first_time);
void cheat_tab_amiibo(float scale);
void cheat_shrine_box(float scale);  // the shrine solver, shown on the Warp tab
// Open a guide page and switch to the Guide tab (hint: "shrine", "item", "place", "korok", "tower", "beast", "").
void cheat_guide_show(const char *name, const char *hint);
