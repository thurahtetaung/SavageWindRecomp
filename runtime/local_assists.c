// Local experimental assist. Only the verified 1.9.0 stamina setter calls this.
#include "local_assists.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

static atomic_int stamina;
int local_stamina_enabled(void) { return atomic_load_explicit(&stamina, memory_order_relaxed); }
void local_stamina_set(int enabled) { atomic_store_explicit(&stamina, !!enabled, memory_order_relaxed); }


uint32_t rt_local_stamina(uint32_t current, uint32_t requested) {
    if (!local_stamina_enabled()) return requested;
    float before, after;
    memcpy(&before, &current, sizeof before);
    memcpy(&after, &requested, sizeof after);
    // Preserve initialization and non-finite values; never invent a maximum.
    if (isfinite(before) && isfinite(after) && before > 0 && after >= 0 && after < before) return current;
    return requested;
}
