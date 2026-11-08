// The cheat panel's Map and Guide tabs (cheat_panel_tabs.h).
//
// Map: Hyrule drawn from the game's own marker data (cheats/map.tsv): shrines, towers, Divine Beasts and all 900 Korok
// spots, coloured by the save's flags (found / completed), with the region names as labels and the game's map
// sections (A-J, 1-8) as a grid. Wheel or +/- zooms, dragging pans; click a marker for Warp and Guide.
// Guide: Zelda Wiki pages (runtime/wiki.h), suggested from where Link is: the shrine he is in, the nearest unfinished
// shrine, the nearest tower, the area he is in; or searched with live title suggestions.
#include "cheat_panel_tabs.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <vector>

#include "imgui.h"

extern "C" {
#include "../cheats.h"
#include "../wiki.h"
}

int g_cheat_select_tab = -1;

static const ImVec4 k_gold(0.93f, 0.76f, 0.33f, 1), k_dim(0.62f, 0.62f, 0.58f, 1);
static void dim_text(const char *t) { ImGui::TextColored(k_dim, "%s", t); }

static const char *compass(float dx, float dz) {  // north is -z on the game's map
    static const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    float a = atan2f(dx, -dz) * 57.29578f;
    if (a < 0) a += 360;
    return dirs[(int)((a + 22.5f) / 45) % 8];
}
static bool missing(const CheatMark *m) { return m->done == 0; }
static const char *hint_of(const CheatMark *m) {
    return !strcmp(m->kind, "label") ? "place" : m->kind;
}

void cheat_guide_show(const char *name, const char *hint) {
    wiki_open(name, hint);
    g_cheat_select_tab = CHEAT_TAB_GUIDE;
}

// ---------------------------------------------------------------- Map
static float g_zoom = 1, g_cx = 0, g_cz = 0;
static int g_sel = -1;
static bool f_shrine = true, f_tower = true, f_korok = true, f_beast = true, f_label = true, f_missing = false;

static bool shown(const CheatMark *m) {
    switch (m->kind[0]) {
    case 's': return f_shrine && (!f_missing || missing(m));
    case 't': return f_tower && (!f_missing || missing(m));
    case 'k': return f_korok && (!f_missing || missing(m));
    case 'b': return f_beast && (!f_missing || missing(m));
    default: return false;
    }
}
static const char *state_text(const CheatMark *m) {
    if (m->done < 0) return "reading save...";
    switch (m->kind[0]) {
    case 's': return m->done ? "completed" : m->found == 1 ? "found, not completed" : "not found yet";
    case 't': return m->done ? "activated" : "not activated";
    case 'k': return m->done ? "found" : "not found";
    case 'b': return m->done ? "freed" : "not freed";
    }
    return "";
}

void cheat_tab_map(float scale) {
    cheat_marks_want();
    int n = cheat_mark_count();
    if (!n) { dim_text("No map data yet. Run tools/cheat_catalog.py on your game files and restart."); return; }
    float link[3];
    bool have_link = cheat_link_position(link);

    // totals
    int tot[4] = {0}, done[4] = {0};
    for (int i = 0; i < n; i++) {
        const CheatMark *m = cheat_mark(i);
        int k = m->kind[0] == 's' ? 0 : m->kind[0] == 'k' ? 1 : m->kind[0] == 't' ? 2 : m->kind[0] == 'b' ? 3 : -1;
        if (k < 0 || (k == 3 && !strstr(m->name, "Divine Beast"))) continue;
        tot[k]++;
        done[k] += m->done == 1;
    }
    ImGui::TextColored(k_gold, "Shrines %d/%d   Korok Seeds %d/%d   Towers %d/%d", done[0], tot[0], done[1], tot[1], done[2], tot[2]);
    ImGui::Checkbox("Shrines", &f_shrine); ImGui::SameLine();
    ImGui::Checkbox("Towers", &f_tower); ImGui::SameLine();
    ImGui::Checkbox("Koroks", &f_korok); ImGui::SameLine();
    ImGui::Checkbox("Divine Beasts", &f_beast); ImGui::SameLine();
    ImGui::Checkbox("Names", &f_label); ImGui::SameLine();
    ImGui::Checkbox("Only missing", &f_missing);

    float avail_w = ImGui::GetContentRegionAvail().x, avail_h = ImGui::GetContentRegionAvail().y;
    float list_w = avail_w * 0.30f;
    float map_w = avail_w - list_w - ImGui::GetStyle().ItemSpacing.x;
    float info_h = ImGui::GetFrameHeightWithSpacing() * 2.2f;
    float map_h = avail_h - info_h;

    // ---- the map canvas
    ImGui::BeginGroup();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##map", ImVec2(map_w, map_h), ImGuiButtonFlags_MouseButtonLeft);
    bool hovered = ImGui::IsItemHovered();
    static bool dragged;  // (a drag pans; only a click without one selects)
    if (ImGui::IsItemActivated()) dragged = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 3 * scale)) dragged = true;
    bool clicked = ImGui::IsItemDeactivated() && !dragged;
    ImGuiIO &io = ImGui::GetIO();
    float ppm = map_w / (10000.0f / g_zoom);  // pixels per metre
    auto to_screen = [&](float x, float z) { return ImVec2(p0.x + map_w * 0.5f + (x - g_cx) * ppm, p0.y + map_h * 0.5f + (z - g_cz) * ppm); };
    auto to_world = [&](ImVec2 s, float *x, float *z) { *x = g_cx + (s.x - p0.x - map_w * 0.5f) / ppm; *z = g_cz + (s.y - p0.y - map_h * 0.5f) / ppm; };
    if (hovered && io.MouseWheel != 0) {  // zoom around the pointer
        float wx, wz;
        to_world(io.MousePos, &wx, &wz);
        g_zoom = std::clamp(g_zoom * powf(1.25f, io.MouseWheel), 1.0f, 40.0f);
        ppm = map_w / (10000.0f / g_zoom);
        g_cx = wx - (io.MousePos.x - p0.x - map_w * 0.5f) / ppm;
        g_cz = wz - (io.MousePos.y - p0.y - map_h * 0.5f) / ppm;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 3)) { g_cx -= io.MouseDelta.x / ppm; g_cz -= io.MouseDelta.y / ppm; }
    g_cx = std::clamp(g_cx, -5000.0f, 5000.0f);
    g_cz = std::clamp(g_cz, -4000.0f, 4000.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, ImVec2(p0.x + map_w, p0.y + map_h), true);
    dl->AddRectFilled(p0, ImVec2(p0.x + map_w, p0.y + map_h), IM_COL32(18, 22, 20, 255));
    dl->AddRectFilled(to_screen(-5000, -4000), to_screen(5000, 4000), IM_COL32(34, 40, 33, 255), 4);
    ImFont *font = ImGui::GetFont();
    float fs = ImGui::GetFontSize();
    for (int c = 0; c <= 10; c++) dl->AddLine(to_screen(-5000 + c * 1000.0f, -4000), to_screen(-5000 + c * 1000.0f, 4000), IM_COL32(255, 255, 255, 18));
    for (int r = 0; r <= 8; r++) dl->AddLine(to_screen(-5000, -4000 + r * 1000.0f), to_screen(5000, -4000 + r * 1000.0f), IM_COL32(255, 255, 255, 18));
    for (int c = 0; c < 10; c++)
        for (int r = 0; r < 8; r++) {
            char sec[4] = {(char)('A' + c), '-', (char)('1' + r), 0};
            dl->AddText(font, fs * 0.55f, ImVec2(to_screen(-5000 + c * 1000.0f, 0).x + 3, to_screen(0, -4000 + r * 1000.0f).y + 2),
                        IM_COL32(255, 255, 255, 40), sec);
        }
    if (f_label) {  // region names: provinces far out, smaller places as you zoom in
        float level_max = g_zoom < 1.6f ? 1 : g_zoom < 3.5f ? 2 : g_zoom < 7 ? 4 : 8;
        for (int i = 0; i < n; i++) {
            const CheatMark *m = cheat_mark(i);
            if (m->kind[0] != 'l' || m->y > level_max) continue;
            float size = fs * (m->y <= 1 ? 0.95f : m->y <= 2 ? 0.75f : 0.6f);
            ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, m->name);
            ImVec2 s = to_screen(m->x, m->z);
            dl->AddText(font, size, ImVec2(s.x - ts.x * 0.5f, s.y - ts.y * 0.5f),
                        m->y <= 1 ? IM_COL32(236, 214, 160, 150) : IM_COL32(220, 220, 210, 110), m->name);
        }
    }
    int hover = -1;
    float hover_d = (12 * scale) * (12 * scale);
    for (int pass = 0; pass < 2; pass++)  // Koroks under everything else
        for (int i = 0; i < n; i++) {
            const CheatMark *m = cheat_mark(i);
            if ((m->kind[0] == 'k') != (pass == 0) || !shown(m)) continue;
            ImVec2 s = to_screen(m->x, m->z);
            if (s.x < p0.x - 20 || s.y < p0.y - 20 || s.x > p0.x + map_w + 20 || s.y > p0.y + map_h + 20) continue;
            float r = scale * (m->kind[0] == 'k' ? (g_zoom >= 3 ? 3.5f : 2.2f) : m->kind[0] == 'b' ? 9 : 6.5f);
            ImU32 col;
            switch (m->kind[0]) {
            case 's': col = m->done == 1 ? IM_COL32(110, 200, 130, 255) : m->found == 1 ? IM_COL32(245, 160, 60, 255) : IM_COL32(150, 150, 150, 200); break;
            case 't': col = m->done == 1 ? IM_COL32(90, 200, 230, 255) : IM_COL32(150, 150, 150, 220); break;
            case 'b': col = m->done == 1 ? IM_COL32(110, 200, 130, 255) : IM_COL32(230, 80, 70, 255); break;
            default: col = m->done == 1 ? IM_COL32(120, 140, 90, 110) : IM_COL32(250, 220, 70, 255); break;
            }
            if (m->kind[0] == 's') dl->AddQuadFilled(ImVec2(s.x, s.y - r), ImVec2(s.x + r, s.y), ImVec2(s.x, s.y + r), ImVec2(s.x - r, s.y), col);
            else if (m->kind[0] == 't') dl->AddTriangleFilled(ImVec2(s.x, s.y - r * 1.4f), ImVec2(s.x + r * 0.8f, s.y + r), ImVec2(s.x - r * 0.8f, s.y + r), col);
            else dl->AddCircleFilled(s, r, col);
            if (i == g_sel) dl->AddCircle(s, r + 5 * scale, IM_COL32(255, 255, 255, 230), 0, 2 * scale);
            float dx = io.MousePos.x - s.x, dy = io.MousePos.y - s.y;
            if (hovered && dx * dx + dy * dy < hover_d) { hover_d = dx * dx + dy * dy; hover = i; }
        }
    if (have_link) {
        ImVec2 s = to_screen(link[0], link[2]);
        dl->AddCircleFilled(s, 8 * scale, IM_COL32(60, 140, 255, 255));
        dl->AddCircle(s, 8 * scale, IM_COL32(255, 255, 255, 255), 0, 2.5f * scale);
    }
    dl->PopClipRect();
    if (hover >= 0) {
        const CheatMark *m = cheat_mark(hover);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(m->name);
        dim_text(state_text(m));
        if (have_link) ImGui::TextColored(k_dim, "%.0f m %s", sqrtf((m->x - link[0]) * (m->x - link[0]) + (m->z - link[2]) * (m->z - link[2])),
                                          compass(m->x - link[0], m->z - link[2]));
        ImGui::EndTooltip();
        if (clicked) g_sel = hover;
    }
    // ---- selection bar
    const CheatMark *sel = cheat_mark(g_sel);
    if (sel) {
        ImGui::TextColored(k_gold, "%s", sel->name);
        ImGui::SameLine();
        dim_text(state_text(sel));
    } else dim_text("Click a marker. Wheel or +/- zooms, drag to move.");
    if (ImGui::Button(" - ")) g_zoom = std::max(1.0f, g_zoom / 1.5f);
    ImGui::SameLine();
    if (ImGui::Button(" + ")) g_zoom = std::min(40.0f, g_zoom * 1.5f);
    ImGui::SameLine();
    if (ImGui::Button("Center on Link") && have_link) { g_cx = link[0]; g_cz = link[2]; g_zoom = std::max(g_zoom, 4.0f); }
    ImGui::SameLine();
    if (ImGui::Button("Whole map")) { g_cx = g_cz = 0; g_zoom = 1; }
    ImGui::BeginDisabled(!sel);
    ImGui::SameLine();
    if (ImGui::Button("Warp here") && sel) {
        if (sel->kind[0] == 'k' || sel->kind[0] == 't') cheat_warp_spot(sel->x, sel->y, sel->z);  // (may be off the ground)
        else cheat_warp(sel->x, sel->y + 0.5f, sel->z);
    }
    ImGui::SameLine();
    if (ImGui::Button("Via medallion") && sel) cheat_medallion_set(sel->x, sel->y + (sel->kind[0] == 'k' ? 1.0f : 0), sel->z, 0);
    ImGui::SameLine();
    if (ImGui::Button("Guide") && sel) cheat_guide_show(sel->name, hint_of(sel));
    ImGui::EndDisabled();
    ImGui::EndGroup();

    // ---- nearest missing
    ImGui::SameLine();
    ImGui::BeginChild("##near", ImVec2(list_w, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(k_gold, "Nearest missing");
    if (!have_link) dim_text("Load a game.");
    else {
        std::vector<std::pair<float, int>> near;
        for (int i = 0; i < n; i++) {
            const CheatMark *m = cheat_mark(i);
            if (m->kind[0] == 'l' || !missing(m) || !shown(m)) continue;
            float dx = m->x - link[0], dz = m->z - link[2];
            near.push_back({dx * dx + dz * dz, i});
        }
        std::partial_sort(near.begin(), near.begin() + std::min<size_t>(12, near.size()), near.end());
        for (size_t k = 0; k < near.size() && k < 12; k++) {
            const CheatMark *m = cheat_mark(near[k].second);
            char line[200];
            snprintf(line, sizeof line, "%s##n%d", m->name, near[k].second);
            if (ImGui::Selectable(line, g_sel == near[k].second)) {
                g_sel = near[k].second;
                g_cx = m->x; g_cz = m->z; g_zoom = std::max(g_zoom, 5.0f);
            }
            ImGui::TextColored(k_dim, "   %.0f m %s, %s", sqrtf(near[k].first), compass(m->x - link[0], m->z - link[2]), state_text(m));
        }
        if (near.empty()) dim_text("Nothing missing in the shown kinds. Well done!");
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Guide
static char g_query[96], g_sent[96];
static double g_typed_at;
static bool g_show_spoiler[128];
static char g_page_seen[160];

struct Suggestion { char label[200]; char name[160]; const char *hint; };
static std::vector<Suggestion> context_suggestions(void) {
    std::vector<Suggestion> out;
    float p[3];
    if (!cheat_link_position(p)) return out;
    auto dist = [&](const CheatMark *m) { return sqrtf((m->x - p[0]) * (m->x - p[0]) + (m->z - p[2]) * (m->z - p[2])); };
    const CheatMark *in = cheat_mark(cheat_inside_shrine());
    if (in) {
        Suggestion s;
        snprintf(s.label, sizeof s.label, "You're in: %s", in->name);
        snprintf(s.name, sizeof s.name, "%s", in->name);
        s.hint = "shrine";
        out.push_back(s);
    }
    const CheatMark *best_shrine = NULL, *best_tower = NULL, *area = NULL;
    float bs = 1e9f, bt = 1e9f, ba = 1e9f;
    for (int i = 0; i < cheat_mark_count(); i++) {
        const CheatMark *m = cheat_mark(i);
        float d = dist(m);
        if (m->kind[0] == 's' && m != in && m->done != 1 && d < bs) { bs = d; best_shrine = m; }
        if (m->kind[0] == 't' && d < bt) { bt = d; best_tower = m; }
        if (m->kind[0] == 'l' && m->y >= 4 && d < ba) { ba = d; area = m; }
    }
    if (best_shrine) {
        Suggestion s;
        snprintf(s.label, sizeof s.label, "Nearest unfinished shrine: %s (%.0f m %s)", best_shrine->name, bs,
                 compass(best_shrine->x - p[0], best_shrine->z - p[2]));
        snprintf(s.name, sizeof s.name, "%s", best_shrine->name);
        s.hint = "shrine";
        out.push_back(s);
    }
    if (area && ba < 600) {
        Suggestion s;
        snprintf(s.label, sizeof s.label, "Around you: %s", area->name);
        snprintf(s.name, sizeof s.name, "%s", area->name);
        s.hint = "place";
        out.push_back(s);
    }
    if (best_tower) {
        Suggestion s;
        snprintf(s.label, sizeof s.label, "Nearest tower: %s (%.0f m %s)", best_tower->name, bt, compass(best_tower->x - p[0], best_tower->z - p[2]));
        snprintf(s.name, sizeof s.name, "%s", best_tower->name);
        s.hint = "tower";
        out.push_back(s);
    }
    return out;
}

void cheat_tab_guide(float scale, bool first_time) {
    std::vector<Suggestion> sugg = context_suggestions();
    const char *title, *status;
    const WikiSection *sec;
    int nsec;
    int state = wiki_lock_page(&title, &status, &sec, &nsec);
    bool empty = !*title;
    wiki_unlock_page();
    // The opinionated default: open the most relevant page by itself (the shrine you are in, else the nearest
    // unfinished shrine), so the tab is useful the moment it opens.
    if (first_time && empty && !sugg.empty()) wiki_open(sugg[0].name, sugg[0].hint);
    else if (first_time && empty) wiki_open("Breath of the Wild", "");

    ImGui::TextColored(k_gold, "Suggested");
    for (size_t i = 0; i < sugg.size(); i++) {
        ImGui::PushID((int)i);
        if (ImGui::Button(sugg[i].label)) wiki_open(sugg[i].name, sugg[i].hint);
        ImGui::PopID();
    }
    if (sugg.empty()) dim_text("Load a game for suggestions based on where you are.");

    ImGui::SetNextItemWidth(-200 * scale);
    bool enter = ImGui::InputTextWithHint("##gq", "Search Zelda Wiki (e.g. \"lynel\", \"elixir\", \"Master Sword\")", g_query,
                                          sizeof g_query, ImGuiInputTextFlags_EnterReturnsTrue);
    bool active = ImGui::IsItemActive();
    if (ImGui::IsItemEdited()) g_typed_at = ImGui::GetTime();
    if (strcmp(g_query, g_sent) && ImGui::GetTime() - g_typed_at > 0.3) {  // live suggestions, after a short pause
        snprintf(g_sent, sizeof g_sent, "%s", g_query);
        wiki_suggest(g_query);
    }
    ImGui::SameLine();
    if (ImGui::Button("Search", ImVec2(-1, 0)) || enter) if (*g_query) wiki_open(g_query, "");
    if (*g_query && (active || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))) {
        char s[8][96];
        int ns = wiki_suggestions(s, 8);
        for (int i = 0; i < ns; i++) {
            ImGui::PushID(100 + i);
            if (i % 4) ImGui::SameLine();
            if (ImGui::SmallButton(s[i])) { wiki_open(s[i], ""); g_query[0] = 0; g_sent[0] = 0; }
            ImGui::PopID();
        }
    }
    ImGui::Separator();

    float foot = ImGui::GetFrameHeightWithSpacing() * 1.2f;
    ImGui::BeginChild("##page", ImVec2(0, -foot), ImGuiChildFlags_Borders);
    state = wiki_lock_page(&title, &status, &sec, &nsec);
    if (strcmp(g_page_seen, title)) {  // a new page: spoilers hidden again, back to the top
        snprintf(g_page_seen, sizeof g_page_seen, "%s", title);
        memset(g_show_spoiler, 0, sizeof g_show_spoiler);
        ImGui::SetScrollY(0);
    }
    ImGui::TextColored(k_gold, "%s", *title ? title : "Guide");
    dim_text(state == WIKI_LOADING ? "Loading..." : status);
    ImGui::PushTextWrapPos(0);
    for (int i = 0; i < nsec && state == WIKI_READY; i++) {
        ImGui::PushID(i);
        if (!*sec[i].heading) ImGui::TextUnformatted(sec[i].text);
        else {
            if (sec[i].level > 2) ImGui::Indent(16 * scale);
            ImGui::Spacing();
            ImGui::TextColored(sec[i].level > 2 ? ImVec4(0.85f, 0.80f, 0.65f, 1) : k_gold, "%s", sec[i].heading);
            if (sec[i].spoiler && i < 128 && !g_show_spoiler[i]) {
                if (ImGui::SmallButton("Show solution (spoiler)")) g_show_spoiler[i] = true;
            } else if (*sec[i].text) ImGui::TextUnformatted(sec[i].text);
            if (sec[i].level > 2) ImGui::Unindent(16 * scale);
        }
        ImGui::PopID();
    }
    ImGui::PopTextWrapPos();
    wiki_unlock_page();
    ImGui::EndChild();
    if (ImGui::Button("Open in browser: Zelda Wiki")) { char u[300]; wiki_url(0, u, sizeof u); SDL_OpenURL(u); }
    ImGui::SameLine();
    if (ImGui::Button("Zelda Dungeon")) { char u[300]; wiki_url(1, u, sizeof u); SDL_OpenURL(u); }
    ImGui::SameLine();
    dim_text("Solutions stay hidden until you ask.");
}

// ---------------------------------------------------------------- Amiibo
// Zelda-series figures (8-byte amiibo IDs, from the community AmiiboAPI database) and what BotW gives for them.
struct Figure { uint64_t id; const char *name, *gives; };
static const Figure k_figures[] = {
    {0x01030000024f0902ull, "Midna & Wolf Link", "Wolf Link fights beside you (his hearts are saved on the figure)"},
    {0x0100000000040002ull, "Link (Super Smash Bros.)", "Epona, plus weapons and materials"},
    {0x0100000003530902ull, "Link - Archer (Breath of the Wild)", "Bows, arrows and materials"},
    {0x0100000003540902ull, "Link - Rider (Breath of the Wild)", "Weapons, horse gear chance, materials"},
    {0x0101000003560902ull, "Zelda (Breath of the Wild)", "Weapons, materials, rare chance"},
    {0x0140000003550902ull, "Guardian", "Ancient parts and Guardian weapons"},
    {0x01410000035c0902ull, "Bokoblin", "Monster parts and Bokoblin weapons"},
    {0x0105000003580902ull, "Daruk", "Champion series drops"},
    {0x01070000035a0902ull, "Mipha", "Champion series drops"},
    {0x01080000035b0902ull, "Revali", "Champion series drops"},
    {0x0106000003590902ull, "Urbosa", "Champion series drops"},
    {0x01000000034f0902ull, "8-Bit Link", "Hero of Hyrule armor pieces, Sword of the Hero (rare)"},
    {0x01000000034b0902ull, "Link - Ocarina of Time", "Hero of Time armor, Biggoron's Sword (rare)"},
    {0x01000000034c0902ull, "Link - Majora's Mask", "Fierce Deity armor and sword (rare)"},
    {0x01000000034d0902ull, "Link - Twilight Princess", "Twilight armor, Sword of the Six Sages (rare)"},
    {0x01000000034e0902ull, "Link - Skyward Sword", "Sky armor, Goddess Sword (rare)"},
    {0x0100010003500902ull, "Toon Link - The Wind Waker", "Wind Waker armor, Sea-Breeze Boomerang (rare)"},
    {0x0101000003520902ull, "Toon Zelda - The Wind Waker", "Sea-Breeze Shield (rare), materials"},
    {0x0100000003990902ull, "Link - Link's Awakening", "Materials and food"},
    {0x01020100001b0002ull, "Ganondorf (Super Smash Bros.)", "Twilight Bow chance, weapons"},
    {0x0101010000170002ull, "Sheik (Super Smash Bros.)", "Weapons and materials"},
    {0x0100010000160002ull, "Toon Link (Super Smash Bros.)", "Weapons and materials"},
    {0x01000000037c0002ull, "Young Link (Super Smash Bros.)", "Weapons and materials"},
    {0x01010000000e0002ull, "Zelda (Super Smash Bros.)", "Weapons and materials"},
    {0x0000000000000002ull, "Mario (any other series)", "Random materials, food and rupees"},
};

extern "C" {
#include "../services/nfp.h"
}

void cheat_tab_amiibo(float scale) {
    static int sel = 0;
    ImGui::PushTextWrapPos(0);
    dim_text("A virtual amiibo reader. Pick a figure and press Place on reader, then use the amiibo rune in the game "
             "(hold L, choose the amiibo rune, press L again). Each scan counts as a new figure, so there is no "
             "once-a-day limit. Turn amiibo on first: Options > Gameplay > amiibo.");
    ImGui::PopTextWrapPos();
    float foot = ImGui::GetFrameHeightWithSpacing() * 2.4f;
    ImGui::BeginChild("##figs", ImVec2(0, -foot), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)(sizeof k_figures / sizeof *k_figures); i++) {
        ImGui::PushID(i);
        if (ImGui::Selectable(k_figures[i].name, sel == i, ImGuiSelectableFlags_AllowDoubleClick)) {
            sel = i;
            if (ImGui::IsMouseDoubleClicked(0)) nfp_virtual_place(k_figures[i].id);
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f + ImGui::GetCursorStartPos().x);
        ImGui::TextColored(k_dim, "%s", k_figures[i].gives);
        ImGui::PopID();
    }
    ImGui::EndChild();
    char st[96];
    nfp_virtual_status(st, sizeof st);
    ImGui::TextColored(k_gold, "%s", k_figures[sel].name);
    ImGui::SameLine();
    dim_text(st);
    if (ImGui::Button("Place on reader", ImVec2(320 * scale, 0))) nfp_virtual_place(k_figures[sel].id);
    ImGui::SameLine();
    dim_text("Figure data (like Wolf Link's hearts) is kept in cheats/amiibo/.");
}

// ---------------------------------------------------------------- Shrine solver (top of the Warp tab)
void cheat_shrine_box(float scale) {
    const CheatShrine *s = cheat_current_shrine();
    ImGui::TextColored(k_gold, "Shrine solver");
    ImGui::SameLine();
    if (!s) { dim_text("works only inside a shrine: enter one, then come back here."); return; }
    ImGui::Text("You're in %s.", s->name);
    if (ImGui::Button("Warp to the end (monk)", ImVec2(380 * scale, 0))) cheat_shrine_warp(0);
    ImGui::SameLine();
    if (ImGui::Button("Back to the entrance")) cheat_shrine_warp(1);
    for (int i = 0; i < s->nchest; i++) {
        char b[24];
        snprintf(b, sizeof b, "Chest %d", i + 1);
        ImGui::SameLine();
        if (ImGui::Button(b)) cheat_shrine_warp(2 + i);
    }
    ImGui::SameLine();
    if (ImGui::Button("Guide##shrine")) cheat_guide_show(s->name, "shrine");
}
