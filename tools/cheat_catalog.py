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


def ground_start(start, t, radius=200):
    """A ground start point near a map marker (a marker's own height is its icon's). Warping into the air leaves the
    game stuck loading, so cutscene, sky, battle and escape points are skipped: a goddess statue first, then a shrine's
    own warp point, then any other start point, nearest first."""
    bad = ('Demo', 'Sky', 'Battle', 'Escape', 'Fall', 'Clear_', 'Glide', 'Retire', 'Restart', 'Failed')
    near = []
    for s in start.values():
        d = (s['Translate']['X'] - t['X']) ** 2 + (s['Translate']['Z'] - t['Z']) ** 2
        name = s.get('PosName', '')
        if d > radius ** 2 or any(b in name for b in bad):
            continue
        rank = 0 if name.startswith('Goddess') else 1 if name.startswith('StartPos_') else 2
        near.append((rank, d, s))
    return min(near, key=lambda r: (r[0], r[1]))[2] if near else None


def beast_ground(start, icon):
    """A Divine Beast's own warp points are on the beast or in the air (warping there leaves the game stuck loading).
    Use the ground point where its quest starts, in the town beside it (Rito Village, Gerudo Town, Goron City,
    Zora's Domain)."""
    return start.get(icon)


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
    loc = {}  # LocationMarker and Dungeon messages (also names the map areas)
    for k in ('StaticMsg/LocationMarker.msbt', 'StaticMsg/Dungeon.msbt'):
        loc.update(f.msbt(msg[k]))
    places = []
    for m in static['LocationMarker']:
        cat = next((c for c, icons in PLACE_CATEGORIES if m.get('Icon') in icons), None)
        name = clean(loc.get(m.get('MessageID', ''), ''))
        if not cat or not name:
            continue
        if cat == 'Divine Beasts':
            if name + ' (nearby town)' in {p[1] for p in places}:
                continue  # (each beast has two markers)
            dest = beast_ground(start, m['Icon'])
            if not dest:
                continue
            name += ' (nearby town)'
            t, yaw = dest['Translate'], dest.get('Rotate', {}).get('Y', 0.0)
            places.append((cat, name, t['X'], t['Y'], t['Z'], yaw, True))
            continue
        dest = start.get(m.get('WarpDestPosName', ''))
        if not dest:  # a marker's own height is the icon's, not the ground: use the nearest start position
            dest = ground_start(start, m['Translate'])
            if not dest:
                continue
        t, yaw = dest['Translate'], dest.get('Rotate', {}).get('Y', 0.0)
        places.append((cat, name, t['X'], t['Y'], t['Z'], yaw, 'WarpDestPosName' in m))
    porder = {c: i for i, (c, _) in enumerate(PLACE_CATEGORIES)}
    places.sort(key=lambda p: (porder[p[0]], p[1].lower()))
    with open(os.path.join(out, 'places.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
        for p in places:
            fp.write(f'{p[0]}\t{p[1]}\t{p[2]:.2f}\t{p[3]:.2f}\t{p[4]:.2f}\t{p[5]:.1f}\n')
    import glob
    packs = sorted(glob.glob(os.path.join(romfs, 'Pack', 'Dungeon*.pack')))
    if dlc:
        packs += sorted(glob.glob(os.path.join(dlc, 'Pack', 'Dungeon*.pack')))
    shrines = shrine_points(packs, {k: clean(v) for k, v in loc.items() if k.startswith('Dungeon') and '_' not in k})
    with open(os.path.join(out, 'shrines.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
        for r in shrines:
            fp.write('\t'.join(f'{x:.2f}' if isinstance(x, float) else str(x) for x in r) + '\n')
    marks = map_markers(static, start, boot, loc)
    with open(os.path.join(out, 'map.tsv'), 'w', encoding='utf-8', newline='\n') as fp:
        for m in marks:
            fp.write('\t'.join(str(x) if not isinstance(x, float) else f'{x:.1f}' for x in m) + '\n')
    print(f'{len(rows)} items, {len(places)} places ({sum(p[6] for p in places)} with a warp point), '
          f'{len(marks)} map markers, {len(shrines)} shrine interiors -> {out}')


def _xyz(t):
    return (t[0], t[1], t[2]) if isinstance(t, list) else (t['X'], t['Y'], t['Z'])


def shrine_points(packs, names):
    """Per shrine stage: where its end is (in front of the monk), its entrance, and its treasure chests."""
    import math
    rows = []
    for path in packs:
        sid = os.path.basename(path)[:-5]  # DungeonNNN
        try:
            p = f.sarc(open(path, 'rb').read())
        except (OSError, ValueError):
            continue
        objs = []
        for k, v in p.items():
            if k.endswith('_Static.smubin') or k.endswith('_Dynamic.smubin'):
                objs += f.byml(v).get('Objs', [])
        by = lambda pre: [o for o in objs if o.get('UnitConfigName', '').startswith(pre) and 'Translate' in o]
        monk = by('Npc_DungeonPriest')
        if not monk:
            continue
        exit_ = by('DgnObj_GoalExit')
        if exit_:
            goal = _xyz(exit_[0]['Translate'])
        else:  # 3 m in front of the monk, along its facing
            mx, my, mz = _xyz(monk[0]['Translate'])
            r = monk[0].get('Rotate', 0.0)
            ry = r[1] if isinstance(r, list) else r if isinstance(r, float) else 0.0
            goal = (mx + 3 * math.sin(ry), my, mz + 3 * math.cos(ry))
        ent = by('DgnObj_EntranceElevator') or by('DgnObj_Hrl_Warpin') or by('DgnObj_Warpin')
        entrance = _xyz(ent[0]['Translate']) if ent else goal
        chests = ';'.join('%.1f,%.1f,%.1f' % _xyz(o['Translate']) for o in by('TBox_'))
        name = names.get(sid, sid)
        rows.append((sid, name, *goal, *entrance, chests))
    return rows


def map_markers(static, start, boot, msg):
    """kind, name, x, y, z, found flag, done flag  (flags: "s:<s32 name>" true when > 0, "b:<bool name>", or "")"""
    out = []
    areas = []  # named areas (map labels, and the names of Korok spots)
    for a in f.byml(boot['Map/MainField/Location.smubin']):
        name = clean(msg.get(a.get('MessageID', ''), ''))
        if name:
            t = a['Translate']
            areas.append((name, t['X'], t['Z'], a.get('ShowLevel', 0)))
            out.append(('label', name, t['X'], float(a.get('ShowLevel', 0)), t['Z'], '', ''))
    beasts = {'RemainsWind': 'Clear_RemainsWind', 'RemainsWater': 'Clear_RemainsWater',
              'RemainsFire': 'Clear_RemainsFire', 'RemainsElectric': 'Clear_RemainsElectric'}
    for m in static['LocationMarker']:
        icon, mid = m.get('Icon'), m.get('MessageID', '')
        name = clean(msg.get(mid, ''))
        dest = start.get(m.get('WarpDestPosName', ''))
        t = (dest or m)['Translate']
        if icon == 'Dungeon' and name:
            out.append(('shrine', name, t['X'], t['Y'], t['Z'], f's:Location_{mid}', f'b:Clear_{mid}'))
        elif icon == 'Tower' and name:
            n = mid[-2:]
            out.append(('tower', name, t['X'], t['Y'], t['Z'], f's:{m.get("SaveFlag", "")}', f's:MapTower_{n}_OpenScaleLevel'))
        elif icon in beasts and name and not any(o[0] == 'beast' and o[1] == name for o in out):
            g = beast_ground(start, icon)  # (drawn and warped to at the ground point beside it)
            t = g['Translate'] if g else t
            out.append(('beast', name, t['X'], t['Y'], t['Z'], f's:{m.get("SaveFlag", "")}', f'b:{beasts[icon]}'))
    for k in static.get('KorokLocation', []):
        t = k['Translate']
        near = min(areas, key=lambda a: (a[1] - t['X']) ** 2 + (a[2] - t['Z']) ** 2)[0] if areas else ''
        kind = k.get('PlacementType', '')
        out.append(('korok', f'Korok Seed near {near}' + (f' ({kind.lower()})' if kind else ''),
                    t['X'], t['Y'], t['Z'], '', f'b:{k["Flag"]}'))
    return out


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
