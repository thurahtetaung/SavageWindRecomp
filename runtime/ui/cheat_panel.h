// The local cheat panel (F9, or L3 + R3 on a controller, or Options > Cheats): an ImGui window over the game with
// Player / Items / Warp / World tabs. Actions go through runtime/cheats.h; it is wired into the overlay in menu.mm.
#pragma once
#include <stdbool.h>
#include "imgui.h"

#ifdef __cplusplus
extern "C" {
#endif
bool cheat_panel_is_open(void);  // any thread
void cheat_panel_set_open(bool open);
#ifdef __cplusplus
}
#endif
// In the ImGui frame (render thread), when open.
void cheat_panel_draw(ImGuiIO &io, float scale, ImFont *font);
