"""Local native assists for verified BOTW 1.9.0 instruction sites.

The setter and offset were checked against the user's NSO and disassembly.
No patches are applied to other game builds.
"""
BUILD = 'cd57b23fa4bbad65803d9788c01821ee'
OFFSET = 0xcc7430
WORD = 0xbd00a668  # str s8, [x19, #0xa4]

def install(module):
    if module.nso.build_id != BUILD:
        raise ValueError('Local assists require the verified BOTW 1.9.0 main build')
    for offset, word, function in SITES:
        address = module.base + offset
        if module.word(address) != word or address in module.patches:
            raise ValueError(f'Local assist instruction mismatch or patch conflict at {offset:#x}')
    for offset, word, function in SITES:
        module.patches[module.base + offset] = function

def emit(module, address):
    assert module.word(address) == WORD
    return ('{ extern uint32_t rt_local_stamina(uint32_t, uint32_t); '
            'uint64_t ea = c->x[19] + 0xa4ull; '
            'ST32(ea, rt_local_stamina(LD32(ea), VS32(8))); }')

def health(module, address):
    pointer, value = (0, 19) if address - module.base == 0xcc7174 else (21, 25)
    return ('{ extern uint32_t rt_local_health(uint32_t, uint32_t); '
            f'uint64_t ea = c->x[{pointer}]; ST32(ea, rt_local_health(LD32(ea), (uint32_t)c->x[{value}])); }}')

def arrows(module, address):
    return ('{ extern int local_assist_enabled(int); if (local_assist_enabled(3)) return; } '
            + module.lift(address).code)

def durability(module, address):
    return ('{ extern uint32_t rt_local_durability_damage(uint64_t, uint32_t); '
            'c->x[9] = rt_local_durability_damage(LD64(c->x[20] + 0x18ull), LD32(c->x[29] + 0x1cull)); }')

def food_item(module, address):
    # The lock is already held; jump to its existing unlock/return block.
    return ('{ extern int rt_local_food_item(uint32_t, uint32_t); '
            f'if (rt_local_food_item(LD32(c->x[23] + 0x18ull), LD32(c->x[23] + 0x1cull))) goto L_{module.base + 0xd93418:x}; }} '
            + module.lift(address).code)

def food_cooked(module, address):
    return ('{ extern int local_assist_enabled(int); if (local_assist_enabled(4)) return; } '
            + module.lift(address).code)

def food_inventory(module, address):
    # The UI has resolved the selected item. Exit through the existing mutex-unlock block before removal.
    return ('{ extern int rt_local_food_item(uint32_t, uint32_t); '
            f'if (rt_local_food_item(LD32(c->x[24] + 0x18ull), LD32(c->x[24] + 0x1cull))) goto L_{module.base + 0xd97bc0:x}; }} '
            + module.lift(address).code)

def horse(module, address):
    # Spurring: ldrsh w8, [x21, #0x3a8]; subs w8, w8, #1; b.lt <no charge>; strh w8, [x21, #0x3a8]. The horse's
    # live spur count is that s16 (copied into Horse_CurrentChargeNum each frame). Skipping the store keeps the
    # charge; the spur itself, its speed and recharging are unchanged.
    return ('{ extern int local_assist_enabled(int); '
            'if (!local_assist_enabled(5)) ST16(c->x[21] + 0x3a8ull, (uint16_t)c->x[8]); }')

# Original words checked against the locally supplied NSO, not patched Switch code caves.
SITES = (
    (OFFSET, WORD, emit),
    (0xcc7174, 0xb9000013, health),
    (0xcc4e70, 0xb90002b9, health),
    (0xd93814, 0xd101c3ff, arrows),
    (0xb95424, 0xb9401fa9, durability),
    (0xd93440, 0xf0007e88, food_item),
    (0xd96310, 0xd102c3ff, food_cooked),
    (0xd97c4c, 0xf0007e68, food_inventory),
    (0x6b0d2c, 0x790752a8, horse),
)
