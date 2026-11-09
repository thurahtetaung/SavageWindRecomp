// Companion overlay commands (runtime/companion.h). Both are player safe: they only change what the overlay shows.
#include "companion.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "platform.h"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static CompanionToast g_toasts[COMPANION_TOASTS];  // [0] newest
static CompanionBeacon g_beacon;
static volatile int g_wanted;  // cheap flag for the render thread: anything to draw

// Printable ASCII and UTF-8 continuation/lead bytes; control characters become spaces, runs of space collapse.
static void clean(char *out, size_t cap, const char *in) {
    size_t n = 0;
    int space = 1;
    for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p && n + 1 < cap; p++) {
        unsigned char c = *p < 0x20 || *p == 0x7f ? ' ' : *p;
        if (c == ' ' && space) continue;
        space = c == ' ';
        out[n++] = (char)c;
    }
    while (n && out[n - 1] == ' ') n--;
    // never end inside a UTF-8 sequence
    size_t k = n;
    while (k && ((unsigned char)out[k - 1] & 0xc0) == 0x80) k--;
    if (k && (unsigned char)out[k - 1] >= 0xc0) {
        unsigned char lead = (unsigned char)out[k - 1];
        size_t need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
        if (n - (k - 1) < need) n = k - 1;
    }
    out[n] = 0;
}

static void update_wanted(uint64_t now) {
    int any = g_beacon.active;
    for (int i = 0; i < COMPANION_TOASTS; i++) any |= g_toasts[i].text[0] && g_toasts[i].until_ns > now;
    g_wanted = any;
}

bool companion_notify(const char *text, const char *from, double seconds, uint64_t now_ns) {
    char t[COMPANION_TEXT_CAP];
    clean(t, sizeof t, text);
    if (!t[0]) return false;
    if (!(seconds >= 2)) seconds = 8;  // (NaN and below 2: the default)
    if (seconds > 30) seconds = 30;
    pthread_mutex_lock(&g_lock);
    memmove(&g_toasts[1], &g_toasts[0], sizeof g_toasts[0] * (COMPANION_TOASTS - 1));
    memset(&g_toasts[0], 0, sizeof g_toasts[0]);
    snprintf(g_toasts[0].text, sizeof g_toasts[0].text, "%s", t);
    clean(g_toasts[0].from, sizeof g_toasts[0].from, from && *from ? from : "Fieldnotes");
    g_toasts[0].until_ns = now_ns + (uint64_t)(seconds * 1e9);
    update_wanted(now_ns);
    pthread_mutex_unlock(&g_lock);
    return true;
}

void companion_notify_clear(void) {
    pthread_mutex_lock(&g_lock);
    memset(g_toasts, 0, sizeof g_toasts);
    update_wanted(0);
    pthread_mutex_unlock(&g_lock);
}

int companion_toasts(CompanionToast out[COMPANION_TOASTS], uint64_t now_ns) {
    int n = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < COMPANION_TOASTS; i++)
        if (g_toasts[i].text[0] && g_toasts[i].until_ns > now_ns) out[n++] = g_toasts[i];
    update_wanted(now_ns);
    pthread_mutex_unlock(&g_lock);
    return n;
}

void companion_beacon_set(const char *name, float x, float z) {
    pthread_mutex_lock(&g_lock);
    clean(g_beacon.name, sizeof g_beacon.name, name && *name ? name : "Beacon");
    g_beacon.x = x;
    g_beacon.z = z;
    g_beacon.active = isfinite(x) && isfinite(z);
    g_wanted = g_wanted || g_beacon.active;
    pthread_mutex_unlock(&g_lock);
}

void companion_beacon_clear(void) {
    pthread_mutex_lock(&g_lock);
    memset(&g_beacon, 0, sizeof g_beacon);
    update_wanted(plat_now_ns());
    pthread_mutex_unlock(&g_lock);
}

bool companion_beacon(CompanionBeacon *out) {
    pthread_mutex_lock(&g_lock);
    *out = g_beacon;
    pthread_mutex_unlock(&g_lock);
    return out->active;
}

bool companion_overlay_wanted(void) { return g_wanted != 0; }

static void join(char *out, size_t cap, int argc, char **argv, int from) {
    out[0] = 0;
    for (int i = from; i < argc; i++) {
        if (i > from) strncat(out, " ", cap - strlen(out) - 1);
        strncat(out, argv[i], cap - strlen(out) - 1);
    }
}

// notify [--seconds N] [--from NAME] <text...> | notify clear
static void cmd_notify(int argc, char **argv, ControlReply *r) {
    if (argc == 2 && !strcmp(argv[1], "clear")) { companion_notify_clear(); control_text(r, "Messages cleared"); return; }
    double seconds = 8;
    const char *from = "Fieldnotes";
    int i = 1;
    while (i + 1 < argc && argv[i][0] == '-' && argv[i][1] == '-') {
        if (!strcmp(argv[i], "--seconds")) seconds = strtod(argv[i + 1], NULL);
        else if (!strcmp(argv[i], "--from")) from = argv[i + 1];
        else { control_fail(r, "Unknown option %s", argv[i]); return; }
        i += 2;
    }
    char text[COMPANION_TEXT_CAP * 2];
    join(text, sizeof text, argc, argv, i);
    if (!companion_notify(text, from, seconds, plat_now_ns())) {
        control_fail(r, "Usage: notify [--seconds 2-30] [--from name] <text> | notify clear");
        return;
    }
    control_field_bool(r, "shown", true);
    control_text(r, "Showing: %s", text);
}

// beacon <x> <z> [name...] | beacon clear | beacon
static void cmd_beacon(int argc, char **argv, ControlReply *r) {
    if (argc == 1) {
        CompanionBeacon b;
        control_field_bool(r, "active", companion_beacon(&b));
        if (b.active) {
            control_field_str(r, "name", b.name);
            control_field_num(r, "x", b.x);
            control_field_num(r, "z", b.z);
        }
        return;
    }
    if (argc == 2 && !strcmp(argv[1], "clear")) { companion_beacon_clear(); control_text(r, "Beacon cleared"); return; }
    char *e1, *e2;
    float x = argc >= 3 ? strtof(argv[1], &e1) : NAN, z = argc >= 3 ? strtof(argv[2], &e2) : NAN;
    if (argc < 3 || *e1 || *e2 || !isfinite(x) || !isfinite(z) || fabsf(x) > 20000 || fabsf(z) > 20000) {
        control_fail(r, "Usage: beacon <x> <z> [name] | beacon clear");
        return;
    }
    char name[COMPANION_NAME_CAP * 2];
    join(name, sizeof name, argc, argv, 3);
    companion_beacon_set(name, x, z);
    control_field_bool(r, "active", true);
    control_text(r, "Beacon set at %.0f, %.0f", x, z);
}

void companion_init(void) {
    static const ControlCmd cmds[] = {
        {"notify", "[--seconds N] [--from name] <text> | clear", "Show a short companion message over the game", cmd_notify, CONTROL_PLAYER},
        {"beacon", "<x> <z> [name] | clear", "Show the direction and distance to a place over the game", cmd_beacon, CONTROL_PLAYER},
    };
    control_register(cmds, (int)(sizeof cmds / sizeof *cmds));
}
