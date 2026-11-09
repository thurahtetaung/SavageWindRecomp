// Companion overlay (runtime/companion.h): messages from a companion app at the top centre of the screen, and a
// beacon line with the distance and a direction arrow to a place. Display only; no input is taken.
#include "companion_overlay.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
#include "../companion.h"
#include "../cheats.h"
#include "../platform.h"
bool control_camera_angles(double *yaw, double *pitch);  // runtime/control_game.c
}

bool companion_overlay_needed(void) { return companion_overlay_wanted(); }

// Screen angle (radians, clockwise from up) of a place for a camera with BOTW's yaw convention: yaw 0 looks along +z,
// 90 along +x. Looking along +z, +x is on the left, so a positive relative yaw turns the arrow counter-clockwise.
static float arrow_angle(float px, float pz, float tx, float tz, double cam_yaw_deg) {
    double target = atan2((double)(tx - px), (double)(tz - pz)) * 180.0 / M_PI;
    double rel = fmod(target - cam_yaw_deg + 540.0, 360.0) - 180.0;
    return (float)(-rel * M_PI / 180.0);
}

static void draw_arrow(ImDrawList *dl, ImVec2 c, float r, float angle, ImU32 col) {
    float s = sinf(angle), co = cosf(angle);
    auto at = [&](float x, float y) { return ImVec2(c.x + x * co - y * s, c.y + x * s + y * co); };
    dl->AddTriangleFilled(at(0, -r), at(r * 0.7f, r * 0.75f), at(-r * 0.7f, r * 0.75f), col);
    dl->AddTriangleFilled(at(r * 0.7f, r * 0.75f), at(0, r * 0.35f), at(-r * 0.7f, r * 0.75f), IM_COL32(20, 32, 30, 200));
}

void companion_overlay_draw(ImGuiIO &io, float scale) {
    CompanionToast toasts[COMPANION_TOASTS];
    int n = companion_toasts(toasts, plat_now_ns());
    CompanionBeacon beacon;
    bool have_beacon = companion_beacon(&beacon);
    if (!n && !have_beacon) return;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImFont *font = ImGui::GetFont();
    float fs = ImGui::GetFontSize() * 0.78f;
    float y = 22 * scale;
    const float maxw = fminf(io.DisplaySize.x * 0.56f, 900 * scale);
    for (int i = 0; i < n; i++) {
        char head[COMPANION_NAME_CAP + 8];
        snprintf(head, sizeof head, "%s", toasts[i].from);
        ImVec2 body = font->CalcTextSizeA(fs, FLT_MAX, maxw - 36 * scale, toasts[i].text);
        float w = fmaxf(body.x, font->CalcTextSizeA(fs * 0.8f, FLT_MAX, 0, head).x) + 36 * scale;
        float h = body.y + fs * 0.8f + 26 * scale;
        ImVec2 a(io.DisplaySize.x * 0.5f - w * 0.5f, y), b(a.x + w, y + h);
        float alpha = i == 0 ? 1.0f : 0.78f;
        dl->AddRectFilled(a, b, IM_COL32(18, 32, 30, (int)(215 * alpha)), 10 * scale);
        dl->AddRect(a, b, IM_COL32(222, 190, 112, (int)(200 * alpha)), 10 * scale, 0, 1.5f * scale);
        dl->AddText(font, fs * 0.8f, ImVec2(a.x + 18 * scale, a.y + 9 * scale), IM_COL32(222, 190, 112, (int)(255 * alpha)), head);
        dl->AddText(font, fs, ImVec2(a.x + 18 * scale, a.y + 13 * scale + fs * 0.8f), IM_COL32(244, 239, 224, (int)(255 * alpha)),
                    toasts[i].text, NULL, maxw - 36 * scale);
        y = b.y + 8 * scale;
    }
    if (!have_beacon) return;
    float p[3];
    double yaw, pitch;
    char line[COMPANION_NAME_CAP + 48];
    bool located = cheat_link_position(p);
    bool facing = located && control_camera_angles(&yaw, &pitch);
    if (located)
        snprintf(line, sizeof line, "%s  ·  %.0f m", beacon.name, hypotf(beacon.x - p[0], beacon.z - p[2]));
    else
        snprintf(line, sizeof line, "%s", beacon.name);
    float r = fs * 0.62f;
    ImVec2 t = font->CalcTextSizeA(fs, FLT_MAX, 0, line);
    float w = t.x + (facing ? r * 2 + 30 * scale : 28 * scale), h = fmaxf(t.y, r * 2) + 16 * scale;
    ImVec2 a(io.DisplaySize.x * 0.5f - w * 0.5f, y), b(a.x + w, y + h);
    dl->AddRectFilled(a, b, IM_COL32(18, 32, 30, 190), h * 0.5f);
    float tx = a.x + 14 * scale;
    if (facing) {
        draw_arrow(dl, ImVec2(tx + r, a.y + h * 0.5f), r, arrow_angle(p[0], p[2], beacon.x, beacon.z, yaw),
                   IM_COL32(236, 194, 84, 255));
        tx += r * 2 + 10 * scale;
    }
    dl->AddText(font, fs, ImVec2(tx, a.y + (h - t.y) * 0.5f), IM_COL32(244, 239, 224, 255), line);
}
