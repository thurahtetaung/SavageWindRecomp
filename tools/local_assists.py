"""Local native assist for the verified BOTW 1.9.0 stamina setter.

The setter and offset were checked against the user's NSO and disassembly.
No patches are applied to other game builds.
"""
BUILD = 'cd57b23fa4bbad65803d9788c01821ee'
OFFSET = 0xcc7430
WORD = 0xbd00a668  # str s8, [x19, #0xa4]

def install(module):
    if module.nso.build_id != BUILD:
        raise ValueError('Local assists require the verified BOTW 1.9.0 main build')
    address = module.base + OFFSET
    if module.word(address) != WORD or address in module.patches:
        raise ValueError('Local stamina instruction mismatch or patch conflict')
    module.patches[address] = emit

def emit(module, address):
    assert module.word(address) == WORD
    return ('{ extern uint32_t rt_local_stamina(uint32_t, uint32_t); '
            'uint64_t ea = c->x[19] + 0xa4ull; '
            'ST32(ea, rt_local_stamina(LD32(ea), VS32(8))); }')
