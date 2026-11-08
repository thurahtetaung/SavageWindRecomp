#include "../runtime/local_assists.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static uint32_t bits(float f) { uint32_t n; memcpy(&n, &f, 4); return n; }
int main(void) {
    assert(!local_stamina_enabled());
    assert(rt_local_stamina(bits(1000), bits(900)) == bits(900));
    local_stamina_set(1);
    assert(rt_local_stamina(bits(1000), bits(900)) == bits(1000));
    assert(rt_local_stamina(bits(1000), bits(0)) == bits(1000));
    assert(rt_local_stamina(bits(900), bits(1000)) == bits(1000));
    assert(rt_local_stamina(bits(0), bits(1000)) == bits(1000));
    assert(rt_local_stamina(bits(1000), bits(-1)) == bits(-1));
    assert(rt_local_stamina(bits(NAN), bits(900)) == bits(900));
    local_stamina_set(0);
    assert(rt_local_stamina(bits(1000), bits(900)) == bits(900));
}
