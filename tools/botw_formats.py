"""Minimal readers for the BOTW Switch file formats the cheat catalog needs: Yaz0, SARC, BYML (v2-v4, LE/BE)
and MSBT (LBL1 + TXT2, UTF-16). Read-only, no third-party dependencies."""
import struct


def yaz0(data):
    """Decompress Yaz0 data (returned unchanged if not Yaz0)."""
    if data[:4] != b'Yaz0':
        return data
    size = struct.unpack_from('>I', data, 4)[0]
    out = bytearray(size)
    src, dst = 16, 0
    while dst < size:
        code = data[src]; src += 1
        for bit in range(8):
            if dst >= size:
                break
            if code & (0x80 >> bit):
                out[dst] = data[src]; src += 1; dst += 1
            else:
                b1, b2 = data[src], data[src + 1]; src += 2
                dist = ((b1 & 0xF) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0:
                    n = data[src] + 0x12; src += 1
                else:
                    n += 2
                for _ in range(n):
                    out[dst] = out[dst - dist]; dst += 1
    return bytes(out)


def sarc(data):
    """{name: bytes} of a (possibly Yaz0) SARC archive."""
    data = yaz0(data)
    if data[:4] != b'SARC':
        raise ValueError('not a SARC')
    e = '<' if data[6:8] == b'\xff\xfe' else '>'
    data_off = struct.unpack_from(e + 'I', data, 0xC)[0]
    sfat = 0x14
    count = struct.unpack_from(e + 'H', data, sfat + 6)[0]
    sfnt = sfat + 0xC + count * 0x10
    names = sfnt + 8
    out = {}
    for i in range(count):
        _, attr, start, end = struct.unpack_from(e + 'IIII', data, sfat + 0xC + i * 0x10)
        if attr & 0x01000000:
            p = names + (attr & 0xFFFF) * 4
            name = data[p:data.index(b'\0', p)].decode()
        else:
            name = f'{i:08x}'
        out[name] = data[data_off + start:data_off + end]
    return out


def byml(data):
    """Parse BYML into Python dicts/lists/scalars."""
    data = yaz0(data)
    e = '<' if data[:2] == b'YB' else '>'
    if data[:2] not in (b'YB', b'BY'):
        raise ValueError('not BYML')
    u32 = lambda o: struct.unpack_from(e + 'I', data, o)[0]
    keys_off, strs_off, root_off = u32(4), u32(8), u32(12)

    def table(off):
        if not off:
            return []
        n = u32(off) >> 8 if e == '<' else u32(off) & 0xFFFFFF
        return [data[off + u32(off + 4 + 4 * i):data.index(b'\0', off + u32(off + 4 + 4 * i))].decode('utf-8', 'replace')
                for i in range(n)]
    keys, strs = table(keys_off), table(strs_off)

    def value(t, raw):
        if t == 0xA0: return strs[raw]
        if t in (0xC0, 0xC1): return node(raw)
        if t == 0xD0: return bool(raw)
        if t == 0xD1: return struct.unpack(e + 'i', struct.pack(e + 'I', raw))[0]
        if t == 0xD2: return struct.unpack(e + 'f', struct.pack(e + 'I', raw))[0]
        if t == 0xD3: return raw
        if t in (0xD4, 0xD5, 0xD6): return struct.unpack_from(e + {0xD4: 'q', 0xD5: 'Q', 0xD6: 'd'}[t], data, raw)[0]
        if t == 0xFF: return None
        return raw

    def node(off):
        t = data[off]
        n = u32(off) >> 8 if e == '<' else u32(off) & 0xFFFFFF
        if t == 0xC0:
            types = data[off + 4:off + 4 + n]
            base = off + 4 + ((n + 3) & ~3)
            return [value(types[i], u32(base + 4 * i)) for i in range(n)]
        if t == 0xC1:
            out = {}
            for i in range(n):
                p = off + 4 + 8 * i
                if e == '<':
                    k = u32(p) & 0xFFFFFF; vt = data[p + 3]
                else:
                    k = u32(p) >> 8; vt = data[p + 3]
                out[keys[k]] = value(vt, u32(p + 4))
            return out
        raise ValueError(f'byml node {t:#x}')
    return node(root_off) if root_off else None


def msbt(data):
    """{label: text} of an MSBT file; control tags are dropped."""
    e = '<' if data[8:10] == b'\xff\xfe' else '>'
    nsec = struct.unpack_from(e + 'H', data, 0xE)[0]
    off = 0x20
    sections = {}
    for _ in range(nsec):
        magic = data[off:off + 4]
        size = struct.unpack_from(e + 'I', data, off + 4)[0]
        sections[magic] = off + 0x10
        off = (off + 0x10 + size + 15) & ~15
    labels = {}
    if b'LBL1' in sections:
        base = sections[b'LBL1']
        n = struct.unpack_from(e + 'I', data, base)[0]
        for i in range(n):
            cnt, loc = struct.unpack_from(e + 'II', data, base + 4 + 8 * i)
            p = base + loc
            for _ in range(cnt):
                ln = data[p]; name = data[p + 1:p + 1 + ln].decode(); p += 1 + ln
                idx = struct.unpack_from(e + 'I', data, p)[0]; p += 4
                labels[idx] = name
    out = {}
    if b'TXT2' in sections:
        base = sections[b'TXT2']
        n = struct.unpack_from(e + 'I', data, base)[0]
        offs = [struct.unpack_from(e + 'I', data, base + 4 + 4 * i)[0] for i in range(n)]
        enc = 'utf-16-le' if e == '<' else 'utf-16-be'
        for i in range(n):
            p = base + offs[i]
            chars = []
            while True:
                c = struct.unpack_from(e + 'H', data, p)[0]; p += 2
                if c == 0:
                    break
                if c == 0x0E:  # tag: group, type, param size, params
                    _, _, ps = struct.unpack_from(e + 'HHH', data, p); p += 6 + ps
                    continue
                if c == 0x0F:  # closing tag
                    p += 4
                    continue
                chars.append(c)
            text = struct.pack(e + f'{len(chars)}H', *chars).decode(enc, 'replace')
            out[labels.get(i, str(i))] = text
    return out
