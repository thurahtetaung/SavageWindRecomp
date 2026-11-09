// Local experimental assists, called only from verified 1.9.0 patch sites.
#include "local_assists.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

static atomic_int enabled[LOCAL_ASSIST_COUNT];
static const char *names[] = {"stamina", "health", "durability", "arrows", "food", "horse"};
// Assists upstream now has too (Options > Accessibility: infinite stamina, infinite arrows, god mode): this fork's
// toggles drive upstream's settings, bound at start-up (runtime/local_assists_control.c). Unbound (the unit test) they
// are plain flags.
static void (*upstream_set)(int assist, int value);
static int (*upstream_get)(int assist);  // -1: not one of upstream's
void local_assists_bind_upstream(void (*set)(int, int), int (*get)(int)) {
    upstream_set = set, upstream_get = get;
    for (int i = 0; i < LOCAL_ASSIST_COUNT; i++)  // (a fork setting saved before the sync carries over)
        if (atomic_load_explicit(&enabled[i], memory_order_relaxed) && get(i) == 0) set(i, 1);
}
int local_assist_enabled(int assist) {
    if (assist < 0 || assist >= LOCAL_ASSIST_COUNT) return 0;
    int up = upstream_get && assist != LOCAL_HEALTH ? upstream_get(assist) : -1;
    return up >= 0 ? up : atomic_load_explicit(&enabled[assist], memory_order_relaxed);
}
void local_assist_set(int assist, int value) {
    if (assist < 0 || assist >= LOCAL_ASSIST_COUNT) return;
    atomic_store_explicit(&enabled[assist], !!value, memory_order_relaxed);
    if (upstream_set) upstream_set(assist, !!value);
}
const char *local_assist_name(int assist) { return assist >= 0 && assist < LOCAL_ASSIST_COUNT ? names[assist] : "unknown"; }
int local_assist_find(const char *name) {
    for (int i = 0; i < LOCAL_ASSIST_COUNT; i++) if (!strcmp(name, names[i])) return i;
    return -1;
}
int local_stamina_enabled(void) { return local_assist_enabled(LOCAL_STAMINA); }
void local_stamina_set(int value) { local_assist_set(LOCAL_STAMINA, value); }


uint32_t rt_local_stamina(uint32_t current, uint32_t requested) {
    if (!local_stamina_enabled()) return requested;
    float before, after;
    memcpy(&before, &current, sizeof before);
    memcpy(&after, &requested, sizeof after);
    // Preserve initialization and non-finite values; never invent a maximum.
    if (isfinite(before) && isfinite(after) && before > 0 && after >= 0 && after < before) return current;
    return requested;
}

static atomic_int g_health = -1;     // the last stored player health (quarter hearts), -1 before any store
static atomic_uint g_deaths;          // stores that took Link from above zero to zero (companion apps)
int local_health_last(void) { return atomic_load_explicit(&g_health, memory_order_relaxed); }
unsigned local_death_count(void) { return atomic_load_explicit(&g_deaths, memory_order_relaxed); }

// Link's life after upstream's own life patch (tools/local_assists.py wraps it): observed only, never changed.
void rt_local_health_seen(uint32_t before, uint32_t after) {
    if ((int32_t)before > 0 && after == 0) atomic_fetch_add_explicit(&g_deaths, 1, memory_order_relaxed);
    if ((int32_t)after >= 0) atomic_store_explicit(&g_health, (int)after, memory_order_relaxed);
}

uint32_t rt_local_health(uint32_t current, uint32_t requested) {
    uint32_t result = requested;
    if (local_assist_enabled(LOCAL_HEALTH) && (int32_t)current > 0 &&
        (int32_t)requested >= 0 && requested < current) result = current;
    if ((int32_t)current > 0 && result == 0) atomic_fetch_add_explicit(&g_deaths, 1, memory_order_relaxed);
    if ((int32_t)result >= 0) atomic_store_explicit(&g_health, (int)result, memory_order_relaxed);
    return result;
}

int rt_local_food_item(uint32_t type, uint32_t use) {
    // PouchItem: Food=8, Material=7; edible raw materials have CureItem use=10.
    return local_assist_enabled(LOCAL_FOOD) && (type == 8 || (type == 7 && use == 10));
}

int local_equipment_name(const char *name) {
    if (!name) return 0;
    const char *prefix[] = {"Weapon_Sword_", "Weapon_Lsword_", "Weapon_Spear_", "Weapon_Bow_", "Weapon_Shield_"};
    for (unsigned i = 0; i < sizeof prefix / sizeof *prefix; i++)
        if (!strncmp(name, prefix[i], strlen(prefix[i]))) return 1;
    return 0;
}

uint32_t rt_local_durability_damage(uint64_t actor, uint32_t damage) {
    if (!local_assist_enabled(LOCAL_DURABILITY) || (int32_t)damage <= 0) return damage;
    // Verified Actor/BaseProc layout: the name SafeString's data pointer is at +0x10.
    const uint64_t lo = 0x7000000000ull, hi = 0x7320000000ull;
    if (actor < lo || actor > hi - 0x18) return damage;
    uint64_t name;
    memcpy(&name, (const void *)(uintptr_t)(actor + 0x10), sizeof name);
    if (name < lo || name > hi - 64) return damage;
    return local_equipment_name((const char *)(uintptr_t)name) ? 0 : damage;
}
