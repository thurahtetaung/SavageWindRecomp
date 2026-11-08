// Local cheat actions (runtime/cheats.h). Requests are queued from any thread and run on the presenting guest thread,
// calling the game's own functions so its bookkeeping stays consistent:
//   uking::ui::PauseMenuDataMgr::itemGet(name, value, modifier)   1.9.0 0x7000d921bc (located by its "PouchGet" report)
//   ksys::gdt increaseS32(delta, name, debug)                     1.9.0 0x7000d34b30 (adds: verified live)
//   ksys::gdt::TriggerParam::getS32(param, &out, name, check, 1)  1.9.0 0x7001137684 (a CurrentRupee reader's callee)
// Warps reuse the control layer's teleport (runtime/control_tp.c). The entry words of every called function are
// checked once before the first call; any mismatch disables the cheats instead of calling into unknown code.
#include "cheats.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_timer.h>

#include "control.h"
#include "game_sites.h"
#include "rt.h"
#include "local_assists.h"
#include "framepace.h"

#define GOT_GDT_MGR 0x7001d66478ull       // &ksys::gdt::Manager::sInstance
#define GOT_SAFESTRING_VT 0x7001d66260ull // sead::SafeString vtable (+0x10: the vptr)
#define GOT_POUCH_MGR 0x7001d67af0ull     // &uking::ui::PauseMenuDataMgr::sInstance
#define FN_ITEM_GET 0x7000d921bcull
#define FN_GDT_SET_S32 0x7000d34b30ull
#define FN_TP_GET_S32 0x7001137684ull
#define FN_TP_GET_BOOL 0x70011375ecull

static const struct { uint64_t addr; uint32_t word; } k_guard[] = {
    {FN_ITEM_GET, 0xd10243ff},     // sub sp, sp, #0x90
    {FN_GDT_SET_S32, 0xd100c3ff},  // sub sp, sp, #0x30
    {FN_TP_GET_S32, 0xa9bd7bfd},   // stp x29, x30, [sp, #-0x30]!
    {FN_TP_GET_BOOL, 0xa9bd7bfd},  // stp x29, x30, [sp, #-0x30]!
    {0x700061f794, 0x396fa123},    // ldrb w3, [x9, #0xbe8]   (the bool getter's permission byte)
    {0x7000b4f9a8, 0x396f2103},    // ldrb w3, [x8, #0xbc8]   (the getter's permission byte)
    {0x7000b4f9b0, 0xf945e109},    // ldr x9, [x8, #0xbc0]    (the getter's TriggerParam)
};

static uint32_t rd32(uint64_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static uint64_t rd64(uint64_t a) { return *(volatile uint64_t *)(uintptr_t)a; }

// ---- catalog
static CheatItem *g_items;
static int g_nitems;
static CheatPlace *g_places;
static int g_nplaces;
static CheatMark *g_marks;
static int g_nmarks;
static pthread_once_t g_catalog_once = PTHREAD_ONCE_INIT;

static char *dup_field(char **p) {
    char *s = *p, *t = strchr(s, '\t');
    if (t) { *t = 0; *p = t + 1; } else { char *e = s + strcspn(s, "\r\n"); *e = 0; *p = e; }
    return strdup(s);
}
static void load_catalog(void) {
    char line[512];
    FILE *f = fopen("cheats/items.tsv", "rb");
    int cap = 0;
    while (f && fgets(line, sizeof line, f)) {
        char *p = line;
        if (g_nitems == cap) { cap = cap ? cap * 2 : 256; g_items = realloc(g_items, (size_t)cap * sizeof *g_items); }
        CheatItem *it = &g_items[g_nitems];
        it->category = dup_field(&p); it->actor = dup_field(&p); it->name = dup_field(&p);
        it->value = atoi(p);
        if (*it->actor) g_nitems++;
    }
    if (f) fclose(f);
    f = fopen("cheats/places.tsv", "rb");
    cap = 0;
    while (f && fgets(line, sizeof line, f)) {
        char *p = line;
        if (g_nplaces == cap) { cap = cap ? cap * 2 : 128; g_places = realloc(g_places, (size_t)cap * sizeof *g_places); }
        CheatPlace *pl = &g_places[g_nplaces];
        pl->category = dup_field(&p); pl->name = dup_field(&p);
        if (sscanf(p, "%f\t%f\t%f\t%f", &pl->x, &pl->y, &pl->z, &pl->yaw) == 4) g_nplaces++;
    }
    if (f) fclose(f);
    f = fopen("cheats/map.tsv", "rb");
    cap = 0;
    while (f && fgets(line, sizeof line, f)) {
        char *p = line;
        if (g_nmarks == cap) { cap = cap ? cap * 2 : 1024; g_marks = realloc(g_marks, (size_t)cap * sizeof *g_marks); }
        CheatMark *m = &g_marks[g_nmarks];
        m->kind = dup_field(&p); m->name = dup_field(&p);
        char *xs = dup_field(&p), *ys = dup_field(&p), *zs = dup_field(&p);
        m->x = (float)atof(xs); m->y = (float)atof(ys); m->z = (float)atof(zs);
        free(xs); free(ys); free(zs);
        m->found_flag = dup_field(&p); m->done_flag = dup_field(&p);
        m->found = m->done = -1;
        if (*m->kind) g_nmarks++;
    }
    if (f) fclose(f);
    LOG("[cheats] catalog: %d items, %d places, %d map markers\n", g_nitems, g_nplaces, g_nmarks);
}
int cheat_item_count(void) { pthread_once(&g_catalog_once, load_catalog); return g_nitems; }
const CheatItem *cheat_item(int i) { return i >= 0 && i < cheat_item_count() ? &g_items[i] : NULL; }
int cheat_place_count(void) { pthread_once(&g_catalog_once, load_catalog); return g_nplaces; }
const CheatPlace *cheat_place(int i) { return i >= 0 && i < cheat_place_count() ? &g_places[i] : NULL; }

static bool contains_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t k = 0;
        while (k < n && hay[k] && tolower((unsigned char)hay[k]) == tolower((unsigned char)needle[k])) k++;
        if (k == n) return true;
    }
    return !n;
}
int cheat_mark_count(void) { pthread_once(&g_catalog_once, load_catalog); return g_nmarks; }
const CheatMark *cheat_mark(int i) { return i >= 0 && i < cheat_mark_count() ? &g_marks[i] : NULL; }
static volatile uint64_t g_marks_wanted;  // the tick count until which the map wants fresh flags
static volatile uint64_t g_ticks;         // game ticks seen (cheatstate)
void cheat_marks_want(void) { g_marks_wanted = g_ticks + 120; }
bool cheat_link_position(float p[3]) {
    if (!g_site.player_info) return false;
    uint64_t pi = *(volatile uint64_t *)(uintptr_t)g_site.player_info;
    if (!pi) return false;
    for (int i = 0; i < 3; i++) p[i] = *(volatile float *)(uintptr_t)(pi + 0x88 + 4 * i);
    return p[0] != 0 || p[2] != 0;
}

const CheatItem *cheat_item_find(const char *q) {
    for (int i = 0; i < cheat_item_count(); i++)
        if (!strcmp(g_items[i].actor, q) || !strcasecmp(g_items[i].name, q)) return &g_items[i];
    for (int i = 0; i < cheat_item_count(); i++)
        if (contains_ci(g_items[i].name, q)) return &g_items[i];
    return NULL;
}
const CheatPlace *cheat_place_find(const char *q) {
    for (int i = 0; i < cheat_place_count(); i++)
        if (!strcasecmp(g_places[i].name, q)) return &g_places[i];
    for (int i = 0; i < cheat_place_count(); i++)
        if (contains_ci(g_places[i].name, q)) return &g_places[i];
    return NULL;
}

// ---- state
enum { R_GIVE, R_RUPEES_ADD, R_RUPEES_SET, R_WARP, R_FLAG, R_FLAG_GET };
typedef struct { int kind; char name[96]; int a, b; float x, y, z; } Request;
#define QMAX 32
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Request g_queue[QMAX];
static int g_head, g_count;
static char g_status[160] = "Ready.";
static volatile int g_rupees = -1;
static int g_supported = -1;  // -1 unchecked
static char g_query[200];
static volatile int g_query_done;

static void set_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_status(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&g_lock);
    memcpy(g_status, buf, sizeof buf);
    pthread_mutex_unlock(&g_lock);
    LOG("[cheats] %s\n", buf);
}
void cheat_status(char *buf, int n) {
    pthread_mutex_lock(&g_lock);
    snprintf(buf, (size_t)n, "%s", g_status);
    pthread_mutex_unlock(&g_lock);
}
int cheat_rupees_cached(void) { return g_rupees; }
int cheat_pending(void) { pthread_mutex_lock(&g_lock); int n = g_count; pthread_mutex_unlock(&g_lock); return n; }

bool cheats_supported(void) {
    if (g_supported < 0) {
        int ok = g_game_version && !strcmp(g_game_version, "1.9.0");
        for (size_t i = 0; ok && i < sizeof k_guard / sizeof *k_guard; i++)
            if (rd32(k_guard[i].addr) != k_guard[i].word) {
                LOG("[cheats] guard mismatch at %llx\n", (unsigned long long)k_guard[i].addr);
                ok = 0;
            }
        if (!g_game_version || !*g_game_version) return false;  // (not loaded yet: decide later)
        g_supported = ok;
        if (!ok) set_status("Cheats are disabled: this game version is not the verified 1.9.0 build.");
    }
    return g_supported == 1;
}

bool cheats_world_ready(void) {
    if (!g_site.player_info || !rd64(g_site.player_info)) return false;
    uint64_t pinfo = rd64(g_site.player_info);
    if (!rd64(pinfo + 0x60)) return false;                      // PlayerInfo's player actor
    if (g_site.pause_state && rd32(g_site.pause_state) != 0) return false;
    if (framepace_in_event() || framepace_loading_ui() || framepace_title_ui()) return false;  // cutscenes, loading
    return true;
}

static bool push(const Request *r) {
    pthread_mutex_lock(&g_lock);
    bool ok = g_count < QMAX;
    if (ok) { g_queue[(g_head + g_count) % QMAX] = *r; g_count++; }
    pthread_mutex_unlock(&g_lock);
    return ok;
}
bool cheat_give(const char *actor, int value, int count) {
    if (!actor || !*actor || strlen(actor) >= 96 || count < 1 || count > 999) return false;
    Request r = {R_GIVE};
    snprintf(r.name, sizeof r.name, "%s", actor);
    r.a = value; r.b = count;
    return push(&r);
}
bool cheat_rupees_add(int delta) { Request r = {R_RUPEES_ADD}; r.a = delta; return push(&r); }
bool cheat_rupees_set(int value) { Request r = {R_RUPEES_SET}; r.a = value; return push(&r); }
bool cheat_warp(float x, float y, float z) { Request r = {R_WARP}; r.x = x; r.y = y; r.z = z; return push(&r); }
bool cheat_flag_s32_set(const char *name, int value) {
    if (!name || !*name || strlen(name) >= 96) return false;
    Request r = {R_FLAG};
    snprintf(r.name, sizeof r.name, "%s", name);
    r.a = value;
    return push(&r);
}

// ---- calls into the game: a SafeString and the out value live on the guest stack, below its current top
typedef struct { Cpu *c; Cpu saved; uint64_t base; } Frame;
static bool frame_begin(Frame *f, const char *name) {
    f->c = kernel_current_cpu();
    if (!f->c) return false;
    f->saved = *f->c;
    f->base = (f->c->sp - 0x400) & ~15ull;
    f->c->sp = f->base;
    char *str = (char *)(uintptr_t)(f->base + 0x100);
    snprintf(str, 0x200, "%s", name ? name : "");
    uint64_t *ss = (uint64_t *)(uintptr_t)(f->base + 0x80);  // sead::SafeString {vptr, cstr}
    ss[0] = rd64(GOT_SAFESTRING_VT) + 0x10;
    ss[1] = f->base + 0x100;
    *(int32_t *)(uintptr_t)(f->base + 0x70) = 0;
    return true;
}
static void frame_end(Frame *f) { *f->c = f->saved; }

static uint64_t gdt_mgr(void) { uint64_t p = rd64(GOT_GDT_MGR); return p ? rd64(p) : 0; }

static bool gdt_get_s32(const char *name, int *out) {
    uint64_t mgr = gdt_mgr();
    if (!mgr || !rd64(mgr + 0xbc0) || !rd64(rd64(mgr + 0xbc0))) return false;
    Frame f;
    if (!frame_begin(&f, name)) return false;
    Cpu *c = f.c;
    c->x[0] = rd64(rd64(mgr + 0xbc0));
    c->x[1] = f.base + 0x70;
    c->x[2] = f.base + 0x80;
    c->x[3] = *(volatile uint8_t *)(uintptr_t)(mgr + 0xbc8);
    c->x[4] = 1;
    guest_call(c, FN_TP_GET_S32);
    bool ok = c->x[0] & 1;
    *out = *(int32_t *)(uintptr_t)(f.base + 0x70);
    frame_end(&f);
    return ok;
}
static bool gdt_get_bool(const char *name, bool *out) {
    uint64_t mgr = gdt_mgr();
    if (!mgr || !rd64(mgr + 0xbe0) || !rd64(rd64(mgr + 0xbe0))) return false;
    Frame f;
    if (!frame_begin(&f, name)) return false;
    Cpu *c = f.c;
    c->x[0] = rd64(rd64(mgr + 0xbe0));
    c->x[1] = f.base + 0x70;
    c->x[2] = f.base + 0x80;
    c->x[3] = *(volatile uint8_t *)(uintptr_t)(mgr + 0xbe8);
    c->x[4] = 1;
    guest_call(c, FN_TP_GET_BOOL);
    bool ok = c->x[0] & 1;
    *out = *(volatile uint8_t *)(uintptr_t)(f.base + 0x70) & 1;
    frame_end(&f);
    return ok;
}
static bool gdt_add_s32(const char *name, int delta) {
    if (!gdt_mgr()) return false;
    Frame f;
    if (!frame_begin(&f, name)) return false;
    f.c->x[0] = (uint32_t)delta;
    f.c->x[1] = f.base + 0x80;
    f.c->x[2] = 0;
    guest_call(f.c, FN_GDT_SET_S32);
    frame_end(&f);
    return true;
}
static bool pouch_item_get(const char *actor, int value) {
    uint64_t p = rd64(GOT_POUCH_MGR), mgr = p ? rd64(p) : 0;
    if (!mgr) return false;
    Frame f;
    if (!frame_begin(&f, actor)) return false;
    f.c->x[0] = mgr;
    f.c->x[1] = f.base + 0x80;
    f.c->x[2] = (uint32_t)value;
    f.c->x[3] = 0;
    guest_call(f.c, FN_ITEM_GET);
    frame_end(&f);
    return true;
}

static bool gdt_set_s32(const char *name, int value) {
    int cur;
    if (!gdt_get_s32(name, &cur)) return false;
    return value == cur || gdt_add_s32(name, value - cur);
}

static uint32_t pouch_count(void) {  // PauseMenuDataMgr's item list (+0x68: OffsetList; +0x78 its count)
    uint64_t p = rd64(GOT_POUCH_MGR), mgr = p ? rd64(p) : 0;
    return mgr ? rd32(mgr + 0x78) : 0;
}

// "s:Name" (s32, true when > 0) or "b:Name" (bool); -1 when unreadable or empty
static int flag_eval(const char *spec) {
    if (!spec || strlen(spec) < 3) return -1;
    if (spec[0] == 's') { int v; return gdt_get_s32(spec + 2, &v) ? v > 0 : -1; }
    bool b;
    return gdt_get_bool(spec + 2, &b) ? b : -1;
}

static const CheatItem *item_by_actor(const char *actor) {
    for (int i = 0; i < cheat_item_count(); i++) if (!strcmp(g_items[i].actor, actor)) return &g_items[i];
    return NULL;
}
static bool stackable(const CheatItem *it) {
    return it && strcmp(it->category, "Weapons") && strcmp(it->category, "Bows") && strcmp(it->category, "Shields") &&
           strncmp(it->category, "Armor", 5);
}

void control_tp_run(int argc, char **argv, ControlReply *r);  // runtime/control_tp.c

static void run(const Request *r) {
    switch (r->kind) {
    case R_GIVE: {
        const CheatItem *it = item_by_actor(r->name);
        const char *label = it ? it->name : r->name;
        if (stackable(it)) {
            if (!pouch_item_get(r->name, r->b)) { set_status("Could not add %s.", label); return; }
        } else {  // one call per copy; the game refuses equipment when that pouch tab is full
            int added = 0;
            for (int i = 0; i < r->b; i++) {
                uint32_t before = pouch_count();
                if (!pouch_item_get(r->name, r->a)) break;
                if (pouch_count() == before) break;
                added++;
            }
            if (added < r->b) {
                if (added) set_status("Added %d of %d x %s: no more room in that pouch tab.", added, r->b, label);
                else set_status("No room for %s: that pouch tab is full (sell or drop something first).", label);
                return;
            }
        }
        set_status("Added %d x %s.", r->b, label);
        break;
    }
    case R_RUPEES_ADD:
    case R_RUPEES_SET: {
        int cur = 0;
        if (!gdt_get_s32("CurrentRupee", &cur)) { set_status("Could not read rupees."); return; }
        long long v = r->kind == R_RUPEES_SET ? r->a : (long long)cur + r->a;
        if (v < 0) v = 0;
        if (v > 999999) v = 999999;
        gdt_set_s32("CurrentRupee", (int)v);
        g_rupees = (int)v;
        set_status("Rupees: %d -> %lld.", cur, v);
        break;
    }
    case R_FLAG_GET: {
        int v; bool b;
        if (gdt_get_s32(r->name, &v)) snprintf(g_query, sizeof g_query, "%s = %d (s32)", r->name, v);
        else if (gdt_get_bool(r->name, &b)) snprintf(g_query, sizeof g_query, "%s = %s (bool)", r->name, b ? "true" : "false");
        else snprintf(g_query, sizeof g_query, "%s: no such s32 or bool flag", r->name);
        g_query_done = 1;
        break;
    }
    case R_FLAG:
        set_status(gdt_set_s32(r->name, r->a) ? "Set %s = %d." : "Could not set %s = %d.", r->name, r->a);
        break;
    case R_WARP: {
        char xs[32], ys[32], zs[32], msg[256];
        snprintf(xs, sizeof xs, "%.3f", r->x); snprintf(ys, sizeof ys, "%.3f", r->y); snprintf(zs, sizeof zs, "%.3f", r->z);
        char *argv[] = {(char *)"teleport", xs, ys, zs};
        if (control_run_fn(control_tp_run, 4, argv, msg, sizeof msg)) set_status("Warping to %.0f, %.0f, %.0f.", r->x, r->y, r->z);
        else set_status("Warp failed: %s", msg);
        break;
    }
    }
}

static int g_why = 1;            // the last reason the game tick skipped (cheatstate)

// Take the next request of the kind this thread runs: warps on the presenting thread (the teleport's own state
// machine lives there), everything else on the game thread.
static bool take(bool warp, Request *out) {
    pthread_mutex_lock(&g_lock);
    bool have = g_count > 0 && ((g_queue[g_head].kind == R_WARP) == warp);
    if (have) { *out = g_queue[g_head]; g_head = (g_head + 1) % QMAX; g_count--; }
    pthread_mutex_unlock(&g_lock);
    return have;
}

// Which shrine Link is in, guessed (no flag says so until the game saves): he was by a shrine's entrance moments
// before, then his position jumped far, to somewhere no map marker is (a shrine's own stage). Coming back out puts
// him by the entrance again, which ends the guess. Warps and fast travel land on a marker, so they do not count.
static int g_inside = -1, g_near_shrine = -1;
static uint64_t g_near_frame;
static float g_last[3];
static bool g_have_last;
int cheat_inside_shrine(void) { return g_inside; }
static float dist2(const CheatMark *m, const float p[3]) {
    float dx = m->x - p[0], dz = m->z - p[2];
    return dx * dx + dz * dz;
}
static void track_shrine(uint64_t frame) {
    float p[3];
    if (!cheat_link_position(p) || !cheat_mark_count()) return;
    int near = -1;
    float best = 1e30f, any = 1e30f;
    for (int i = 0; i < g_nmarks; i++) {
        if (g_marks[i].kind[0] == 'l') continue;  // (labels)
        float d = dist2(&g_marks[i], p);
        if (d < any) any = d;
        if (g_marks[i].kind[0] == 's' && d < best) { best = d; near = i; }
    }
    if (near >= 0 && best < 40 * 40) { g_near_shrine = near; g_near_frame = frame; }
    if (g_have_last) {
        float dx = p[0] - g_last[0], dz = p[2] - g_last[2];
        if (dx * dx + dz * dz > 300 * 300) {
            g_inside = g_near_shrine >= 0 && frame - g_near_frame < 900 && any > 150 * 150 ? g_near_shrine : -1;
            g_near_shrine = -1;
        }
    }
    memcpy(g_last, p, sizeof g_last);
    g_have_last = true;
}

// Presenting thread, once per presented frame.
void cheats_frame(uint64_t frame) {
    Request r;
    if (!cheats_supported() || !cheats_world_ready()) return;
    track_shrine(frame);
    if (take(true, &r)) { g_near_shrine = -1; run(&r); }
}

// Game thread: the cameras' controller reads its flags once a frame (tools/patches.py _cam_reverse, hle/input.c
// rt_cam_reverse), a point where the game itself reads game data, with no lock of ours held.
void cheats_game_tick(void) {
    g_ticks++;
    if (!kernel_current_cpu()) { g_why = 1; return; }
    if (!cheats_supported()) { g_why = 2; return; }
    if (!cheats_world_ready()) { g_why = 3; return; }
    g_why = 0;
    if (g_ticks % 30 == 0) {
        int v;
        if (gdt_get_s32("CurrentRupee", &v)) g_rupees = v;
    }
    if (g_ticks < g_marks_wanted && cheat_mark_count()) {  // the map is up: re-read 24 markers a tick
        static int next;
        for (int k = 0; k < 24; k++, next = (next + 1) % g_nmarks) {
            CheatMark *m = &g_marks[next];
            m->found = flag_eval(m->found_flag);
            m->done = flag_eval(m->done_flag);
        }
    }
    Request r;  // one request per tick, so a batch of gifts never stalls a frame
    if (take(false, &r)) run(&r);
}

// ---- console commands (player safe: they only change the player's own game)
static void reply_queued(ControlReply *rep, bool ok, const char *what) {
    if (!ok) { control_fail(rep, "Queue full or invalid request"); return; }
    control_field_str(rep, "queued", what);
    control_text(rep, "%s%s", what, cheats_world_ready() ? "" : " (runs when you are back in the game)");
}
static void cmd_give(int argc, char **argv, ControlReply *rep) {
    if (argc < 2) { control_fail(rep, "Usage: give <actor or English name> [count]"); return; }
    int count = 1, last = argc;
    if (argc > 2) { char *e; long n = strtol(argv[argc - 1], &e, 10); if (!*e && n > 0) { count = (int)n; last = argc - 1; } }
    char q[160] = "";
    for (int i = 1; i < last; i++) { if (i > 1) strncat(q, " ", sizeof q - strlen(q) - 1); strncat(q, argv[i], sizeof q - strlen(q) - 1); }
    const CheatItem *it = cheat_item_find(q);
    if (!it) { control_fail(rep, "No item matches \"%s\" (see cheats/items.tsv)", q); return; }
    char what[200];
    snprintf(what, sizeof what, "Giving %d x %s", count, it->name);
    reply_queued(rep, cheat_give(it->actor, it->value, count > 999 ? 999 : count), what);
}
static void cmd_rupees(int argc, char **argv, ControlReply *rep) {
    if (argc == 1) { control_field_int(rep, "rupees", cheat_rupees_cached()); return; }
    if (argc != 2) { control_fail(rep, "Usage: rupees [<amount> | +<amount> | -<amount> | max]"); return; }
    const char *a = argv[1];
    if (!strcmp(a, "max")) { reply_queued(rep, cheat_rupees_set(999999), "Rupees to 999999"); return; }
    char *e;
    long n = strtol(a, &e, 10);
    if (*e) { control_fail(rep, "Not a number: %s", a); return; }
    bool rel = a[0] == '+' || a[0] == '-';
    reply_queued(rep, rel ? cheat_rupees_add((int)n) : cheat_rupees_set((int)n), "Rupees updated");
}
static void cmd_warp(int argc, char **argv, ControlReply *rep) {
    if (argc < 2) { control_fail(rep, "Usage: warp <place name>  (shrines, towers, towns, stables: cheats/places.tsv)"); return; }
    char q[160] = "";
    for (int i = 1; i < argc; i++) { if (i > 1) strncat(q, " ", sizeof q - strlen(q) - 1); strncat(q, argv[i], sizeof q - strlen(q) - 1); }
    const CheatPlace *p = cheat_place_find(q);
    if (!p) { control_fail(rep, "No place matches \"%s\"", q); return; }
    char what[200];
    snprintf(what, sizeof what, "Warping to %s", p->name);
    reply_queued(rep, cheat_warp(p->x, p->y, p->z), what);
}

static void cmd_cheatstate(int argc, char **argv, ControlReply *rep) {
    (void)argc; (void)argv;
    static const char *why[] = {"running", "no game tick yet", "unsupported version", "world not ready"};
    control_field_int(rep, "game_ticks", (int64_t)g_ticks);
    control_field_str(rep, "state", why[g_why]);
    control_field_int(rep, "pending", cheat_pending());
    control_field_str(rep, "version", g_game_version ? g_game_version : "");
    uint64_t pi = g_site.player_info ? rd64(g_site.player_info) : 0;
    control_field_int(rep, "pause_state", g_site.pause_state ? (int64_t)rd32(g_site.pause_state) : -1);
    control_field_int(rep, "player", pi ? (int64_t)(rd64(pi + 0x60) != 0) : -1);
    control_field_bool(rep, "event", framepace_in_event());
    control_field_bool(rep, "loading", framepace_loading_ui());
    char st[160]; cheat_status(st, sizeof st); control_field_str(rep, "status", st);
}

static void cmd_flag(int argc, char **argv, ControlReply *rep) {
    if (argc < 2 || argc > 3) { control_fail(rep, "Usage: flag <game data name> [s32 value]"); return; }
    if (argc == 3) {
        char *e; long v = strtol(argv[2], &e, 10);
        if (*e) { control_fail(rep, "Not a number: %s", argv[2]); return; }
        if (!cheat_flag_s32_set(argv[1], (int)v)) control_fail(rep, "Queue full");
        else control_text(rep, "Setting %s = %ld", argv[1], v);
        return;
    }
    Request r = {R_FLAG_GET};
    snprintf(r.name, sizeof r.name, "%s", argv[1]);
    g_query_done = 0;
    if (!push(&r)) { control_fail(rep, "Queue full"); return; }
    for (int i = 0; i < 50 && !g_query_done; i++) SDL_Delay(4);  // (the game thread answers within a frame or two)
    if (g_query_done) { control_field_str(rep, "flag", g_query); control_text(rep, "%s", g_query); }
    else control_fail(rep, "No answer: is a game loaded and unpaused?");
}

void menu_cheat_panel_open(void);  // runtime/ui/menu.mm
static void cmd_cheatmenu(int argc, char **argv, ControlReply *rep) {
    (void)argc; (void)argv;
    menu_cheat_panel_open();
    control_text(rep, "Cheat panel opened (F9 or L3 + R3 toggles it)");
}

void cheats_init(void) {
    static const ControlCmd cmds[] = {
        {"give", "<item> [count]", "Add an item to the inventory (local cheat)", cmd_give, CONTROL_PLAYER},
        {"rupees", "[n | +n | -n | max]", "Show or change rupees (local cheat)", cmd_rupees, CONTROL_PLAYER},
        {"warp", "<place>", "Warp to a shrine, tower, town or stable (local cheat)", cmd_warp, CONTROL_PLAYER},
        {"flag", "<name> [value]", "Read a game data flag, or set an s32 one (local cheat)", cmd_flag, CONTROL_PLAYER},
        {"cheatmenu", "", "Open the cheat panel (also F9, or L3 + R3)", cmd_cheatmenu, CONTROL_PLAYER},
        {"cheatstate", "", "Why queued cheats are waiting (local cheat)", cmd_cheatstate, CONTROL_PLAYER},
    };
    control_register(cmds, (int)(sizeof cmds / sizeof *cmds));
}
