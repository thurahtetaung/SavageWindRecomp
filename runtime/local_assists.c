// Local experimental assists, called only from verified 1.9.0 patch sites.
#include "local_assists.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

static atomic_int enabled[LOCAL_ASSIST_COUNT];
static const char *names[] = {"stamina", "health", "durability", "arrows", "food"};
int local_assist_enabled(int assist) {
    return assist >= 0 && assist < LOCAL_ASSIST_COUNT ? atomic_load_explicit(&enabled[assist], memory_order_relaxed) : 0;
}
void local_assist_set(int assist, int value) {
    if (assist >= 0 && assist < LOCAL_ASSIST_COUNT) atomic_store_explicit(&enabled[assist], !!value, memory_order_relaxed);
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

uint32_t rt_local_health(uint32_t current, uint32_t requested) {
    if (local_assist_enabled(LOCAL_HEALTH) && (int32_t)current > 0 &&
        (int32_t)requested >= 0 && requested < current) return current;
    return requested;
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
