// The companion overlay (runtime/ui/companion_overlay.mm), wired into the overlay frame in menu.mm.
#pragma once
#include "imgui.h"

bool companion_overlay_needed(void);                       // any thread
void companion_overlay_draw(ImGuiIO &io, float scale);     // in the ImGui frame (render thread)
