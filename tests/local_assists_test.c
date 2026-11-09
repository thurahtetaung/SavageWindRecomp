#include "../runtime/local_assists.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static int up_stamina, up_arrows;
static void up_set(int a, int v) { if (a == LOCAL_STAMINA) up_stamina = v; if (a == LOCAL_ARROWS) up_arrows = v; }
static int up_get(int a) { return a == LOCAL_STAMINA ? up_stamina : a == LOCAL_ARROWS ? up_arrows : -1; }
static uint32_t bits(float f) { uint32_t n; memcpy(&n, &f, 4); return n; }
int main(void) {
    assert(!local_stamina_enabled());
    assert(rt_local_stamina(bits(1000), bits(900)) == bits(900));
    for (int i = 0; i < LOCAL_ASSIST_COUNT; i++) {
        assert(!local_assist_enabled(i));
        assert(local_assist_find(local_assist_name(i)) == i);
    }
    assert(rt_local_health(12, 4) == 4);
    assert(local_health_last() == 4 && local_death_count() == 0);
    assert(rt_local_health(4, 0) == 0);
    assert(local_health_last() == 0 && local_death_count() == 1);
    assert(rt_local_health(0, 0) == 0 && local_death_count() == 1);
    local_assist_set(LOCAL_HEALTH, 1);
    assert(rt_local_health(12, 4) == 12);
    assert(rt_local_health(12, 0) == 12);
    assert(rt_local_health(4, 12) == 12);
    assert(rt_local_health(0, 12) == 12);
    local_assist_set(LOCAL_HEALTH, 0);
    assert(rt_local_health(12, 4) == 4);
    assert(local_equipment_name("Weapon_Sword_001"));
    assert(local_equipment_name("Weapon_Bow_001"));
    assert(local_equipment_name("Weapon_Shield_001"));
    assert(!local_equipment_name("Enemy_Bokoblin_Junior"));
    assert(!local_equipment_name("Obj_TreeApple_A_L_01"));
    assert(!local_equipment_name("Animal_Boar_A"));
    assert(!local_equipment_name(0));
    assert(!local_equipment_name("GameROMPlayer"));
    local_assist_set(LOCAL_DURABILITY, 1);
    assert(rt_local_durability_damage(0, 1) == 1);
    assert(rt_local_durability_damage(0, 0xffffffffu) == 0xffffffffu);
    local_assist_set(LOCAL_DURABILITY, 0);
    assert(!rt_local_food_item(8, 10));
    local_assist_set(LOCAL_FOOD, 1);
    assert(rt_local_food_item(8, 10));
    assert(rt_local_food_item(7, 10));
    assert(!rt_local_food_item(7, 8));
    assert(!rt_local_food_item(9, 9));
    assert(!rt_local_food_item(0, 0));
    local_assist_set(LOCAL_FOOD, 0);
    assert(local_assist_find("not-a-cheat") == -1);
    local_stamina_set(1);
    assert(rt_local_stamina(bits(1000), bits(900)) == bits(1000));
    assert(rt_local_stamina(bits(1000), bits(0)) == bits(1000));
    assert(rt_local_stamina(bits(900), bits(1000)) == bits(1000));
    assert(rt_local_stamina(bits(0), bits(1000)) == bits(1000));
    assert(rt_local_stamina(bits(1000), bits(-1)) == bits(-1));
    assert(rt_local_stamina(bits(NAN), bits(900)) == bits(900));
    local_stamina_set(0);
    assert(rt_local_stamina(bits(1000), bits(900)) == bits(900));
    // upstream's life patch, observed: health and deaths, never changed
    unsigned d0 = local_death_count();
    rt_local_health_seen(8, 4);
    assert(local_health_last() == 4 && local_death_count() == d0);
    rt_local_health_seen(4, 0);
    assert(local_health_last() == 0 && local_death_count() == d0 + 1);
    rt_local_health_seen(0, 0);
    assert(local_death_count() == d0 + 1);
    // toggles that drive upstream's settings once bound; a flag saved before the sync carries over
    local_assist_set(LOCAL_STAMINA, 1);
    local_assists_bind_upstream(up_set, up_get);
    assert(up_stamina == 1 && local_assist_enabled(LOCAL_STAMINA));
    local_assist_set(LOCAL_STAMINA, 0);
    assert(up_stamina == 0 && !local_assist_enabled(LOCAL_STAMINA));
    up_arrows = 1;
    assert(local_assist_enabled(LOCAL_ARROWS));
    local_assist_set(LOCAL_FOOD, 1);
    assert(local_assist_enabled(LOCAL_FOOD));
}
