#pragma once
#include <stdint.h>
int local_stamina_enabled(void);
void local_stamina_set(int enabled);
void local_assists_init(void);
uint32_t rt_local_stamina(uint32_t current, uint32_t requested);
