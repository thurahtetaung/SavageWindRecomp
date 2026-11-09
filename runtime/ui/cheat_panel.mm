// The local cheat panel (runtime/ui/cheat_panel.h). Everything it does is queued through runtime/cheats.h and runs on
// the game's thread once the game is unpaused, so it is safe to use from the pause menu.
#include "cheat_panel.h"
#include "cheat_panel_tabs.h"

#include <atomic>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

extern "C" {
#include "../cheats.h"
#include "../local_assists.h"
#include "../settings.h"
void test_warp_set_hour(float hour);   // runtime/services/test_warp.c
void test_warp_hold_hour(float hour);
float test_warp_hour(bool *held);
}

static std::atomic<bool> g_open{false};
static std::atomic<bool> g_appearing{false};
bool cheat_panel_is_open(void) { return g_open.load(); }
void cheat_panel_set_open(bool open) {
    if (open && !g_open.load()) g_appearing.store(true);
    g_open.store(open);
}

static bool match(const char *text, const char *query) {
    if (!*query) return true;
    // every space-separated word of the query appears in text (case-insensitive)
    char words[128];
    snprintf(words, sizeof words, "%s", query);
    for (char *w = strtok(words, " "); w; w = strtok(NULL, " ")) {
        size_t n = strlen(w);
        bool found = false;
        for (const char *h = text; *h && !found; h++) {
            size_t k = 0;
            while (k < n && h[k] && tolower((unsigned char)h[k]) == tolower((unsigned char)w[k])) k++;
            found = k == n;
        }
        if (!found) return false;
    }
    return true;
}

static const ImVec4 k_gold(0.93f, 0.76f, 0.33f, 1), k_dim(0.62f, 0.62f, 0.58f, 1), k_warn(1.0f, 0.62f, 0.35f, 1);

static void section(const char *title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, k_gold);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
}
static void hint(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, k_dim);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// ---- Player
static void assist_row(int assist, const char *label, const char *help) {
    bool on = local_assist_enabled(assist);
    if (ImGui::Checkbox(label, &on)) { local_assist_set(assist, on); settings_save(); }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, k_dim);
    ImGui::TextUnformatted(help);
    ImGui::PopStyleColor();
}
static void tab_player(float scale) {
    section("Assists");
    assist_row(LOCAL_STAMINA, "Infinite stamina", "Stamina never drains; recovery still works.");
    assist_row(LOCAL_HEALTH, "Infinite health", "Damage is ignored; healing still works.");
    assist_row(LOCAL_DURABILITY, "Unbreakable equipment", "Weapons, bows and shields keep durability (enemies' too).");
    assist_row(LOCAL_ARROWS, "Infinite arrows", "Firing keeps your arrows (you need at least one).");
    assist_row(LOCAL_FOOD, "Infinite food", "Eating keeps the item. Turn off before quests that need you to hand food over.");
    assist_row(LOCAL_HORSE, "Infinite horse stamina", "Spurring your horse never uses up a spur.");

    section("Rupees");
    int r = cheat_rupees_cached();
    if (r >= 0) ImGui::Text("Current: %d", r); else hint("Current: (load a game to see)");
    const int adds[] = {100, 1000, 10000, 100000};
    for (int i = 0; i < 4; i++) {
        char b[32];
        snprintf(b, sizeof b, "+%d", adds[i]);
        if (i) ImGui::SameLine();
        if (ImGui::Button(b, ImVec2(150 * scale, 0))) cheat_rupees_add(adds[i]);
    }
    ImGui::SameLine();
    if (ImGui::Button("Max (999,999)", ImVec2(220 * scale, 0))) cheat_rupees_set(999999);
    static int set_to = 5000;
    ImGui::SetNextItemWidth(260 * scale);
    ImGui::InputInt("##rupees", &set_to, 100, 1000);
    if (set_to < 0) set_to = 0;
    if (set_to > 999999) set_to = 999999;
    ImGui::SameLine();
    if (ImGui::Button("Set rupees")) cheat_rupees_set(set_to);

    section("Collectibles");
    if (ImGui::Button("+10 Korok Seeds")) cheat_give("Obj_KorokNuts", 0, 10);
    ImGui::SameLine();
    if (ImGui::Button("+4 Spirit Orbs")) cheat_give("Obj_DungeonClearSeal", 0, 4);
    hint("Trade Spirit Orbs at a Goddess Statue for hearts or stamina; give Korok Seeds to Hestu for inventory slots.");
}

// ---- Items
static int g_item_sel = -1, g_item_cat = 0, g_qty = 1;
static char g_item_query[96];
static void tab_items(float scale) {
    int n = cheat_item_count();
    if (!n) {
        hint("No item list yet. Run tools/cheat_catalog.py on your game files (the build helper does this) and restart.");
        return;
    }
    std::vector<const char *> cats{"All"};
    for (int i = 0; i < n; i++) {
        const char *c = cheat_item(i)->category;
        if (strcmp(cats.back(), c)) cats.push_back(c);
    }
    ImGui::SetNextItemWidth(380 * scale);
    if (ImGui::BeginCombo("##cat", cats[g_item_cat < (int)cats.size() ? g_item_cat : 0])) {
        for (int i = 0; i < (int)cats.size(); i++)
            if (ImGui::Selectable(cats[i], i == g_item_cat)) { g_item_cat = i; g_item_sel = -1; }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##q", "Search items (e.g. \"ancient arrow\", \"lynel\")", g_item_query, sizeof g_item_query);

    float bottom = ImGui::GetFrameHeightWithSpacing() * 2.6f;
    ImGui::BeginChild("##items", ImVec2(0, -bottom), ImGuiChildFlags_Borders);
    const char *cat = g_item_cat ? cats[g_item_cat] : NULL;
    int shown = 0;
    bool give_now = false;
    for (int i = 0; i < n; i++) {
        const CheatItem *it = cheat_item(i);
        if (cat && strcmp(it->category, cat)) continue;
        if (!match(it->name, g_item_query) && !match(it->actor, g_item_query)) continue;
        shown++;
        ImGui::PushID(i);
        if (ImGui::Selectable(it->name, g_item_sel == i, ImGuiSelectableFlags_AllowDoubleClick)) {
            g_item_sel = i;
            if (ImGui::IsMouseDoubleClicked(0)) give_now = true;
        }
        if (!cat) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.62f + ImGui::GetCursorStartPos().x);
            ImGui::TextColored(k_dim, "%s", it->category);
        }
        ImGui::PopID();
    }
    if (!shown) hint("Nothing matches.");
    ImGui::EndChild();

    const CheatItem *sel = cheat_item(g_item_sel);
    bool equipment = sel && (!strcmp(sel->category, "Weapons") || !strcmp(sel->category, "Bows") ||
                             !strcmp(sel->category, "Shields") || !strncmp(sel->category, "Armor", 5));
    int max = equipment ? 20 : 999;
    ImGui::TextColored(sel ? k_gold : k_dim, "%s", sel ? sel->name : "Pick an item");
    if (sel && equipment) { ImGui::SameLine(); hint(sel->value ? "(copies; full durability)" : "(copies)"); }
    ImGui::SetNextItemWidth(260 * scale);
    ImGui::InputInt("Quantity", &g_qty, 1, 10);
    if (g_qty < 1) g_qty = 1;
    if (g_qty > max) g_qty = max;
    const int quick[] = {1, 5, 10, 99, 999};
    for (int q : quick) {
        if (q > max) break;
        char b[16];
        snprintf(b, sizeof b, "%d##q%d", q, q);
        ImGui::SameLine();
        if (ImGui::Button(b)) g_qty = q;
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 220 * scale);
    ImGui::BeginDisabled(!sel);
    if (ImGui::Button("Give", ImVec2(220 * scale, 0)) || (give_now && sel)) cheat_give(sel->actor, sel->value, g_qty);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 360 * scale);
    if (ImGui::Button("Guide", ImVec2(130 * scale, 0)) && sel) cheat_guide_show(sel->name, "item");
    ImGui::EndDisabled();
}

// ---- Warp
static int g_place_sel = -1, g_place_cat = 0;
static char g_place_query[96];
static void tab_warp(float scale) {
    cheat_shrine_box(scale);
    ImGui::Separator();
    int n = cheat_place_count();
    if (!n) { hint("No place list yet. Run tools/cheat_catalog.py on your game files and restart."); return; }
    std::vector<const char *> cats{"All"};
    for (int i = 0; i < n; i++) {
        const char *c = cheat_place(i)->category;
        if (strcmp(cats.back(), c)) cats.push_back(c);
    }
    ImGui::SetNextItemWidth(380 * scale);
    if (ImGui::BeginCombo("##pcat", cats[g_place_cat < (int)cats.size() ? g_place_cat : 0])) {
        for (int i = 0; i < (int)cats.size(); i++)
            if (ImGui::Selectable(cats[i], i == g_place_cat)) { g_place_cat = i; g_place_sel = -1; }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pq", "Search places (e.g. \"kakariko\", \"shrine\")", g_place_query, sizeof g_place_query);
    float bottom = ImGui::GetFrameHeightWithSpacing() * 2.6f;
    ImGui::BeginChild("##places", ImVec2(0, -bottom), ImGuiChildFlags_Borders);
    const char *cat = g_place_cat ? cats[g_place_cat] : NULL;
    bool go_now = false;
    for (int i = 0; i < n; i++) {
        const CheatPlace *p = cheat_place(i);
        if ((cat && strcmp(p->category, cat)) || !match(p->name, g_place_query)) continue;
        ImGui::PushID(i);
        if (ImGui::Selectable(p->name, g_place_sel == i, ImGuiSelectableFlags_AllowDoubleClick)) {
            g_place_sel = i;
            if (ImGui::IsMouseDoubleClicked(0)) go_now = true;
        }
        if (!cat) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.62f + ImGui::GetCursorStartPos().x);
            ImGui::TextColored(k_dim, "%s", p->category);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    const CheatPlace *sel = cheat_place(g_place_sel);
    if (sel) ImGui::TextColored(k_gold, "%s  (%.0f, %.0f, %.0f)", sel->name, sel->x, sel->y, sel->z);
    else hint("Pick a place.");
    hint("A real fast travel, with the game's loading screen. Overworld only; inside a shrine, use the solver above.");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 380 * scale);
    ImGui::BeginDisabled(!sel);
    if (ImGui::Button("Guide", ImVec2(130 * scale, 0)) && sel)
        cheat_guide_show(sel->name, !strcmp(sel->category, "Shrines") ? "shrine" : "place");
    ImGui::SameLine();
    if (ImGui::Button("Warp", ImVec2(220 * scale, 0)) || (go_now && sel)) cheat_travel_to(sel->x, sel->y, sel->z, sel->yaw);
    ImGui::EndDisabled();
}

// ---- World
static void tab_world(float scale) {
    (void)scale;
    section("Time of day");
    bool held = false;
    float hour = test_warp_hour(&held);
    static float want = 12;
    if (hour >= 0) ImGui::Text("Now: %02d:%02d%s", (int)hour, (int)(fmodf(hour, 1) * 60), held ? "  (frozen)" : "");
    else hint("Now: (load a game to see)");
    ImGui::SetNextItemWidth(500 * scale);
    ImGui::SliderFloat("##hour", &want, 0, 24, "%.1f h");
    ImGui::SameLine();
    if (ImGui::Button("Set time")) test_warp_set_hour(want);
    const struct { const char *name; float h; } presets[] = {{"Dawn", 5.5f}, {"Noon", 12}, {"Sunset", 18.5f}, {"Midnight", 0}};
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(presets[i].name)) { want = presets[i].h; test_warp_set_hour(want); }
    }
    bool freeze = held;
    if (ImGui::Checkbox("Freeze time", &freeze)) test_warp_hold_hour(freeze ? (hour >= 0 ? hour : want) : -1);
    hint("Freezing keeps the hour where it is until you unfreeze it or restart the game.");
}

void cheat_panel_draw(ImGuiIO &io, float scale, ImFont *font) {
    bool appearing = g_appearing.exchange(false);
    float w = fminf(io.DisplaySize.x - 32 * scale, 1500 * scale), h = io.DisplaySize.y * 0.86f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    if (appearing) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6 * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * scale, 18 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12 * scale, 7 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12 * scale, 9 * scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.06f, 0.06f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.50f, 0.36f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.93f, 0.76f, 0.33f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(0.93f, 0.76f, 0.33f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.93f, 0.76f, 0.33f, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.93f, 0.76f, 0.33f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, k_gold);
    ImGui::Begin("Cheats##panel", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    if (font) ImGui::PushFont(font);
    ImGui::TextColored(k_gold, "CHEATS");
    ImGui::SameLine();
    ImGui::TextColored(k_dim, "local, experimental  |  F9 to toggle (L3+R3 with free camera clicks off)  |  Esc / B to close");
    char status[160];
    cheat_status(status, sizeof status);
    int pending = cheat_pending();
    if (!cheats_supported()) ImGui::TextColored(k_warn, "%s", status);
    else if (pending && !cheats_world_ready())
        ImGui::TextColored(k_warn, "%d queued: they run when you are back in the game (menus closed, no cutscene).", pending);
    else ImGui::TextColored(k_dim, "%s", status);

    if (ImGui::BeginTabBar("##tabs")) {
        int want = g_cheat_select_tab;
        g_cheat_select_tab = -1;
        auto flags = [&](int t) { return want == t ? ImGuiTabItemFlags_SetSelected : 0; };
        static bool guide_opened;
        if (ImGui::BeginTabItem("Player", NULL, flags(CHEAT_TAB_PLAYER))) { tab_player(scale); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Items", NULL, flags(CHEAT_TAB_ITEMS))) { tab_items(scale); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Warp", NULL, flags(CHEAT_TAB_WARP))) { tab_warp(scale); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Map", NULL, flags(CHEAT_TAB_MAP))) { cheat_tab_map(scale); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Guide", NULL, flags(CHEAT_TAB_GUIDE))) {
            cheat_tab_guide(scale, !guide_opened);
            guide_opened = true;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Amiibo", NULL, flags(CHEAT_TAB_AMIIBO))) { cheat_tab_amiibo(scale); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("World", NULL, flags(CHEAT_TAB_WORLD))) { tab_world(scale); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    bool typing = ImGui::GetIO().WantTextInput;
    if (!appearing && !typing && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        cheat_panel_set_open(false);
    if (font) ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor(10);
    ImGui::PopStyleVar(4);
}
