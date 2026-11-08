"""Build the cheat menu's item and warp lists from the player's own BOTW files (never shipped: derived from game data).

usage: cheat_catalog.py <extracted folder with romfs/ [aoc1|dlc1]> <output folder>

Writes items.tsv  (category, actor name, English name, value)   value: durability x100 for weapons, else 0
       places.tsv (category, English name, x, y, z, yaw degrees)
The runtime (runtime/cheats.c) reads them from <data root>/cheats/.
"""
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import botw_formats as f  # noqa: E402

# Key items that are plain inventory counts. Others (champion abilities, heart containers, quest items) are progress
# flags the pouch alone does not grant; adding them could break quests, so they are left out.
SAFE_KEY_ITEMS = {'Obj_DungeonClearSeal', 'Obj_KorokNuts', 'Obj_WarpDLC', 'Obj_ProofGiantKiller',
                  'Obj_ProofSandwormKiller', 'Obj_ProofGolemKiller'}
TAG = {t: zlib.crc32(t.encode()) for t in ('CanGetPouch', 'CookResult', 'RoastItem', 'Important', 'Arrow')}

ITEM_CATEGORIES = [  # (category, test) in menu order
    ('Weapons', lambda a, t: a['profile'] in ('WeaponSmallSword', 'WeaponLargeSword', 'WeaponSpear')),
    ('Bows', lambda a, t: a['profile'] == 'WeaponBow'),
    ('Arrows', lambda a, t: a['profile'] == 'Bullet' and TAG['Arrow'] in t),
    ('Shields', lambda a, t: a['profile'] == 'WeaponShield'),
    ('Armor (head)', lambda a, t: a['profile'] == 'ArmorHead'),
    ('Armor (body)', lambda a, t: a['profile'] == 'ArmorUpper'),
    ('Armor (legs)', lambda a, t: a['profile'] == 'ArmorLower'),
    ('Key items', lambda a, t: a['profile'] == 'Item' and TAG['Important'] in t),
    ('Roasted & frozen food', lambda a, t: a['profile'] == 'Item' and TAG['RoastItem'] in t),
    ('Materials', lambda a, t: a['profile'] == 'Item'),
]
PLACE_CATEGORIES = [  # LocationMarker icons
    ('Shrines', ('Dungeon',)),
    ('Towers', ('Tower',)),
    ('Towns & landmarks', ('Village', 'Castle', 'Labo', 'CheckPoint', 'StartPoint')),
    ('Stables', ('Hatago',)),
    ('Divine Beasts', ('RemainsWind', 'RemainsWater', 'RemainsFire', 'RemainsElectric')),
    ('Shops & inns', ('ShopYorozu', 'ShopYadoya', 'ShopBougu', 'ShopJewel', 'ShopColor')),
]


def first(*paths):
    return next((p for p in paths if os.path.exists(p)), None)


def clean(s):
    return ' '.join(s.replace('\t', ' ').split())


def main(root, out):
    romfs = os.path.join(root, 'romfs')
    dlc = first(os.path.join(root, 'aoc1'), os.path.join(root, 'dlc1'))
    actors = f.byml(open(os.path.join(romfs, 'Actor', 'ActorInfo.product.sbyml'), 'rb').read())['Actors']
    boot = f.sarc(open(os.path.join(romfs, 'Pack', 'Bootup.pack'), 'rb').read())
    msg = f.sarc(f.sarc(open(os.path.join(romfs, 'Pack', 'Bootup_USen.pack'), 'rb').read())['Message/Msg_USen.product.ssarc'])
    names = {}
    for k, v in msg.items():
        if k.startswith('ActorType/'):
            names.update(f.msbt(v))
    os.makedirs(out, exist_ok=True)

    rows, seen = [], {}
    for a in actors:
        tags = set((a.get('tags') or {}).values())
        if TAG['CanGetPouch'] not in tags or a['profile'] == 'CookResult':
            continue
        label = clean(names.get(a['name'] + '_Name', ''))
        if not label or label.startswith('-'):
            continue
        cat = next((c for c, test in ITEM_CATEGORIES if test(a, tags)), None)
        if not cat or (cat == 'Key items' and a['name'] not in SAFE_KEY_ITEMS):
            continue
        value = a.get('generalLife', 0) * 100 if cat in ('Weapons', 'Bows', 'Shields') else 0
        rows.append([cat, a['name'], label, value, a.get('sortKey', 0)])
        seen.setdefault((cat, label), []).append(rows[-1])
    stars = {a['name']: a.get('armorStarNum', 0) for a in actors}
    for dupes in seen.values():  # same English name: armor upgrade level, else the actor name
        if len(dupes) > 1:
            levels = [stars.get(r[1], 0) for r in dupes]
            by_level = all(levels) and len(set(levels)) == len(levels)
            for r, lv in zip(dupes, levels):
                r[2] = (f'{r[2]} (+{lv - 1})' if lv > 1 else r[2]) if by_level else f'{r[2]} ({r[1]})'
    order = {c: i for i, (c, _) in enumerate(ITEM_CATEGORIES)}
    rows.sort(key=lambda r: (order[r[0]], r[2].lower()))
    with open(os.path.join(out, 'items.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
        for r in rows:
            fp.write(f'{r[0]}\t{r[1]}\t{r[2]}\t{r[3]}\n')

    static_path = dlc and os.path.join(dlc, 'Map', 'MainField', 'Static.smubin')
    static = f.byml(open(static_path, 'rb').read()) if static_path and os.path.exists(static_path) \
        else f.byml(boot['Map/MainField/Static.smubin'])
    start = {s['PosName']: s for s in static.get('StartPos', []) if 'PosName' in s}
    loc = {}
    for k in ('StaticMsg/LocationMarker.msbt', 'StaticMsg/Dungeon.msbt'):
        loc.update(f.msbt(msg[k]))
    places = []
    for m in static['LocationMarker']:
        cat = next((c for c, icons in PLACE_CATEGORIES if m.get('Icon') in icons), None)
        name = clean(loc.get(m.get('MessageID', ''), ''))
        if not cat or not name:
            continue
        dest = start.get(m.get('WarpDestPosName', ''))
        if not dest:  # a marker's own height is the icon's, not the ground: use the nearest start position
            mx, mz = m['Translate']['X'], m['Translate']['Z']
            near = min(start.values(), key=lambda s: (s['Translate']['X'] - mx) ** 2 + (s['Translate']['Z'] - mz) ** 2)
            if (near['Translate']['X'] - mx) ** 2 + (near['Translate']['Z'] - mz) ** 2 > 200 ** 2:
                continue
            dest = near
        t, yaw = dest['Translate'], dest.get('Rotate', {}).get('Y', 0.0)
        places.append((cat, name, t['X'], t['Y'], t['Z'], yaw, 'WarpDestPosName' in m))
    porder = {c: i for i, (c, _) in enumerate(PLACE_CATEGORIES)}
    places.sort(key=lambda p: (porder[p[0]], p[1].lower()))
    with open(os.path.join(out, 'places.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
        for p in places:
            fp.write(f'{p[0]}\t{p[1]}\t{p[2]:.2f}\t{p[3]:.2f}\t{p[4]:.2f}\t{p[5]:.1f}\n')
    print(f'{len(rows)} items, {len(places)} places ({sum(p[6] for p in places)} with a warp point) -> {out}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
