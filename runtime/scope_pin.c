// Native scope pins, placed on the game thread through the map manager's own method.
// Layout checked in the player's 1.9.0 executable: five pin objects at manager +0x660,
// types 37..41 (45 before first use); bit 4 of +0x3c marks an occupied slot. Activation updates the game's
// icon list and MapApp_MapIconNo/Pos through its normal update, including persistence.
#include "scope_pin.h"
#include "control.h"
#include "control_mem.h"
#include "cheats.h"
#include "game_sites.h"
#include "rt.h"
#include "hle.h"
#include "platform.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
enum { PIN_PLACE, PIN_MOVE, PIN_CLEAR };
static struct {
    unsigned id;
    int state, slot, kind;
    bool conflict, removing;
    float pos[3], expected[3];
    uint64_t expires;
    char error[128];
} job;
static const char *states[] = {"idle", "pending", "placed", "failed", "cleared"};

static bool supported(void) {
    return g_game_version && !strcmp(g_game_version, "1.9.0") &&
        g_site.scope_pin_manager && g_site.scope_pin_place;
}

static bool coordinates(char **args, float p[3]) {
    for (int i = 0; i < 3; i++) {
        char *end; errno = 0; p[i] = strtof(args[i], &end);
        if (end == args[i] || *end || errno || !isfinite(p[i]) || fabsf(p[i]) > 10000) return false;
    }
    return true;
}

static void cmd_scopepin(int argc, char **argv, ControlReply *r) {
    pthread_mutex_lock(&lock);
    if (argc != 1) {
        float p[3] = {0}, expected[3] = {0};
        int kind = PIN_PLACE, slot = -1;
        if ((argc == 9 && !strcmp(argv[1], "move")) || (argc == 6 && !strcmp(argv[1], "clear"))) {
            kind = argc == 9 ? PIN_MOVE : PIN_CLEAR;
            if (!g_site.scope_pin_remove) { control_fail(r, "Managed pins need the updated game library"); goto out; }
            if (strlen(argv[2]) != 1 || argv[2][0] < '0' || argv[2][0] > '4') {
                control_fail(r, "Pin slot must be 0 through 4"); goto out;
            }
            slot = argv[2][0] - '0';
            if (!coordinates(argv + 3, expected) || (kind == PIN_MOVE && !coordinates(argv + 6, p))) {
                control_fail(r, "Invalid pin coordinates"); goto out;
            }
        } else if (argc != 4 || !coordinates(argv + 1, p)) {
            control_fail(r, "Usage: scopepin [x y z | move slot oldX oldY oldZ x y z | clear slot oldX oldY oldZ]"); goto out;
        }
        if (!supported()) { control_fail(r, "Native scope pins need the updated 1.9.0 game library"); goto out; }
        if (!cheats_world_ready()) { control_fail(r, "Return to the overworld before placing a scope pin"); goto out; }
        char type[32], name[32]; cheat_current_stage(type, name, sizeof type);
        if (strcmp(type, "MainField")) { control_fail(r, "Scope pins can only be placed in the overworld"); goto out; }
        if (job.state == 1) { control_fail(r, "A scope pin request is already pending"); goto out; }
        job.id++; job.state = 1; job.slot = slot; job.kind = kind; job.error[0] = 0;
        job.conflict = job.removing = false;
        job.expires = plat_now_ns() + 5000000000ull;
        memcpy(job.pos, p, sizeof p);
        memcpy(job.expected, expected, sizeof expected);
    }
    control_field_bool(r, "supported", supported());
    control_field_bool(r, "managed", supported() && g_site.scope_pin_remove);
    control_field_int(r, "id", job.id);
    control_field_str(r, "state", states[job.state]);
    control_field_int(r, "slot", job.slot);
    control_field_bool(r, "conflict", job.conflict);
    if (*job.error) control_field_str(r, "error", job.error);
out:
    pthread_mutex_unlock(&lock);
}

static void failed(const char *why) {
    job.state = 3;
    snprintf(job.error, sizeof job.error, "%s", why);
}

void scope_pin_tick(void) {
    pthread_mutex_lock(&lock);
    if (job.state != 1) goto out;
    if (plat_now_ns() > job.expires) { failed("Pin request expired; request it again"); goto out; }
    // Never let a request made in one scene run later in another one.
    char type[32], name[32]; cheat_current_stage(type, name, sizeof type);
    if (!supported() || !cheats_world_ready() || strcmp(type, "MainField")) {
        failed("The overworld is no longer ready; request the pin again"); goto out;
    }
    uint64_t mgr = 0, array = 0, obj = 0;
    int count = 0;
    if (!control_mem_peek(g_site.scope_pin_manager, &mgr, 8) || !mgr ||
        !control_mem_peek(mgr + 0x658, &count, 4) || count != 5 ||
        !control_mem_peek(mgr + 0x660, &array, 8) || !array) {
        failed("Native map pin slots are unavailable"); goto out;
    }
    int slot = job.kind == PIN_PLACE ? 0 : job.slot;
    for (; slot < count; slot++) {
        uint32_t flags, type, category;
        if (!control_mem_peek(array + 8 * slot, &obj, 8) || !obj ||
            !control_mem_peek(obj + 0x1c, &type, 4) ||
            !control_mem_peek(obj + 0x20, &category, 4) || category != 22 ||
            !control_mem_peek(obj + 0x3c, &flags, 4) ||
            (type != (unsigned)(37 + slot) && !(!(flags & 0x10) && type == 45))) {
            failed("Native pin layout did not match; nothing was changed"); goto out;
        }
        if (job.kind != PIN_PLACE) {
            float actual[3];
            if (!(flags & 0x10) && job.kind == PIN_CLEAR) { job.state = 4; goto out; }
            if (!(flags & 0x10) || !control_mem_peek(obj + 0x28, actual, sizeof actual) ||
                memcmp(actual, job.expected, sizeof actual)) {
                job.conflict = true;
                failed("The pin was changed in the game; it was left untouched"); goto out;
            }
            if (job.removing) goto out;  // native update removes the icon and its saved entry
            break;
        }
        if (!(flags & 0x10)) break;
    }
    if (slot == count) { failed("All five scope pins are occupied; remove one in the game first"); goto out; }
    Cpu *cpu = kernel_current_cpu();
    if (!cpu) { failed("Game thread unavailable"); goto out; }
    Cpu saved = *cpu;
    if (job.kind == PIN_CLEAR) {
        uint64_t vtable = 0, remove = 0;
        if (!control_mem_peek(obj, &vtable, 8) || !control_mem_peek(vtable + 0xc8, &remove, 8) ||
            remove != g_site.scope_pin_remove) { failed("Native pin removal method did not match"); goto out; }
        cpu->x[0] = obj; cpu->x[1] = 0;
        guest_call(cpu, remove);
        *cpu = saved;
        job.removing = true;
        goto out;
    }
    uint64_t scratch = (cpu->sp - 0x100) & ~15ull;
    if (!control_mem_poke(scratch, job.pos, sizeof job.pos)) {
        failed("Guest stack unavailable"); goto out;
    }
    cpu->sp = scratch;
    cpu->x[0] = mgr; cpu->x[1] = 37 + slot; cpu->x[2] = scratch;
    guest_call(cpu, g_site.scope_pin_place);
    *cpu = saved;
    float actual[3];
    if (!control_mem_peek(obj + 0x28, actual, sizeof actual) ||
        memcmp(actual, job.pos, sizeof actual)) {
        failed("The game did not accept the pin position"); goto out;
    }
    job.slot = slot; job.state = 2;
out:
    pthread_mutex_unlock(&lock);
}

RT_ON_RESET(scope_pin_reset) { memset(&job, 0, sizeof job); }
void scope_pin_init(void) {
    static const ControlCmd cmds[] = {
        {"scopepin", "[x y z | move slot oldX oldY oldZ x y z | clear slot oldX oldY oldZ]", "Place or manage a native pin with ownership checks", cmd_scopepin, CONTROL_PLAYER},
    };
    control_register(cmds, 1);
}
