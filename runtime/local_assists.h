#pragma once
#include <stdint.h>
enum { LOCAL_STAMINA, LOCAL_HEALTH, LOCAL_DURABILITY, LOCAL_ARROWS, LOCAL_FOOD, LOCAL_ASSIST_COUNT };
int local_assist_enabled(int assist);
void local_assist_set(int assist, int enabled);
const char *local_assist_name(int assist);
int local_assist_find(const char *name);
int local_stamina_enabled(void);
void local_stamina_set(int enabled);
void local_assists_init(void);
uint32_t rt_local_stamina(uint32_t current, uint32_t requested);
uint32_t rt_local_health(uint32_t current, uint32_t requested);
int rt_local_food_item(uint32_t type, uint32_t use);
int local_equipment_name(const char *name);
uint32_t rt_local_durability_damage(uint64_t actor, uint32_t damage);
