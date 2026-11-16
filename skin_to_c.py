#!/usr/bin/env python3
# Turns all 14 Cabela's 3 trucks into C data for sadxtruck.c: models,
# physics numbers, textures (a SADX mod loader texture pack) and a picture
# of each truck for the D-pad picker.
#
# usage: skin_to_c.py [preview folder]
#   reads   Cora3.arc and EnhPack/Scripts/cars.lua in the Cabela's install,
#           (truck names: Scripts\ACCS.LUA in Cora3.arc), model_bones.py
#   writes  sadxtruck/trucks_model.h (layout only)
#           trucks.bin in the mod folder and truckprobe/run (the data)
#           ../sonic-adventure-dx/sadx/mods/SADX Truck/textures/SADXTRUCK/
#   With a preview folder: also saves the 14 pictures there, full size.
#
# Per truck n (files in Cora3.arc):
#   Objects\Cars\CarN\CarN_0.skin       the body (meshes, see read_meshes)
#   Objects\Cars\CarN\CarN_0.model      bones: where rigid meshes sit
#   Objects\Cars\CarN\CarN_0.bonedump   physics bones: spring tops (posArc),
#                                       tires (wheel), collision boxes
#                                       (colDummy)
#   Objects\Cars\CarN\Acc\Hood1\Hood1_0.skin  the hood, on the hood1 bone
#                                       (HOOD is the only always-fitted
#                                       accessory type in ACCS.LUA)
#   Objects\Cars\CarN\Acc\Dtire\Dtire_0.skin  the tire: "Dtire" is the only
#                                       TYRE_TYPE_NORMAL in ACCS.LUA
#                                       RegisterTyres, the set a truck comes
#                                       with
#   Objects\Cars\CarN\0.tga             paint (colorTextureTable[1])
#
# .SKIN layout, from EngineDll.dll (Ghidra addresses):
#   cMeshBlended::Load 0x1013E6F0 and cMeshRigid::Load 0x1013CA00.
#   Header: model path, then two name lists (u16 count, then names as
#   1-byte length + text): "Default", then the shader names.  The u16
#   just before the first mesh is the mesh count.
#   MeshBlended: u16 shader, u16 bones, u16 vertices, u16 triangles,
#                u16 parts, bone names, vertices x 8 (bone weights),
#                vertices x 32, triangles x 6, parts x 8, bones x 24.
#   MeshRigid:   u16 shader, u16 vertices, u16 triangles, bone name,
#                vertices x 32, triangles x 6, 24 (box).
#   A vertex: position x y z, normal x y z, texture u v (8 floats).
# A mesh's shader number is a place in that truck's shader list, and the
# order differs per truck (the Predator's 0 is its paint, the Stalker's 0
# its shocks), so textures are picked by shader name (SHADER_TEXTURE).

import math
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from model_bones import bones as model_bones   # noqa: E402

CORA3 = os.path.join(HERE, '..', 'cabelas-4x4-offroad-adventure-3', 'drive_c', 'Program Files (x86)',
                     'Activision Value', 'FUNLabs', 'Cora3')
ARC = os.path.join(CORA3, 'Cora3', 'Cora3.arc')
CARS_LUA = os.path.join(CORA3, 'EnhPack', 'Scripts', 'cars.lua')
MOD = os.path.join(HERE, '..', 'sonic-adventure-dx', 'sadx', 'mods', 'SADX Truck')
OUT_H = os.path.join(HERE, 'sadxtruck', 'trucks_model.h')
# The first game (2001), for its engine starter sounds (Data/data1.res).
CORA1_RES = os.path.join(HERE, '..', "Cabela's 4x4 Offroad Adventure", 'drive_c', 'Program Files (x86)',
                         'Activision Value', "Cabela's 4x4 Off-road Adventure", 'Data', 'data1.res')
UNITS = 10.0                      # SADX units per meter
TRUCKS = 14

# Texture list SADXTRUCK: 0 to 13 paint of truck 1 to 14, 14 tire, 15 shock,
# 16 to 29 the picker pictures.  Global numbers (GBIX) 790001 and up, far
# from SADX's own.
TEX_TIRE, TEX_SHOCK, TEX_PICTURE = 14, 15, 16
# 30 to 49: damage HUD (white, tinted when drawn): 30 panel, 31 the whole
# schematic, 32 to 49 the 18 parts (Cabela's part order).
TEX_HUD = 30
# 50: Textures\Cars\Acc\colors.tga (ColorsShaderNoLight: the snorkels).
TEX_COLORS = 50
# 51, 52: the lamp glows (CARS.LUA SetCoronas: "whitecoronas" and
# "redcoronas" = Textures\Cars\WhiteCoronas_128.tga, RedCoronas_64.tga;
# black backgrounds, so alpha = the brightest channel, drawn added).
TEX_GLOW_WHITE, TEX_GLOW_RED = 51, 52
# 53: Textures\Cars\Glass.tga (GlassShaderCarN: the windows; dark, alpha
# 76 to 229 of 255).
TEX_GLASS = 53
# 54 to 67: Objects\Cars\CarN\glassdmg.tga, truck N's cracked glass
# (GlassBShaderCarN: the windows' _1 copies).
TEX_GLASS_DMG = 54
# 68: the truck's shadow (the mod's own): black, see-through, soft edges.
TEX_SHADOW = 54 + TRUCKS
# 69 to 88: the gauges, from Textures\HUD\hud.tga (HUDLOADER.LUA "BORD
# Indicator", "Water pressure", "Oil pressure", "Fuel Level",
# "Kilometraj"): 69 tachometer, 70 speedometer, 71 needle, 72 to 74 the
# empty fuel, oil and water bars, 75 the bar fill (red), 76 to 78 the
# fuel, oil and water icons, 79 to 88 the digits 0 to 9.
TEX_GAUGE = 55 + TRUCKS
# 89: Textures\Font\FontLarge.tga, Cabela's text font (512 x 512, 12
# columns of 42 x 64 pixel cells; Fonts\FontLarge.fnt places each letter).
TEX_FONT = TEX_GAUGE + 20
TEX_COUNT = TEX_FONT + 1
FONT_GLYPHS = []
GBIX0 = 790001

# Shader name -> (texture, lamp).  'paint' = that truck's paint texture.
# From SHADERS\CARS\CARS.SCRIPT: CarNShader, NoEnvShaderCarN and
# NoMShaderCarN draw Objects\Cars\CarN\0.tga, WhiteShaderCarN the same
# texture added on top (lamps), ShocksShader Textures\Cars\Acc\shock1.tga,
# DT and DT_J Textures\Cars\Tires\Dtire\Drive.tga.
def shader_texture(name, n):
    if name in ('Car%dShader' % n, 'NoEnvShaderCar%d' % n, 'NoMShaderCar%d' % n):
        return n - 1, False
    if name == 'WhiteShaderCar%d' % n:
        return n - 1, True
    if name == 'ShocksShader':
        return TEX_SHOCK, False
    if name in ('DT', 'DT_J'):
        return TEX_TIRE, False
    if name == 'ColorsShaderNoLight':
        return TEX_COLORS, False
    if name == 'GlassShaderCar%d' % n:
        return TEX_GLASS, False
    if name == 'GlassBShaderCar%d' % n:
        return TEX_GLASS_DMG + n - 1, False
    raise SystemExit('Car%d: shader %r has no texture rule' % (n, name))


class Arc:
    """Cora3.arc: footer "ARC\\0" (version, directory block, size, packed
    size, count; blocks are 2048 bytes), directory of "ARCH" entries
    (block, blocks, size, packed size, -1, name length, name).  See
    arc_list.py."""
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        i = self.d.rfind(b'ARC\x00')
        ver, blk, usz, csz, cnt = struct.unpack_from('<5I', self.d, i + 4)
        dr = zlib.decompress(self.d[blk * 2048:blk * 2048 + csz])
        self.idx = {}
        p = 0
        for _ in range(cnt):
            b, nb, sz, cs, ff, nl = struct.unpack_from('<6I', dr, p + 4)
            self.idx[dr[p + 28:p + 28 + nl - 1].decode('latin1').upper()] = (b, sz, cs)
            p += 28 + nl

    def get(self, name):
        b, sz, cs = self.idx[name.upper()]
        raw = self.d[b * 2048:b * 2048 + cs]
        return zlib.decompress(raw) if raw[:1] == b'x' and cs != sz else self.d[b * 2048:b * 2048 + sz]


def read_meshes(d, what, strict=True):
    p = d.find(b'.model') + 6
    shaders = []
    for lst in range(2):
        cnt = struct.unpack_from('<H', d, p)[0]
        p += 2
        for _ in range(cnt):
            ln = d[p]
            if lst == 1:
                shaders.append(d[p + 1:p + 1 + ln].decode())
            p += 1 + ln
    p = min(x for x in (d.find(b'\x0bMeshBlended'), d.find(b'\x09MeshRigid')) if x >= 0)
    count = struct.unpack_from('<H', d, p - 2)[0]
    out = []
    for _ in range(count):
        n = d[p]
        typ = d[p + 1:p + 1 + n].decode()
        p += 1 + n
        if typ == 'MeshBlended':
            shader, nb, nv, nt, nx = struct.unpack_from('<5H', d, p)
            p += 10
            names = []
            for _ in range(nb):
                ln = d[p]
                names.append(d[p + 1:p + 1 + ln].decode())
                p += 1 + ln
            p += nv * 8
        elif typ == 'MeshRigid':
            shader, nv, nt = struct.unpack_from('<3H', d, p)
            p += 6
            ln = d[p]
            names = [d[p + 1:p + 1 + ln].decode()]
            p += 1 + ln
            nx, nb = 0, 1
        else:
            raise SystemExit('%s: unknown mesh type %r' % (what, typ))
        verts = [struct.unpack_from('<8f', d, p + 32 * i) for i in range(nv)]
        p += nv * 32
        tris = [struct.unpack_from('<3H', d, p + 6 * i) for i in range(nt)]
        p += nt * 6 + nx * 8 + nb * 24
        out.append({'type': typ, 'shader': shaders[shader], 'bones': names, 'verts': verts, 'tris': tris})
    if strict and len(d) - p != 6:
        raise SystemExit('%s: %d bytes left after the meshes, expected 6' % (what, len(d) - p))
    return out


def read_bonedump(text):
    """Bone 'name', then 4 rows of 3 numbers: the 3x3, then the position."""
    out = {}
    for m in re.finditer(r"Bone '([^']+)'\s+" + r'\s+'.join([r'([-0-9.]+)'] * 12), text):
        v = [float(x) for x in m.groups()[1:]]
        out[m.group(1)] = {'diag': (v[0], v[4], v[8]), 'pos': (v[9], v[10], v[11])}
    return out


def lua_blocks():
    """EnhPack cars.lua (the copy the game reads): SetDataForCar("CarN")
    blocks.  Returns {n: {function name: [arguments as text]}}."""
    t = open(CARS_LUA, encoding='latin1').read()
    t = re.sub(r'--[^\n]*', '', t)
    out = {}
    parts = re.split(r'SetDataForCar\("Car(\d+)"\)', t)
    for i in range(1, len(parts), 2):
        calls = {}
        for f, args in re.findall(r'(\w+)\s*\(([^()]*)\)', parts[i + 1]):
            calls.setdefault(f, [a.strip().strip('"') for a in args.split(',')])
        out[int(parts[i])] = calls
    return out


def truck_names(arc):
    t = arc.get('Scripts\\ACCS.LUA').decode('latin1')
    return {int(n): name for n, name in re.findall(r'RegisterCar\("Car(\d+)"\s*,\s*"([^"]+)"', t)}


def physics(n, arc, lua):
    bd = read_bonedump(arc.get('Objects\\Cars\\Car%d\\Car%d_0.bonedump' % (n, n)).decode('latin1'))
    bp = {nm: pos for nm, pos, r, s in model_bones(arc.get('Objects\\Cars\\Car%d\\Car%d_0.model' % (n, n)))}
    L = lua[n]
    f = lambda k, i=0: float(L[k][i])
    spring = [bd['posArc_%d' % i]['pos'] for i in range(4)]
    tire = [bd['wheel_%d' % i]['pos'] for i in range(4)]
    dummy = [bd['colDummy_%d' % i]['diag'] + bd['colDummy_%d' % i]['pos'] for i in range(3)]
    wf, wb = bp['weighCenter_front'], bp['weighCenter_back']
    sq = lambda d: d * d
    dist = lambda a, b: math.sqrt(sum(sq(x - y) for x, y in zip(a, b)))
    train = [float(x) for x in L['SetPhysDrivingTrain']]
    train += [0.0] * (8 - len(train))
    return {
        'spring': spring, 'tire': tire,
        'spring_to_tire': spring[0][1] - tire[0][1],
        'dummy': dummy,
        # truck byte 260: weighCenter to wheel_0 / wheel_2 (truckprobe.c)
        'axle': (dist(wf, tire[0]), dist(wb, tire[2])),
        # truck byte 256: posArc_0.y / posArc_2.y minus weighCenter.y
        'pivot': (spring[0][1] - wf[1], spring[2][1] - wb[1]),
        'mass': f('SetMass'), 'adherence': f('SetTireAdherence'), 'steering': f('SetSteering'),
        # SetPhysDrivingTrain: front, rear (1 = solid axle, 0 = independent),
        # then for independent sides: pitch scale front, rear; start pitch
        # front, rear (degrees)
        'axle_state': (int(train[0]), int(train[1])),
        'axle_params': (train[2], train[4], train[3], train[5]),
        'gears': [float(x) for x in L['SetPhysGearRatiosAutomatic']],
        'init_len': f('SetInitLength'), 'min_len': f('SetMinLength'), 'max_len': f('SetMaxLength'),
        'ksusp': f('SetKSusp'), 'kdump': f('SetKDump'), 'rolling': f('SetPhysRollingCoefs'),
        'max_rpm': f('SetMaxRpm'), 'min_rpm': f('SetMinRpm'), 'red_rpm': f('SetRedRpm'),
        # Where Cabela's puts its driver (model bone driver1) and the
        # driver's door (door_0, same side), truck space, meters.
        # x flipped like the body (see mirror()).
        # SetCarPartsPoints: damage points of the 26 parts (0-3 wheels, 4-7
        # body, 8-9 doors, 10-13 lights, 14 transmission, 15 engine,
        # 16 radiator, 17 rear end, 18-25 glass).
        'parts': [float(x) for x in L['SetCarPartsPoints']],
        'seat': (-bp['driver1'][0],) + tuple(bp['driver1'][1:]),
        'door': (-bp['door_0'][0],) + tuple(bp['door_0'][1:]),
    }


def snorkel_parts(n, arc, s):
    """Snorkel s (1 or 2): Objects\\Cars\\CarN\\Acc\\SnorkS\\SnorkS_0.skin,
    rigid on the body at the model bone snork1 (both snorkels use it).
    Empty when the truck has none (Car8 has no Snork2)."""
    c = 'Objects\\Cars\\Car%d\\' % n
    name = c + 'Acc\\Snork%d\\Snork%d_0.skin' % (s, s)
    if name.upper() not in arc.idx:
        return []
    bp = {nm: pos for nm, pos, r, sc in model_bones(arc.get(c + 'Car%d_0.model' % n))}
    out = []
    for m in read_meshes(arc.get(name), 'Car%d snorkel %d' % (n, s)):
        tex, lamp = shader_texture(m['shader'], n)
        out.append((0, tex, lamp, shift(m['verts'], bp['snork1']), m['tris'], NO_TAG))
    return out


# Piece tags: (damage part, lamp kind).  GameDll's part list (strings near
# 0x101D8254) ties parts to bones: door_0 / door_1 the doors (8, 9),
# frontBar the radiator (16), rearBar the rear end (17); lights 10 to 13:
# lightScreen0/1 at the front, "brakeL0;posL0;backL0" at the rear.  Lamp
# kinds (WhiteShader lamp pieces only): 1 headlight, 2 tail (position),
# 3 brake, 4 reverse.  Part 255 = none.
PIECE_TAGS = {
    'door_0': (8, 0), 'door_1': (9, 0), 'frontBar': (16, 0), 'rearBar': (17, 0),
    'light0': (10, 1), 'light1': (11, 1), 'posL0': (12, 2), 'posL1': (13, 2),
    'brakeL0': (12, 3), 'brakeL1': (13, 3), 'backL0': (12, 4), 'backL1': (13, 4),
}
NO_TAG = (255, 0)
# Windows, in GameDll's part order (parts 18-25), and the tag numbers for
# their whole and cracked copies (lamp kinds stop at 4).
WINDOWS = ['frontscreen', 'backscreen', 'screen0', 'screen1', 'screen2', 'screen3', 'lightscreen0', 'lightscreen1']
GLASS_WHOLE, GLASS_CRACKED = 16, 24


def piece_tag(bone, lamp):
    t = PIECE_TAGS.get(bone, NO_TAG)
    if t[1] and not lamp:              # Car4's rear lamps use ordinary shaders: always drawn
        return NO_TAG
    return t


def truck_parts(n, arc):
    """Drawn pieces: (frame, texture, lamp, verts, tris, tag).  frame 0 = the
    body, 1 to 4 = wheel 0 to 3.  Rigid meshes are stored relative to their
    bone; bone positions are in truck space, the same space as the physics.
    Rims (disk_N) and tires turn with their wheel."""
    c = 'Objects\\Cars\\Car%d\\' % n
    bp = {nm: pos for nm, pos, r, s in model_bones(arc.get(c + 'Car%d_0.model' % n))}
    out = []
    for m in read_meshes(arc.get(c + 'Car%d_0.skin' % n), 'Car%d_0.skin' % n):
        tex, lamp = shader_texture(m['shader'], n)
        frame, off, tag = 0, (0.0, 0.0, 0.0), NO_TAG
        if m['type'] == 'MeshRigid':
            bone = m['bones'][0]
            if bone.startswith('disk_'):
                frame = 1 + int(bone[5:])
            else:
                off = bp[bone]
                tag = piece_tag(bone, lamp)
        out.append((frame, tex, lamp, shift(m['verts'], off), m['tris'], tag))
    for m in read_meshes(arc.get(c + 'Acc\\Hood1\\Hood1_0.skin'), 'Car%d hood' % n):
        tex, lamp = shader_texture(m['shader'], n)
        out.append((0, tex, lamp, shift(m['verts'], bp['hood1']), m['tris'], NO_TAG))
    # Windows: Acc\Screens\NAME_0.skin, whole, and NAME_1.skin, cracked,
    # each rigid on the model bone of the same name (case differs:
    # lightScreen0).  GameDll's part list (0x101D8198 on) names them parts
    # 18-25, in WINDOWS order.  The door windows go with their doors (8, 9:
    # they fall off with them), the headlight lenses with the headlights
    # (10, 11).  The tag's second number says which window and which copy:
    # GLASS_WHOLE + window, or GLASS_CRACKED + window.
    lower = {k.lower(): v for k, v in bp.items()}
    pre = (c + 'Acc\\Screens\\').upper()
    for w, stem in enumerate(WINDOWS):
        if stem not in lower:
            continue
        part = {'screen0': 8, 'screen1': 9, 'lightscreen0': 10, 'lightscreen1': 11}.get(stem, 255)
        for v, kind in ((0, GLASS_WHOLE), (1, GLASS_CRACKED)):
            name = [x for x in arc.idx if x == (pre + stem + '_%d.SKIN' % v).upper()]
            if not name:
                continue
            for m in read_meshes(arc.get(name[0]), name[0], False):
                tex, lamp = shader_texture(m['shader'], n)
                out.append((0, tex, lamp, shift(m['verts'], lower[stem]), m['tris'], (part, kind + w)))
    for w in range(4):
        for m in read_meshes(arc.get(c + 'Acc\\Dtire\\Dtire_0.skin'), 'Car%d tire' % n):
            tex, lamp = shader_texture(m['shader'], n)
            out.append((1 + w, tex, lamp, m['verts'], m['tris'], NO_TAG))
    return out


def mirror(frame, verts, tris):
    """Cabela's space is a mirror image of SADX's (licence plate text reads
    backwards and the steering wheel sits on the right when copied
    straight across), so flip each piece once.  Body pieces: x (left and
    right).  Wheel pieces: z, because GetWheelTM turns the left wheels half
    way round; flipping x would put the tire's outer side inward.  A flip
    turns triangles inside out, so the corner order is reversed too."""
    a = 0 if frame == 0 else 2
    out = []
    for v in verts:
        v = list(v)
        v[a] = -v[a]
        v[3 + a] = -v[3 + a]
        out.append(tuple(v))
    return out, [(t[0], t[2], t[1]) for t in tris]


# The mirror moves each body piece to the other side, but the physics (and
# its hit sides, part numbers, HUD) is not mirrored: a piece tagged with
# Cabela's left part (x < 0) now sits at x > 0.  So the left and right part
# numbers swap on body pieces: body corners 4-5 and 6-7, doors 8-9, lights
# 10-11 and 12-13.  (Wheels are placed by the physics, so they need none.)
SIDE_SWAP = {4: 5, 5: 4, 6: 7, 7: 6, 8: 9, 9: 8, 10: 11, 11: 10, 12: 13, 13: 12}


def shift(verts, off):
    return [(v[0] + off[0], v[1] + off[1], v[2] + off[2]) + tuple(v[3:]) for v in verts]


def fmt(x):
    s = '%.6g' % x
    if 'e' not in s and '.' not in s:
        s += '.0'
    return s + 'f'


class Res:
    """First game data1.res: u32 4660, u32 directory offset; directory:
    u32 22136, u32 count, u32 packed size, zlib block of 268-byte entries
    (256-byte name, offset, packed size, size); each file its own zlib
    stream."""
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        off = struct.unpack_from('<I', self.d, 4)[0]
        _, cnt, cs = struct.unpack_from('<3I', self.d, off)
        t = zlib.decompress(self.d[off + 12:off + 12 + cs])
        self.idx = {}
        for i in range(cnt):
            e = t[i * 268:(i + 1) * 268]
            name = e[:256].split(b'\0')[0].decode('latin1').lower()
            self.idx[name] = struct.unpack_from('<3I', e, 256)

    def get(self, name):
        o, c, r = self.idx[name.lower()]
        return zlib.decompress(self.d[o:o + c])


def wav_seconds(data):
    """Length of a RIFF WAV: data chunk size / bytes per second."""
    p, rate, size = 12, 0, 0
    while p + 8 <= len(data):
        cid, n = data[p:p + 4], struct.unpack_from('<I', data, p + 4)[0]
        if cid == b'fmt ':
            rate = struct.unpack_from('<I', data, p + 16)[0]    # bytes per second
        elif cid == b'data':
            size = n
        p += 8 + n + (n & 1)
    return size / rate


# Engine sounds.  Third game: Sound\Cars\CarN\CarN.SND (Lua: each
# BeginSample has WaveName, Gain(SF_KEY, PARAMk, count, x, y, ...) with one
# or two curves, Pitch the same), start/stop Sound\Fx\ignition\carN_st.wav
# and carN_sp.wav.  First game: the starter's crank intro, loop, out and
# click (Data\Sound\Cars\start_stop) and a false start (pickup1).
CRANK_FILES = [('crank_in.wav', '.\\data\\sound\\cars\\start_stop\\false_start_intro1.wav'),
               ('crank_loop.wav', '.\\data\\sound\\cars\\start_stop\\false_start_loop1.wav'),
               ('crank_out.wav', '.\\data\\sound\\cars\\start_stop\\false_start_out1.wav'),
               ('click.wav', '.\\data\\sound\\cars\\start_stop\\ignition.wav')]


def curves(args):
    """'SF_KEY, PARAM1, 4, 400, 0, ...' -> [(param, [(x, y), ...]), ...]"""
    a = [x.strip() for x in args.replace('\n', ' ').split(',') if x.strip()]
    out = []
    while a:
        assert a[0] == 'SF_KEY', a
        param, n = int(a[1][5:]), int(a[2])
        pts = [(float(a[3 + 2 * i]), float(a[4 + 2 * i])) for i in range(n)]
        out.append((param, pts))
        a = a[3 + 2 * n:]
    return out


def engine_sounds(arc, out_dir):
    """Copies the sound files to out_dir; returns (wav names, seconds,
    per-truck samples, per-truck start/stop wav numbers, crank wav numbers)."""
    names, secs = [], []
    def add(name, data):
        if name not in names:
            names.append(name)
            secs.append(wav_seconds(data))
            open(os.path.join(out_dir, name), 'wb').write(data)
        return names.index(name)
    samples, startstop = [], []
    for n in range(1, TRUCKS + 1):
        t = arc.get('Sound\\Cars\\Car%d\\Car%d.snd' % (n, n)).decode('latin1')
        t = t.split('BeginPlayProcedure')[0]
        ss = []
        for blk in t.split('BeginSample();')[1:]:
            wave = re.search(r'WaveName\("([^"]+)"', blk).group(1).replace('\\\\', '\\')
            gain = curves(re.search(r'Gain\(([^;]*)\);', blk, re.S).group(1))
            pitch = curves(re.search(r'Pitch\(([^;]*)\);', blk, re.S).group(1))
            assert len(gain) <= 2 and len(pitch) == 1 and all(len(c[1]) <= 4 for c in gain + pitch), blk
            w = add(wave.split('\\')[-1].lower(), arc.get(wave))
            ss.append((w, gain, pitch[0]))
        samples.append(ss)
        st = add('car%d_st.wav' % n, arc.get('Sound\\Fx\\ignition\\car%d_st.wav' % n))
        sp = add('car%d_sp.wav' % n, arc.get('Sound\\Fx\\ignition\\car%d_sp.wav' % n))
        startstop.append((st, sp))
    res = Res(CORA1_RES)
    crank = [add(name, res.get(src)) for name, src in CRANK_FILES]
    crank.append(add('false_start.wav', res.get('.\\data\\sound\\cars\\out\\pickup1\\false_start.wav')))
    # Third game FX.SND: waterdrive, splash, MediumDamagedEngine,
    # BadDamagedEngine (+ grind every 15 to 33 s), steam.
    for f in FX_FILES:
        crank.append(add(f + '.wav', arc.get('Sound\\Fx\\%s.wav' % f)))
    return names, secs, samples, startstop, crank


FX_FILES = ('twater', 'splash', 'dengine2', 'dengine1', 'grind1', 'steam')
FX_DEFINES = ('WATER', 'SPLASH', 'DAMAGED', 'BAD', 'GRIND', 'STEAM')


def f6(x):
    """Every number goes into trucks.bin as the nearest 32-bit float of the
    exact (64-bit) result.  setup.exe does the same sums in the same order,
    so its trucks.bin matches this one byte for byte."""
    return x


# trucks.bin: everything made from the Cabela's files (made by setup.exe on a
# player's machine; here by this script).  Little-endian, 4-byte aligned:
#   header  "SXTB", then u32 version, models, draws, trucks, wavs,
#           truck record size, sound record size (32 bytes)
#   models  x 32 bytes: npoints, ntris, tex, lamp (i32), center x y z,
#           radius (f32, SADX units)
#   draws   x 8 bytes: model, frame, part, lamp (u16)
#   trucks  x 452 bytes: struct truck_data (name: 16 bytes, zero filled)
#   sounds  x 652 bytes: struct truck_sound
#   wavs    x 28 bytes: file name (24 bytes, zero filled), seconds (f32)
#   font    95 x 5 bytes (font_glyph), then 1 zero byte
#   then per model: points (npoints x 3 f32), normals (npoints x 3 f32),
#           triangles (ntris x 3 i16, then 2 zero bytes if ntris is odd),
#           uv (ntris x 3 x 2 i16)
BIN_VERSION = 1
TRUCK_REC = 452
SOUND_REC = 652


def model_center(verts):
    lo = [min(v[j] for v in verts) for j in range(3)]
    hi = [max(v[j] for v in verts) for j in range(3)]
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    sq = lambda d: d * d
    r = max(math.sqrt(sum(sq(v[j] - c[j]) for j in range(3))) for v in verts)
    return c, r


def flat_draws(draws):
    flat, firsts = [], []          # per truck: (first, count) of body, snorkel 1, snorkel 2
    for groups in draws:
        f = []
        for d in groups:
            f.append((len(flat), len(d)))
            flat.extend(d)
        firsts.append(f)
    return flat, firsts


def write_bin(path, names, phys, draws, models, snd):
    P = struct.pack
    wnames, secs, samples, startstop, crank = snd
    flat, firsts = flat_draws(draws)
    out = [P('<4s7I', b'SXTB', BIN_VERSION, len(models), len(flat), TRUCKS, len(wnames), TRUCK_REC, SOUND_REC)]
    for tex, lamp, verts, tris in models:
        c, r = model_center(verts)
        out.append(P('<4i4f', len(verts), len(tris), tex, 1 if lamp else 0,
                     *[f6(x * UNITS) for x in c + [r]]))
    for d in flat:
        out.append(P('<4H', *d))
    for i in range(TRUCKS):
        n = i + 1
        p = phys[n]
        f = firsts[i]
        nm = names[n].encode('latin1')
        assert len(nm) < 16, nm
        fl = []
        for v in p['spring'] + p['tire']:
            fl += list(v)
        fl.append(p['spring_to_tire'])
        for d in p['dummy']:
            fl += list(d)
        fl += list(p['axle']) + list(p['pivot']) + [p['mass'], p['adherence'], p['steering']]
        rec = P('<16s6i', nm, f[0][0], f[0][1], f[1][0], f[2][0], f[1][1], f[2][1])
        rec += P('<%df' % len(fl), *[f6(x) for x in fl])
        rec += P('<2i', *p['axle_state'])
        fl = list(p['axle_params']) + list(p['gears'])
        fl += [p[k] for k in ('init_len', 'min_len', 'max_len', 'ksusp', 'kdump', 'rolling', 'max_rpm', 'min_rpm', 'red_rpm')]
        fl += list(p['seat']) + list(p['door']) + list(p['parts'])
        rec += P('<%df' % len(fl), *[f6(x) for x in fl])
        assert len(rec) == TRUCK_REC, len(rec)
        out.append(rec)
    def cv(c):
        param, pts = c
        xs = [p[0] for p in pts] + [0.0] * (4 - len(pts))
        ys = [p[1] for p in pts] + [0.0] * (4 - len(pts))
        return P('<2i8f', param, len(pts), *[f6(x) for x in xs + ys])
    empty = P('<2i8f', 0, 0, *[0.0] * 8)
    for i, ss in enumerate(samples):
        assert len(ss) <= 5
        rec = P('<i', len(ss))
        for k in range(5):
            if k < len(ss):
                w, gain, pitch = ss[k]
                g = [cv(c) for c in gain] + [empty] * (2 - len(gain))
                rec += P('<2i', w, len(gain)) + g[0] + g[1] + cv(pitch)
            else:
                rec += P('<2i', 0, 0) + empty * 3
        rec += P('<2i', *startstop[i])
        assert len(rec) == SOUND_REC, len(rec)
        out.append(rec)
    for name, s in zip(wnames, secs):
        nm = name.encode('latin1')
        assert len(nm) < 24, nm
        out.append(P('<24sf', nm, f6(s)))
    out.append(b''.join(P('<5B', *g) for g in FONT_GLYPHS) + b'\0')
    for tex, lamp, verts, tris in models:
        out.append(b''.join(P('<3f', *[f6(c * UNITS) for c in v[0:3]]) for v in verts))
        out.append(b''.join(P('<3f', *[f6(c) for c in v[3:6]]) for v in verts))
        out.append(b''.join(P('<3h', *t) for t in tris) + (b'\0\0' if len(tris) % 2 else b''))
        uv = []
        for t in tris:
            for j in t:
                u, v = verts[j][6], verts[j][7]
                uv += [max(-32768, min(32767, round(u * 255))), max(-32768, min(32767, round(v * 255)))]
        out.append(P('<%dh' % len(uv), *uv))
    data = b''.join(out)
    open(path, 'wb').write(data)
    return len(data)


def write_header(names, phys, draws, models, snd):
    """Only the layout: counts, texture numbers and the struct shapes.  The
    numbers themselves are in trucks.bin (loaded by trucks_bin_load)."""
    wnames, secs, samples, startstop, crank = snd
    flat, firsts = flat_draws(draws)
    L = []
    L.append('// Made by skin_to_c.py.  Do not edit; run skin_to_c.py again instead.')
    L.append('// Only the layout of trucks.bin (the Cabela\'s data itself is in that')
    L.append('// file, made on the player\'s machine from their own Cabela\'s 3).')
    L.append('// Model units: SADX units (10 per meter), truck space (x right, y up,')
    L.append('// z forward).  Physics units: meters, as Cabela\'s.')
    L.append('')
    L.append('#define TRUCK_BIN_VERSION %d' % BIN_VERSION)
    L.append('#define TRUCKS %d' % TRUCKS)
    L.append('#define TRUCK_MODELS %d' % len(models))
    L.append('#define TRUCK_DRAWS %d' % len(flat))
    L.append('#define TRUCK_TEXTURES %d' % TEX_COUNT)
    L.append('#define TRUCK_TEX_PICTURE %d' % TEX_PICTURE)
    L.append('#define TRUCK_TEX_HUD %d          // panel, schematic, then parts 0-17' % TEX_HUD)
    L.append('#define TRUCK_TEX_GLOW_WHITE %d\n#define TRUCK_TEX_GLOW_RED %d' % (TEX_GLOW_WHITE, TEX_GLOW_RED))
    L.append('#define TRUCK_TEX_GLASS %d' % TEX_GLASS)
    L.append('#define TRUCK_TEX_GLASS_DMG %d       // + truck number - 1' % TEX_GLASS_DMG)
    L.append('#define TRUCK_TEX_SHADOW %d' % TEX_SHADOW)
    L.append('#define TRUCK_TEX_GAUGE %d        // tach, speedo, needle, 3 bars, fill, 3 icons, digits 0-9' % TEX_GAUGE)
    L.append('#define TRUCK_TEX_FONT %d' % TEX_FONT)
    L.append('#define GLASS_WHOLE %d\n#define GLASS_CRACKED %d' % (GLASS_WHOLE, GLASS_CRACKED))
    L.append('#define TRUCK_GBIX0 %du' % GBIX0)
    L.append('')
    L.append('// One piece of a truck (points, normals and uv point into trucks.bin).')
    L.append('// Ninja texture coordinates: 255 = 1.0, one pair per triangle corner.')
    L.append('static struct truck_model {')
    L.append('    float (*points)[3], (*normals)[3];')
    L.append('    short *tris, (*uv)[2];')
    L.append('    int npoints, ntris, tex, lamp;')
    L.append('    float center[3], radius;')
    L.append('} truck_models[TRUCK_MODELS];')
    L.append('')
    L.append('// FontLarge glyphs, characters 32 to 126: u1, v1, u2, v2 in the')
    L.append('// texture\'s 0-255 units (2 pixels each), then the width in the same')
    L.append('// units (0 = no glyph; the space is 8 wide).')
    L.append('static const unsigned char (*font_glyph)[5];')
    L.append('')
    L.append('// What to draw for each truck: model number, frame (0 body, 1 to 4 wheels),')
    L.append('// damage part it belongs to (255 none; doors, bumpers fall off at black),')
    L.append('// lamp kind (0 none, 1 headlight, 2 tail, 3 brake, 4 reverse).')
    L.append('static const unsigned short (*truck_draws)[4];')
    L.append('')
    L.append('struct truck_data {')
    L.append('    char name[16];')
    L.append('    int first_draw, draws;')
    L.append('    int snork_first[2], snork_draws[2];   // Snork1, Snork2 (count 0 = none)')
    L.append('    tVect3 spring[4];          // posArc_0..3 (BONEDUMP)')
    L.append('    tVect3 tire[4];            // wheel_0..3 (BONEDUMP)')
    L.append('    float spring_to_tire;      // posArc_0.y - wheel_0.y')
    L.append('    float dummy[3][6];         // colDummy_0..2: scale x y z, position x y z')
    L.append('    float axle[2];             // truck byte 260')
    L.append('    float pivot[2];            // truck byte 256')
    L.append('    float mass, adherence, steering;')
    L.append('    int axle_state[2];         // SetPhysDrivingTrain 1st, 2nd (0 = independent)')
    L.append('    float axle_params[4];      // front pitch scale, front start pitch, rear, rear')
    L.append('    float gears[6];            // SetPhysGearRatiosAutomatic, reverse first')
    L.append('    float init_len, min_len, max_len, ksusp, kdump, rolling;')
    L.append('    float max_rpm, min_rpm, red_rpm;')
    L.append('    float seat[3], door[3];    // model bones driver1, door_0 (meters)')
    L.append('    float parts[26];           // SetCarPartsPoints (15 = engine)')
    L.append('};')
    L.append('#define TRUCK_REC %d' % TRUCK_REC)
    L.append('static const struct truck_data *trucks;')
    L.append('')
    L.append('// Engine sounds (files in the mod folder\'s sounds\\).  Curves: x is the')
    L.append('// sound key (PARAM1, from the RPM) or PARAM2; straight lines between the')
    L.append('// points, held at the end values outside them.')
    L.append('#define TRUCK_WAVS %d' % len(wnames))
    L.append('static const char *truck_wavs[TRUCK_WAVS];')
    L.append('static float truck_wav_seconds[TRUCK_WAVS];')
    for k, f in enumerate(FX_DEFINES):
        L.append('#define WAV_%s %d' % (f, crank[5 + k]))
    L.append('#define WAV_CRANK_IN %d' % crank[0])
    L.append('#define WAV_CRANK_LOOP %d' % crank[1])
    L.append('#define WAV_CRANK_OUT %d' % crank[2])
    L.append('#define WAV_CLICK %d' % crank[3])
    L.append('#define WAV_FALSE_START %d' % crank[4])
    L.append('struct snd_curve { int param, n; float x[4], y[4]; };')
    L.append('struct snd_sample { int wav, ngain; struct snd_curve gain[2], pitch; };')
    L.append('struct truck_sound {')
    L.append('    int nsamples; struct snd_sample s[5]; int start_wav, stop_wav;')
    L.append('};')
    L.append('#define SOUND_REC %d' % SOUND_REC)
    L.append('static const struct truck_sound *truck_sounds;')
    open(OUT_H, 'w', newline='\n').write('\n'.join(L) + '\n')


def tga(arc, name):
    from PIL import Image
    import io
    return Image.open(io.BytesIO(arc.get(name))).convert('RGBA')


def picture(name, parts, paint, tire, shock, phys):
    """A 3/4 view of the truck from the front left, its name underneath.
    Each triangle gets its texture's color at its middle, shaded by its
    normal, and the triangles are drawn far ones first."""
    from PIL import Image, ImageDraw, ImageFont
    import math
    W, H = 512, 256
    img = Image.new('RGBA', (W, H), (20, 24, 32, 215))
    dr = ImageDraw.Draw(img)
    texs = {TEX_TIRE: tire.convert('RGB'), TEX_SHOCK: shock.convert('RGB')}
    yaw, pitch = math.radians(35), math.radians(18)
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    def view(p):                                   # truck space -> camera (x right, y up, z away)
        x = cy * p[0] + sy * p[2]
        z = -sy * p[0] + cy * p[2]
        y2 = cp * p[1] - sp * z
        z2 = sp * p[1] + cp * z
        return (-x, y2, -z2)
    light = (0.4, 0.8, -0.45)
    polys = []
    for frame, tex, lamp, verts, tris, tag in parts:
        im = texs.get(tex, paint)
        off = phys['tire'][frame - 1] if frame else (0, 0, 0)
        pts = [view((v[0] + off[0], v[1] + off[1], v[2] + off[2])) for v in verts]
        nrm = [view(v[3:6]) for v in verts]
        for t in tris:
            a, b, c = (pts[i] for i in t)
            u = sum(verts[i][6] for i in t) / 3 % 1.0
            v = sum(verts[i][7] for i in t) / 3 % 1.0
            col = im.getpixel((int(u * (im.width - 1)), int(v * (im.height - 1))))[:3]
            nx, ny, nz = (sum(nrm[i][j] for i in t) / 3 for j in range(3))
            shade = 0.45 + 0.55 * abs(nx * light[0] + ny * light[1] + nz * light[2])
            polys.append(((a[2] + b[2] + c[2]) / 3, (a, b, c), tuple(min(255, int(x * shade)) for x in col)))
    xs = [q[0] for _, tri, _ in polys for q in tri]
    ys = [q[1] for _, tri, _ in polys for q in tri]
    s = min((W - 40) / (max(xs) - min(xs)), (H - 70) / (max(ys) - min(ys)))
    ox = W / 2 - s * (max(xs) + min(xs)) / 2
    oy = (H - 50) / 2 + 6 + s * (max(ys) + min(ys)) / 2
    polys.sort(key=lambda x: -x[0])
    for _, tri, col in polys:
        dr.polygon([(ox + q[0] * s, oy - q[1] * s) for q in tri], fill=col + (255,))
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 34)
    tw = dr.textlength(name, font=font)
    dr.text(((W - tw) / 2 + 2, H - 46 + 2), name, font=font, fill=(0, 0, 0, 255))
    dr.text(((W - tw) / 2, H - 46), name, font=font, fill=(255, 255, 255, 255))
    dr.text((10, 8), '<', font=font, fill=(255, 220, 80, 255))
    dr.text((W - 30, 8), '>', font=font, fill=(255, 220, 80, 255))
    return img


# Damage HUD from Cabela's 3: TEXTURES\HUD\HUD.TGA (512 x 512) and
# SCRIPTS\HUDLOADER.LUA "Body Damage": the schematic is at texture
# coordinates (994, 10, 414, 344) per mille, front pointing right; on screen
# it is turned so the front points up.  SetCarPartMap gives each part's
# rectangle (u1, v1, u2, v2) as fractions, Cabela's part order.
HUD_PART_MAP = [
    (0.928, 0.01, 0.801, 0.069), (0.928, 0.285, 0.801, 0.344),     # wheels FL FR
    (0.627, 0.01, 0.499, 0.069), (0.627, 0.285, 0.5, 0.344),       # wheels RL RR
    (0.973, 0.033, 0.932, 0.058), (0.973, 0.296, 0.932, 0.32),     # body FL FR
    (0.496, 0.027, 0.45, 0.039), (0.496, 0.314, 0.45, 0.332),      # body RL RR
    (0.797, 0.027, 0.631, 0.039), (0.797, 0.314, 0.631, 0.332),    # doors (wings) L R
    (0.994, 0.058, 0.957, 0.116), (0.994, 0.238, 0.957, 0.295),    # lights FL FR
    (0.45, 0.027, 0.414, 0.068), (0.45, 0.286, 0.414, 0.332),      # lights RL RR
    (0.774, 0.1485, 0.539, 0.205), (0.963, 0.125, 0.774, 0.2285),  # transmission, engine
    (0.987, 0.116, 0.971, 0.237), (0.434, 0.0685, 0.422, 0.286),   # radiator, rear end
]


def shadow_image():
    """The truck's shadow: 64 x 128, black; alpha 160 in the middle, fading
    to 0 over the outer quarter on each side (smooth step)."""
    from PIL import Image
    w, h = 64, 128
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    px = im.load()
    for y in range(h):
        for x in range(w):
            ex = min(x + 0.5, w - x - 0.5) / (w * 0.25)
            ey = min(y + 0.5, h - y - 0.5) / (h * 0.25)
            e = max(0.0, min(1.0, ex, ey))
            e = e * e * (3 - 2 * e)
            px[x, y] = (0, 0, 0, int(160 * e + 0.5))
    return im


def gauge_images(arc):
    """Gauges from hud.tga (512 x 512; HUDLOADER.LUA coordinates are in
    1000ths of it): tachometer, speedometer, needle, the three bars with
    their red fill cleared, the fill, three icons, digits 0 to 9.  Each is
    stretched to a power-of-two size; the mod draws it at its own size."""
    from PIL import Image
    a = tga(arc, 'Textures\\HUD\\hud.tga').convert('RGBA')
    def c(u1, v1, u2, v2):
        return a.crop(tuple(round(x * 0.512) for x in (u1, v1, u2, v2)))
    out = [c(42, 554, 421, 933).resize((256, 256), 1),          # tach 194 x 194
           c(421, 355, 999, 933).resize((512, 512), 1),         # speedo 295 x 296
           c(289.062, 0, 398.4375, 285.156).resize((64, 256), 1)]   # needle 56 x 146
    for u1, u2 in ((78.125, 160.156), (171.875, 253.906), (259.765, 343.75)):
        bar = c(u1, 296.875, u2, 546.875)
        px = bar.load()
        for y in range(bar.size[1]):
            for x in range(19, bar.size[0]):
                r, g, b, al = px[x, y]
                if r > g + 40 and x >= 20:                  # the red fill (and its pink edge) -> the bar's dark inside
                    px[x, y] = (23, 23, 23, 156)
        out.append(bar.resize((64, 128), 1))                    # 42 x 128
    fuel = c(78.125, 296.875, 160.156, 546.875)
    out.append(fuel.crop((21, 96, 38, 124)).resize((16, 32), 1))   # the red fill
    out += [c(189, 1, 281, 94).resize((64, 64), 1), c(93, 1, 189, 94).resize((64, 64), 1),
            c(1, 1, 91, 94).resize((64, 64), 1)]
    # The strip has 0 to 9 then 0 again, 26 pixels apart; each digit's
    # white rows measured from the texture (first at 194 to 212).
    tops = (194, 220, 245, 271, 297, 324, 349, 376, 401, 427)
    for d in range(10):                                         # 18 x 26 each
        out.append(a.crop((0, tops[d] - 3, 18, tops[d] + 23)).resize((32, 32), 1))
    return out


def font_glyphs(arc):
    """Fonts\\FontLarge.fnt: "CORAFNT", the name, then 25-byte records
    from byte 37: the character, then 6 floats: u1 v1 u2 v2 (the 42 x 64
    cell, 0 to 1) and the letter's left offset and width in half-pixels
    of the cell (cell = 21).  Returns 95 (u1, v1, u2, v2, width) for
    characters 32 to 126, in 0-255 texture units."""
    import struct
    f = arc.get('FONTS\\FONTLARGE.FNT')
    g = {}
    o = 37
    while o + 25 <= len(f):
        c = f[o]
        u1, v1, u2, v2, off, w = struct.unpack_from('<6f', f, o + 1)
        vals = (u1, v1, u2, v2, off, w)
        if all(v == v and -1 <= v <= 512 for v in vals):     # skip broken records (NaN or huge)
            x = round(u1 * 256 + off)
            g[c] = (x, round(v1 * 256), min(255, x + round(w)), min(255, round(v2 * 256)), round(w))
        o += 25
    return [g.get(c, (0, 0, 0, 0, 8 if c == 32 else 0)) for c in range(32, 127)]


def glow(im):
    """A corona texture (black background) -> RGBA, alpha = brightest channel."""
    from PIL import ImageChops
    im = im.convert('RGB')
    r, g, b = im.split()
    a = ImageChops.lighter(ImageChops.lighter(r, g), b)
    out = im.copy()
    out.putalpha(a)
    return out


def hud_images(arc):
    """Panel (white square), the whole schematic, then one image per part
    (the schematic with everything outside that part's rectangle clear).
    Each schematic image: cropped, turned front-up, mirrored left to right
    (the mod's truck model is a mirror of Cabela's, so Cabela's left parts
    are on the truck's right as you see it), 128 x 256."""
    from PIL import Image, ImageOps
    atlas = tga(arc, 'Textures\\HUD\\hud.tga')
    W = atlas.size[0]
    box = (round(0.414 * W), round(0.010 * W), round(0.994 * W), round(0.344 * W))
    def turn(im):
        im = im.crop(box).rotate(90, expand=True)
        return ImageOps.mirror(im).resize((128, 256), 1)
    out = [Image.new('RGBA', (8, 8), (255, 255, 255, 255)), turn(atlas)]
    for u1, v1, u2, v2 in HUD_PART_MAP:
        mask = Image.new('L', atlas.size, 0)
        mask.paste(255, (round(u2 * W), round(v1 * W), round(u1 * W), round(v2 * W)))
        part = atlas.copy()
        part.putalpha(Image.composite(atlas.getchannel('A'), Image.new('L', atlas.size, 0), mask))
        out.append(turn(part))
    return out


def main():
    arc = Arc(ARC)
    lua = lua_blocks()
    names = truck_names(arc)
    phys = {n: physics(n, arc, lua) for n in range(1, TRUCKS + 1)}
    tire_img = tga(arc, 'Textures\\Cars\\Tires\\Dtire\\Drive.tga')
    shock_img = tga(arc, 'Textures\\Cars\\Acc\\shock1.tga')
    models, seen, draws, pictures, paints = [], {}, [], [], []
    for n in range(1, TRUCKS + 1):
        parts = truck_parts(n, arc)
        paint = tga(arc, 'Objects\\Cars\\Car%d\\0.tga' % n)
        paints.append(paint)
        groups = []
        for group in (parts, snorkel_parts(n, arc, 1), snorkel_parts(n, arc, 2)):
            d = []
            for frame, tex, lamp, verts, tris, tag in group:
                verts, tris = mirror(frame, verts, tris)
                if frame == 0:
                    tag = (SIDE_SWAP.get(tag[0], tag[0]),) + tag[1:]
                key = (tex, lamp, tuple(verts), tuple(tris))
                if key not in seen:                    # same model in two trucks: store once
                    seen[key] = len(models)
                    models.append((tex, lamp, verts, tris))
                d.append((seen[key], frame) + tag)
            groups.append(d)
        draws.append(groups)
        pictures.append(picture(names[n], [q for q in parts if q[1] < TEX_GLASS], paint.convert('RGB'), tire_img, shock_img, phys[n]))
        print('Car%-2d %-10s %2d parts, %4d triangles' % (n, names[n], len(parts), sum(len(p[4]) for p in parts)))
    snd_dir = os.path.join(MOD, 'sounds')
    os.makedirs(snd_dir, exist_ok=True)
    for f in os.listdir(snd_dir):
        os.remove(os.path.join(snd_dir, f))
    snd = engine_sounds(arc, snd_dir)
    print('%d sound files in %s' % (len(snd[0]), snd_dir))
    FONT_GLYPHS[:] = font_glyphs(arc)
    write_header(names, phys, draws, models, snd)
    for d in (MOD, os.path.join(HERE, 'truckprobe', 'run')):
        size = write_bin(os.path.join(d, 'trucks.bin'), names, phys, draws, models, snd)
    print('trucks.bin: %d bytes (%.1f MB), in the mod folder and truckprobe/run' % (size, size / 1048576))

    out = os.path.join(MOD, 'textures', 'SADXTRUCK')
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    files = ['paint%d.png' % n for n in range(1, TRUCKS + 1)] + ['tire.png', 'shock.png'] + \
            ['picture%d.png' % n for n in range(1, TRUCKS + 1)]
    files += ['hud_panel.png', 'hud_all.png'] + ['hud_part%d.png' % k for k in range(18)] + ['colors.png']
    files += ['glow_white.png', 'glow_red.png']
    files += ['glass.png']
    files += ['glassdmg%d.png' % n for n in range(1, TRUCKS + 1)]
    files += ['shadow.png']
    files += ['gauge_tach.png', 'gauge_speedo.png', 'gauge_needle.png', 'gauge_bar_fuel.png', 'gauge_bar_oil.png',
              'gauge_bar_water.png', 'gauge_fill.png', 'gauge_icon_fuel.png', 'gauge_icon_oil.png',
              'gauge_icon_water.png'] + ['gauge_digit%d.png' % d for d in range(10)]
    files += ['font_large.png']
    imgs = paints + [tire_img, shock_img] + [p.resize((256, 128), 1) for p in pictures] + hud_images(arc) + \
        [tga(arc, 'Textures\\Cars\\Acc\\colors.tga')] + \
        [glow(tga(arc, 'Textures\\Cars\\WhiteCoronas_128.tga')), glow(tga(arc, 'Textures\\Cars\\RedCoronas_64.tga'))] + \
        [tga(arc, 'Textures\\Cars\\Glass.tga')] + \
        [tga(arc, 'Objects\\Cars\\Car%d\\glassdmg.tga' % n) for n in range(1, TRUCKS + 1)] + \
        [shadow_image()] + gauge_images(arc) + [tga(arc, 'Textures\\Font\\FontLarge.tga').convert('RGBA')]
    for f, im in zip(files, imgs):
        im.save(os.path.join(out, f))
    # Mod loader texture pack index: one "global number,file name" per line.
    open(os.path.join(out, 'index.txt'), 'w', newline='\n').write(
        ''.join('%d,%s\n' % (GBIX0 + i, f) for i, f in enumerate(files)))
    if len(sys.argv) > 1:
        os.makedirs(sys.argv[1], exist_ok=True)
        for n, p in enumerate(pictures, 1):
            p.save(os.path.join(sys.argv[1], 'picture%d.png' % n))
    print('%d models stored (%d triangles), %d textures in %s' % (
        len(models), sum(len(m[3]) for m in models), len(files), out))
    print('Stalker physics:', phys[12])


if __name__ == '__main__':
    main()
