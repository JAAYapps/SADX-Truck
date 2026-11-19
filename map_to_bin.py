# Cabela's 3 map ground -> the mod's map file and texture pack.
# Usage: map_to_bin.py <Cora3.arc> <map, e.g. bj_a, or all> <mod folder>
# Writes <mod folder>\maps\<map>.bin and <mod folder>\textures\SXMAP_<MAP>\ (16 PNGs + index.txt),
# and the map's 64 x 64 overview picture (COLORMAP\<MAP>.TGA) into
# <mod folder>\textures\SXMAPMENU\ (the map menu's pictures; index.txt lists every map built so far).
#
# Map k (its place in MAPS, Cabela's own order from SCRIPTS\MAPS.LUA RegisterMaps)
# has textures 792000 + 16 * k to 792000 + 16 * k + 15, so no two maps share a
# texture number; its menu picture is 792256 + k.
#
# The ground (see map_to_obj.py for the .BSP layout) at every other grid point
# (513 x 513, about 6 1/2 feet apart), cut into 16 x 16 blocks of 32 x 32 cells.
# Each block lies inside one of Cabela's 16 color map pictures (4 x 4 blocks
# per picture) and uses that picture as its texture.
# SADX units: 10 per meter, so Cabela's cm x 0.1.  Cabela's space is a mirror
# image of SADX's (see skin_to_c.py mirror()), so x is flipped: SADX x = -x.
# Triangles go (a, b, c) and (b, d, c) for the cell corners
#   a = (row, col)  b = (row, col + 1)  c = (row + 1, col)  d = (row + 1, col + 1)
# which, after the flip, have their (b - a) x (c - a) normal pointing up (+y).
#
# maps\<map>.bin, little-endian:
#   0  "SXMP"           4  u32 version (1)     8  u32 blocks per side (16)
#   12 u32 points per block side (33)          16 u32 textures (16)
#   20 u32 first texture's global number (GBIX)
#   24 f32 spawn x y z (Cabela's "garage" spawn point, SADX units)
#   36 f32 cell size (SADX units)              40 char[16] texture pack name
#   56 u32 the detail texture's GBIX (0 = none), 60 u32 0
#   then per block, row by row along z: u32 texture, f32 center x y z, f32 radius,
#   f32 points[33 * 33][3], f32 normals[33 * 33][3]   (point k = row * 33 + col)
import sys, os, struct, math, re, zlib
from map_to_obj import arc_open, ground, spawns, tga, png, chunks

STEP, NB, CELLS = 2, 16, 32
NP = CELLS + 1
GBIX0 = 792000
MENU_GBIX0 = GBIX0 + 16 * 16
# Ground detail (Cabela's COLORMAP\COLORMAP.SCRIPT: each color map picture's
# shader has a second stage, Texture("Textures/Maps/Detail/detail01.tga"),
# Scale(0.5,0.5,32,32), StageOp(SHADER_DETAILOP); the file name is also the
# second name in BSP chunk 33).  Written as the pack's 17th texture, number
# 792300 + k; the mod draws it over the near ground, repeated 32 times per
# color map picture (8 per block, one repeat about 26 feet).
DETAIL_GBIX0 = GBIX0 + 300
MAPS = ['bj_a', 'bj_b', 'nf_a', 'nf_b', 'bj_d', 'bj_c', 'cn_b', 'nf_c',
        'sw_b', 'sw_c', 'cn_a', 'cn_c', 'sw_a', 'ak_a', 'ak_b', 'ak_c']

def main():
    arc, which, mod = sys.argv[1], sys.argv[2].lower(), sys.argv[3]
    get = arc_open(arc)
    for name in (MAPS if which == 'all' else [which]):
        one(get, name, mod)
        objects(get, name, mod)
        sky(get, name, mod)
    water_pack(get, mod)
    menu(get, mod)

def menu(get, mod):
    mdir = os.path.join(mod, 'textures', 'SXMAPMENU')
    os.makedirs(mdir, exist_ok=True)
    idx = ''
    for k, name in enumerate(MAPS):
        if not os.path.exists(os.path.join(mod, 'maps', name + '.bin')):
            continue
        w, hh, rows = tga(get('colormap\\%s.tga' % name))
        png(os.path.join(mdir, name + '.png'), w, rows)
        idx += '%d,%s.png\n' % (MENU_GBIX0 + k, name)
    open(os.path.join(mdir, 'index.txt'), 'w', newline='').write(idx)
    print('%s: %d menu pictures' % (mdir, idx.count('\n')))

def one(get, name, mod):
    gbix0 = GBIX0 + 16 * MAPS.index(name)
    c, n, x0, y0, z0, sx, sy, sz, h, cmap = ground(get('bsp\\%s.bsp' % name))
    assert n == NB * CELLS * STEP + 1, n
    S = 0.1                                          # cm -> SADX units

    def pos(r, col):                                 # full-grid point -> SADX
        return (-(x0 + col * sx) * S, (y0 + h[r * n + col] * sy) * S, (z0 + r * sz) * S)

    def normal(r, col):                              # from the heights around it, Cabela space
        r0, r1 = max(r - STEP, 0), min(r + STEP, n - 1)
        c0, c1 = max(col - STEP, 0), min(col + STEP, n - 1)
        dx = (h[r * n + c1] - h[r * n + c0]) * sy / ((c1 - c0) * sx)
        dz = (h[r1 * n + col] - h[r0 * n + col]) * sy / ((r1 - r0) * sz)
        l = math.sqrt(dx * dx + 1 + dz * dz)
        return (dx / l, 1 / l, -dz / l)              # (-dx, 1, -dz) with x flipped

    pack = 'SXMAP_' + name.upper()
    sp = [s for s in spawns(c) if s[0] == 'garage'] or spawns(c)
    spx, spy, spz = -sp[0][1] * S, sp[0][2] * S, sp[0][3] * S

    out = bytearray(b'SXMP' + struct.pack('<5I', 1, NB, NP, 16, gbix0) + struct.pack('<4f', spx, spy, spz, sx * STEP * S))
    q = 32 + n * n * 2
    detail = c[33][q + 256:q + 512].split(b'\0')[0].decode('latin1').replace('/', '\\').lower()
    if detail not in get.names: detail = None
    dgbix = DETAIL_GBIX0 + MAPS.index(name) if detail else 0
    out += pack.encode().ljust(16, b'\0') + struct.pack('<2I', dgbix, 0)
    for bz in range(NB):
        for bx in range(NB):
            pts, nrm = [], []
            for i in range(NP):
                for j in range(NP):
                    r, col = (bz * CELLS + i) * STEP, (bx * CELLS + j) * STEP
                    pts.append(pos(r, col))
                    nrm.append(normal(r, col))
            lo = [min(p[k] for p in pts) for k in range(3)]
            hi = [max(p[k] for p in pts) for k in range(3)]
            ctr = [(lo[k] + hi[k]) / 2 for k in range(3)]
            rad = max(math.sqrt(sum((p[k] - ctr[k]) ** 2 for k in range(3))) for p in pts)
            out += struct.pack('<I4f', (bz // 4) * 4 + bx // 4, ctr[0], ctr[1], ctr[2], rad)
            out += b''.join(struct.pack('<3f', *p) for p in pts)
            out += b''.join(struct.pack('<3f', *p) for p in nrm)
    os.makedirs(os.path.join(mod, 'maps'), exist_ok=True)
    open(os.path.join(mod, 'maps', name + '.bin'), 'wb').write(out)

    tdir = os.path.join(mod, 'textures', pack)
    os.makedirs(tdir, exist_ok=True)
    idx = ''
    for k in range(16):
        w, hh, rows = tga(get('colormap\\%s%02d.tga' % (name, k)))
        f = '%s%02d.png' % (name, k)
        png(os.path.join(tdir, f), w, rows)
        idx += '%d,%s\n' % (gbix0 + k, f)
    if detail:
        w, hh, rows = tga(get(detail))
        f = detail.split('\\')[-1].replace('.tga', '.png')
        png(os.path.join(tdir, f), w, rows)
        idx += '%d,%s\n' % (dgbix, f)
    open(os.path.join(tdir, 'index.txt'), 'w', newline='').write(idx)
    print('%s: %d bytes, %d blocks of %d x %d points; spawn "%s" at %.0f %.0f %.0f; textures %s'
          % (os.path.join(mod, 'maps', name + '.bin'), len(out), NB * NB, NP, NP, sp[0][0], spx, spy, spz, tdir))



# ---- Rocks and plants (step 5a) ---------------------------------------------
# maps\<map>_obj.bin and textures\SXOBJ_<MAP>\.  Cabela's places its map
# objects in BSP chunk 41 (EngineDll cBspTree::ReadModels, case 0x29 of
# cBspTree::Load): u32 model count, that many 256-byte model paths (.skin),
# u32 placement count, that many 80-byte placements:
#   u32 model (low byte; the higher bits are per-copy extras on plants)
#   f32 position x y z (cm), f32 angles x y z (degrees), f32 scale x y z,
#   3 u32 (0), f32 box min x y z, max x y z (cm), u32.
# A model point v (meters) lands at  position + Ry * Rx * Rz * (v * scale * 100)
# (angles about y, x, z; checked against the stored boxes of 15 turned
# placements: 0.14 cm off on average, every other order 3.5 cm or more).
# Only the rocks and plants (paths with /rocks/ or /veget/) for now.
#
# .skin with MeshRigidPrelit meshes: "FBSF", the .model path, two
# u16-counted lists of names (bones, shaders), EF E3, u32 size, u16 detail
# levels, then per level: u16 mesh count, meshes: u8 + "MeshRigidPrelit",
# u16 shader, u16 points, u16 triangles, u8 + name, points (f32 x y z,
# f32 normal x y z, u32 color, f32 u v; 36 bytes), triangles (3 u16), f32 box
# (6); after the levels optionally EF E7, u32, u16 n, n f32 switch distances
# (meters, -1 = no limit); then EF E0 and 4 zero bytes.  The level 0 meshes
# are used.  Every model placed in BSP chunk 41 is drawn (step 5a: rocks
# and plants; step 5b: buildings, bridges, ramps, fences, parked cars).
# Rocks and plants: <name>_col.skin, when there, is the collision shape
# (rocks, cactus, palms; Cabela's bushes have none and are driven through).
# Everything else is made solid by Cabela's brushes (brushes() below).
# Shader -> texture and alpha test: every .script file in the archive
# (SHADERS\NEW_ROCKS.SCRIPT, SHADERS\MAPS\BJ_A.SCRIPT, the parked cars'
# OBJECTS\LEVELCARS\CAR2\CAR2L.SCRIPT): the shader's first texture("...")
# and SHADER_ALPHATEST.  Meshes whose shader has no texture are left out.
#
# maps\<map>_obj.bin, little-endian:
#   "SXOB", u32 version 1, u32 models, u32 placements, u32 textures,
#   u32 first texture number, char[16] texture pack
#   models: u32 flags ((1 << 0) drawn as far as the ground, not only
#     near; bushes are the ones without it; (1 << 1) water: its texture
#     numbers are frames of the SXWATER pack, see water_pack()), f32 radius,
#     u32 meshes, then per mesh: u32 texture, u32 alpha (1 = see-through
#     parts), u32 points, u32 triangles, points (f32 x y z each), normals
#     (f32 x y z each),
#     triangles (3 u16 each, padded to 4 bytes), uv per corner (2 i16
#     each, 255 = 1.0);
#     then u32 collision points, u32 collision triangles, points (f32 x y z),
#     triangles (3 u16, padded to 4 bytes)
#   placements: u32 model, f32 m[12]: the rows of a Ninja matrix (x axis,
#     y axis, z axis with the scale in them, then the position; SADX units,
#     without the mod's map offset)
# Model points are in SADX units (10 per meter) and mirrored like the ground
# (x = -x, triangle corners reversed); the matrix is mirrored to match.
OBJ_GBIX0 = 793000                      # map k: 793000 + 32 * k and up
OBJ_TEX_MAX = 32

def skin_prelit(d):
    p = d.find(b'.model') + 6
    shaders = []
    for lst in range(2):
        cnt = struct.unpack_from('<H', d, p)[0]; p += 2
        for _ in range(cnt):
            ln = d[p]
            if lst == 1: shaders.append(d[p + 1:p + 1 + ln].decode())
            p += 1 + ln
    assert d[p:p + 2] == b'\xe3\xef', d[p:p + 8]
    size, nlod = struct.unpack_from('<IH', d, p + 2); p += 8
    lods = []
    for _ in range(nlod):
        cnt = struct.unpack_from('<H', d, p)[0]; p += 2
        ms = []
        for _ in range(cnt):
            n = d[p]; typ = d[p + 1:p + 1 + n].decode(); p += 1 + n
            assert typ == 'MeshRigidPrelit', typ
            sh, nv, nt = struct.unpack_from('<3H', d, p); p += 6
            ln = d[p]; bone = d[p + 1:p + 1 + ln].decode(); p += 1 + ln
            v = [struct.unpack_from('<6fI2f', d, p + 36 * i) for i in range(nv)]; p += nv * 36
            t = [struct.unpack_from('<3H', d, p + 6 * i) for i in range(nt)]; p += nt * 6
            p += 24
            ms.append((shaders[sh], v, t, bone))
        lods.append(ms)
    if d[p:p + 2] == b'\xe7\xef':
        sz, nd = struct.unpack_from('<IH', d, p + 2); p += 8 + 4 * nd
    assert d[p:] == b'\xe0\xef\0\0\0\0', d[p:]
    return lods

# <name>.model next to the .skin: "FBMF", EF E1, u32, u8 + "BoneSkeleton",
# u16 bones, u16, u16 -1, then per bone: u8 + kind ("BoneStatic",
# "BoneCora"), u8 + name, u16 3, f32 position x y z (m), f32 angles x y z
# (degrees), f32 scale x y z, u32 (the next record's number).  Each skin
# mesh names its bone; a mesh's points are in its bone's space (palms:
# the leaf crowns are made around 0 and their bones lift them 15 to 26 m
# up the trunk; oaks and pines have their leaves in place, bones at 0).
def model_bones(d):
    p = d.find(b'BoneSkeleton') + 12
    cnt = struct.unpack_from('<H', d, p)[0]; p += 6
    out = {}
    for _ in range(cnt):
        n = d[p]; p += 1 + n
        n = d[p]; name = d[p + 1:p + 1 + n].decode(); p += 1 + n
        three, = struct.unpack_from('<H', d, p); p += 2
        v = struct.unpack_from('<9f', d, p); p += 36 + 4
        out[name] = v
    return out

def bone_place(v, b):
    """Point v (x y z, meters) of a mesh on bone b -> model space."""
    if not b or (b[0:3] == (0, 0, 0) and not any(b[3:6]) and b[6:9] == (1, 1, 1)):
        return v
    M = mul3(mul3(rot(1, b[4]), rot(0, b[3])), rot(2, b[5]))
    s = [v[i] * b[6 + i] for i in range(3)]
    return tuple(sum(M[i][k] * s[k] for k in range(3)) + b[i] for i in range(3))

def bone_turn(n, b):
    if not b or not any(b[3:6]): return n
    M = mul3(mul3(rot(1, b[4]), rot(0, b[3])), rot(2, b[5]))
    return tuple(sum(M[i][k] * n[k] for k in range(3)) for i in range(3))

def shader_table(get):
    out = {}
    have = set(get.names)
    for f in sorted(get.names):
        if not f.endswith('.script'): continue
        t = get(f).decode('latin1')
        for m in re.finditer(r'function\s+(\w+)\s*\(\)(.*?)\nend', t, re.S | re.I):
            tex = re.findall(r'texture\("([^"]+)"', m.group(2), re.I)
            if not tex: continue
            tx = tex[0].replace('\\\\', '\\').replace('/', '\\').lower()
            if tx in have and m.group(1).lower() not in out:    # sw_c's water names a file that isn't there
                out[m.group(1).lower()] = (tx, 'ALPHATEST' in m.group(2).upper())
    return out

def tga_rgba(data):
    idlen, cmap, kind = data[0], data[1], data[2]
    w, h, bpp, desc = struct.unpack_from('<HHBB', data, 12)
    assert kind == 2 and cmap == 0 and bpp in (24, 32), (kind, cmap, bpp)
    px, bp = data[18 + idlen:], bpp // 8
    rows = [px[r * w * bp:(r + 1) * w * bp] for r in range(h)]
    if not desc & (1 << 5):
        rows.reverse()
    out = []
    for r in rows:
        o = bytearray(w * 4)
        o[0::4], o[1::4], o[2::4] = r[2::bp], r[1::bp], r[0::bp]
        o[3::4] = r[3::bp] if bp == 4 else b'\xff' * w
        out.append(bytes(o))
    return w, h, out

def png_rgba(path, w, rows):
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    raw = b''.join(b'\0' + r for r in rows)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', w, len(rows), 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def rot(axis, deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    if axis == 0: return [[1, 0, 0], [0, c, -s], [0, s, c]]
    if axis == 1: return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]

def mul3(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]

def pad4(b):
    return b + b'\0' * (-len(b) % 4)

def objects(get, name, mod):
    k = MAPS.index(name)
    gbix0 = OBJ_GBIX0 + OBJ_TEX_MAX * k
    pack = 'SXOBJ_' + name.upper()
    shaders = shader_table(get)
    d = chunks(get('bsp\\%s.bsp' % name))[41]
    nn = struct.unpack_from('<I', d, 0)[0]
    paths = [d[4 + i * 256:4 + i * 256 + 256].split(b'\0')[0].decode().lower() for i in range(nn)]
    p = 4 + nn * 256
    ni = struct.unpack_from('<I', d, p)[0]; p += 4
    keep = {}                                   # Cabela's model number -> our model number
    order = []                                  # our models: their paths
    for i, path in enumerate(paths):
        if path not in order: order.append(path)
        keep[i] = order.index(path)
    skipped = 0
    texs = []                                   # texture paths, in texture-number order
    models = b''
    for path in order:
        lods = skin_prelit(get(path.replace('/', '\\')))
        bones = model_bones(get(path.replace('.skin', '.model').replace('/', '\\')))
        def placed(meshes):
            out = []
            for sh, v, t, bone in meshes:
                b = bones.get(bone)
                out.append((sh, [bone_place(x[0:3], b) + bone_turn(x[3:6], b) + tuple(x[6:]) for x in v], t))
            return out
        lods = [placed(ms) for ms in lods]
        colname = path.replace('.skin', '_col.skin').replace('/', '\\')
        plant = '/rocks/' in path or '/veget/' in path
        if not plant: colname = None                 # buildings: Cabela's brushes instead
        try:
            if colname is None: raise KeyError(path)
            col = skin_prelit(get(colname))
            try: cb_ = model_bones(get(colname.replace('_col.skin', '_col.model')))
            except KeyError: cb_ = {}
            col = [[(sh, [bone_place(x[0:3], cb_.get(bone)) + tuple(x[3:]) for x in v], t) for sh, v, t, bone in ms] for ms in col]
        except KeyError: col = None
        body = b''
        rad = 0.0
        water = path.endswith('/water.skin') and path.startswith('objects/water/')
        if water:                                    # step 6: the SXWATER pack's frames instead
            meshes = [x for x in lods[0] if x[0].lower() == 'water' and x[2]]
        else:
            meshes = [x for x in lods[0] if x[0].lower() in shaders and x[2]]
        skipped += len(lods[0]) - len(meshes)
        for sh, v, t in meshes:
            if water: tex, alpha = None, True
            else: tex, alpha = shaders[sh.lower()]
            if tex and tex not in texs: texs.append(tex)
            pts = b''.join(struct.pack('<3f', -x[0] * 10, x[1] * 10, x[2] * 10) for x in v) \
                + b''.join(struct.pack('<3f', -x[3], x[4], x[5]) for x in v)
            rad = max([rad] + [math.sqrt(x[0] ** 2 + x[1] ** 2 + x[2] ** 2) * 10 for x in v])
            tris = [(a, c, b) for a, b, c in t]                 # mirrored: corners reversed
            tb = pad4(b''.join(struct.pack('<3H', *x) for x in tris))
            uv = b''.join(struct.pack('<2h', max(-32768, min(32767, round(v[i][7] * 255))),
                                      max(-32768, min(32767, round(v[i][8] * 255)))) for x in tris for i in x)
            body += struct.pack('<4I', texs.index(tex) if tex else 0, int(alpha), len(v), len(t)) + pts + tb + uv
        cpts, ctris = [], []
        if col:
            for sh, v, t in col[0]:
                base = len(cpts)
                cpts += [(-x[0] * 10, x[1] * 10, x[2] * 10) for x in v]
                ctris += [(base + a, base + c, base + b) for a, b, c in t]
        cb = struct.pack('<2I', len(cpts), len(ctris)) + b''.join(struct.pack('<3f', *x) for x in cpts) \
             + pad4(b''.join(struct.pack('<3H', *x) for x in ctris))
        flags = (1 << 0 if (col or not plant) else 0) | (1 << 1 if water else 0)
        models += struct.pack('<IfI', flags, rad, len(meshes)) + body + cb
    grass_tex = grass(get, name, mod, texs)
    assert len(texs) <= OBJ_TEX_MAX, len(texs)
    F = [[-1, 0, 0], [0, 1, 0], [0, 0, 1]]
    places = b''
    count = 0
    for j in range(ni):
        r = struct.unpack_from('<I3f3f3f', d, p + j * 80)
        m = r[0] & 255
        if m not in keep: continue
        pos, ang, sc = r[1:4], r[4:7], r[7:10]
        M = mul3(mul3(rot(1, ang[1]), rot(0, ang[0])), rot(2, ang[2]))
        M = [[M[i][jj] * sc[jj] for jj in range(3)] for i in range(3)]
        A = mul3(mul3(F, M), F)                  # SADX space (model points are already mirrored)
        rows = [[A[i][c] for i in range(3)] for c in range(3)]   # Ninja rows = columns of A
        places += struct.pack('<I12f', keep[m], *rows[0], *rows[1], *rows[2], -pos[0] * 0.1, pos[1] * 0.1, pos[2] * 0.1)
        count += 1
    out = b'SXOB' + struct.pack('<5I', 1, len(order), count, len(texs), gbix0) + pack.encode().ljust(16, b'\0') + models + places
    open(os.path.join(mod, 'maps', name + '_obj.bin'), 'wb').write(out)
    tdir = os.path.join(mod, 'textures', pack)
    os.makedirs(tdir, exist_ok=True)
    idx = ''
    for i, t in enumerate(texs):
        w, h, rows = tga_rgba(get(t))
        f = t.split('\\')[-1].replace('.tga', '.png')      # Cabela's path uses \ (os.path.basename doesn't split it on Linux)
        png_rgba(os.path.join(tdir, f), w, rows)
        idx += '%d,%s\n' % (gbix0 + i, f)
    open(os.path.join(tdir, 'index.txt'), 'w', newline='').write(idx)
    print('%s: %d bytes, %d models, %d placements, %d textures in %s%s, %d meshes without a texture left out'
          % (os.path.join(mod, 'maps', name + '_obj.bin'), len(out), len(order), count, len(texs), tdir,
             '' if grass_tex is None else ' (texture %d is the grass)' % grass_tex, skipped))
    brushes(get, name, mod, paths, d, p, ni)


# ---- Sky (step 6) ------------------------------------------------------------
# BSP chunk 17 (cBspTree::ReadSky, case 0x11 of cBspTree::Load): 6 faces of
# 11,304 bytes: char[24] shader name (bjaxpos, bjaypos, bjazpos, bjaxneg,
# bjayneg, bjazneg), 16 bytes, then 8 x 8 patches of 176 bytes: 4 points of
# 40 bytes (f32 x y z on a cube from -1 to 1, f32 normal x y z, f32 u v of
# the face's picture, f32 u v of the clouds), 2 triangles (0, 1, 3) (3, 2, 0)
# as u16, 4 bytes.  Cabela's draws it around the camera without fog or depth
# (cBspTree::FlushSky).  Shaders: SHADERS\MAPS\SKY\BJA.SCRIPT: stage 1 the
# clouds (Textures\Sky\bja\ypos.TGA, Scale(0.5,0.5,2,2), scrolled by
# Translate("Time", 0.015, 0, "Time", 0.005, 0)), stage 2 the face's
# picture over them by its alpha (the sides; the top has only clouds, the
# bottom nothing).
#
# maps\<map>_sky.bin, little-endian:
#   "SXSK", u32 version 2, u32 first texture number, char[16] pack name,
#   u32 bottom color (0xAARRGGBB: the sides' pictures' bottom row, averaged),
#   then 6 faces: u32 picture (texture number in the pack, -1 = none),
#   u32 clouds (1 = cloud layer), 256 points (patch by patch, 4 each):
#   f32 direction x y z (1 long, mirrored: x = -x), f32 picture u v,
#   f32 cloud u v (Cabela's times 2).
#   then (version 2) the map's fog: f32 start, f32 end (SADX units),
#   u32 color (0xAARRGGBB).  From the map's entity text (BSP chunk 9):
#   property("fog color", "255 199 142 92") (A R G B), "fog start" and
#   "fog end" in meters (bj_a: 0 and 450).  Cabela's fog color is the
#   sky's bottom color; the sky itself is drawn without fog.
# Pack SXSKY_<MAP>: texture 0 the clouds (made fully opaque: sw_c's has
# alpha 0 everywhere), then the side pictures; map k's numbers 792600 + 8 k
# (792600 to 792725).  Texture numbers in use, so none overlap (SADX binds a
# texture by its number and skips one equal to the last bound, 0x78CF20;
# 792400 + 8 k ran into the water frames from sw_a on, and sw_a's water
# showed a sky picture once a loop):
#   792000-792255 ground, 792256-792271 menu, 792300-792315 ground detail,
#   792500-792537 water, 792600-792725 sky, 793000 and up rocks and plants.
SKY_GBIX0 = GBIX0 + 600

def script_textures(get):
    out = {}
    for f in sorted(get.names):
        if not f.endswith('.script'): continue
        for m in re.finditer(r'function\s+(\w+)\s*\(\)(.*?)\nend', get(f).decode('latin1'), re.S | re.I):
            tx = [x.replace('\\\\', '\\').replace('/', '\\').lower() for x in re.findall(r'texture\s*\("([^"]+)"', m.group(2), re.I)]
            out.setdefault(m.group(1).lower(), tx)
    return out

def sky(get, name, mod, cache={}):
    if 'tx' not in cache: cache['tx'] = script_textures(get)
    s = chunks(get('bsp\\%s.bsp' % name))[17]
    assert len(s) == 6 * 11304, len(s)
    k = MAPS.index(name)
    gbix0 = SKY_GBIX0 + 8 * k
    pack = 'SXSKY_' + name.upper()
    texs = []
    faces = b''
    for f in range(6):
        b = f * 11304
        sh = s[b:b + 24].split(b'\0')[0].decode('latin1').lower()
        tx = [x for x in cache['tx'].get(sh, []) if x in get.names]
        clouds = len(tx) >= 1
        pic = tx[1] if len(tx) >= 2 else None
        if clouds and (not texs or texs[0] != tx[0]):
            assert not texs, (sh, tx)
            texs.append(tx[0])
        if pic and pic not in texs: texs.append(pic)
        faces += struct.pack('<iI', texs.index(pic) if pic else -1, 1 if clouds else 0)
        for pt in range(64):
            for v in range(4):
                x, y, z, nx, ny, nz, u, vv, cu, cv = struct.unpack_from('<10f', s, b + 40 + pt * 176 + v * 40)
                l = math.sqrt(x * x + y * y + z * z)
                faces += struct.pack('<7f', -x / l, y / l, z / l, u, vv, cu * 2, cv * 2)
    tdir = os.path.join(mod, 'textures', pack)
    os.makedirs(tdir, exist_ok=True)
    idx = ''
    rs = gs = bs = n = 0
    for i, t in enumerate(texs):
        w, h, rows = tga_rgba(get(t))
        if i == 0:
            new = []
            for r in rows:
                o = bytearray(r); o[3::4] = b'\xff' * (len(r) // 4); new.append(bytes(o))
            rows = new
        else:
            last = rows[-1]
            for p in range(0, len(last), 4): rs += last[p]; gs += last[p + 1]; bs += last[p + 2]; n += 1
        f = t.split('\\')[-2] + '_' + t.split('\\')[-1].replace('.tga', '.png')
        png_rgba(os.path.join(tdir, f), w, rows)
        idx += '%d,%s\n' % (gbix0 + i, f)
    open(os.path.join(tdir, 'index.txt'), 'w', newline='').write(idx)
    bottom = 0xFF000000 | ((rs // n) << 16 | (gs // n) << 8 | (bs // n) if n else 0x808080)
    ents = chunks(get('bsp\\%s.bsp' % name)).get(9, b'').decode('latin1')
    def prop(key):
        k = ents.find('property("%s", "' % key)
        s = k + len('property("') + len(key) + len('", "')
        return ents[s:ents.find('"', s)] if k >= 0 else None
    fc, fs, fe = prop('fog color'), prop('fog start'), prop('fog end')
    a, r, g, bb = [int(x) for x in fc.split()]
    fog = struct.pack('<2fI', float(fs) * 10, float(fe) * 10, a << 24 | r << 16 | g << 8 | bb)
    out = b'SXSK' + struct.pack('<2I', 2, gbix0) + pack.encode().ljust(16, b'\0') + struct.pack('<I', bottom) + faces + fog
    path = os.path.join(mod, 'maps', name + '_sky.bin')
    open(path, 'wb').write(out)
    print('%s: %d bytes, %d textures in %s, bottom color %06X, fog %s from %s to %s m'
          % (path, len(out), len(texs), tdir, bottom & 0xFFFFFF, fc, fs, fe))


# ---- Water (step 6) ----------------------------------------------------------
# objects\water\water.skin: one flat 32 x 32 m grid (17 x 17 points, uv 0 to
# 2), placed many times at each lake's level.  Its shader "water"
# (SHADERS\CARS\CARS.SCRIPT) has two versions; the mod follows the second
# (CoraWaterVersion not 1): TextureAnim(2.0, Textures\water\water_00.tga to
# water_31.tga) with their alpha (about 75% opaque), drawn without depth
# writes.  Stage 2 adds a reflection picture (StageOp(SHADER_ADD)):
# envBumpWaterTable[map][7] in the same script, e.g. Textures\water\waterenvY.tga
# for bj_a; Cabela's slides it with the camera (SHADER_WORLD_PLANAR_ENV), the
# mod keeps it fixed on each tile, mirrored at the edges.
# Pack SXWATER, the same for every map: the 32 frames, numbers 792500 to
# 792531, then the 6 reflection pictures (WATER_ENVS), 792532 to 792537,
# with alpha 190 (the frames' 75%) put in so the mod can weight the add.
# Which reflection a map uses: maps\<map>_col.bin, header word at byte 12.
WATER_GBIX0 = GBIX0 + 500
WATER_ENVS = ['waterenv', 'waterenvn', 'waterenvnw', 'waterenvr', 'waterenvw', 'waterenvy']

def water_env(get, name):
    """1 + the place in WATER_ENVS of the map's reflection picture, 0 = none."""
    t = get('shaders\\cars\\cars.script').decode('latin1')
    want = '/perturbance/%s/' % name.replace('_', '')
    for line in t.splitlines():
        if line.startswith('envBumpWaterTable[') and want in line.lower():
            f = line.split('"')
            env = f[3].replace('\\\\', '\\').split('\\')[-1].lower().replace('.tga', '')
            return WATER_ENVS.index(env) + 1
    return 0

def water_pack(get, mod):
    tdir = os.path.join(mod, 'textures', 'SXWATER')
    os.makedirs(tdir, exist_ok=True)
    idx = ''
    for i in range(32):
        w, h, rows = tga_rgba(get('textures\\water\\water_%02d.tga' % i))
        f = 'water_%02d.png' % i
        png_rgba(os.path.join(tdir, f), w, rows)
        idx += '%d,%s\n' % (WATER_GBIX0 + i, f)
    for i, e in enumerate(WATER_ENVS):
        w, h, rows = tga_rgba(get('textures\\water\\%s.tga' % e))
        new = []
        for r in rows:
            o = bytearray(r); o[3::4] = bytes([190]) * (len(r) // 4); new.append(bytes(o))
        f = '%s.png' % e
        png_rgba(os.path.join(tdir, f), w, new)
        idx += '%d,%s\n' % (WATER_GBIX0 + 32 + i, f)
    open(os.path.join(tdir, 'index.txt'), 'w', newline='').write(idx)
    print('%s: 32 water frames, %d reflection pictures' % (tdir, len(WATER_ENVS)))

# ---- Buildings' collision (step 5b) -----------------------------------------
# Cabela's makes buildings, bridges, ramps and fences solid with invisible
# convex brushes, not with their models.  BSP chunk 39 (cBspTree::ReadInvBrushes,
# case 0x27 of cBspTree::Load): u32 n, n brushes of 40 bytes (u32 flags,
# u32 sides, u32 0 (a pointer once loaded), u32 0, f32 box low x y z,
# f32 box high x y z, cm), then every brush's sides in order: u32 plane number.
# Planes: BSP chunk 0 (case 0): u32 n, n planes of 20 bytes (f32 normal x y z,
# f32 d, u32); a point p is inside a brush when n . p - d <= 0 for all its
# sides (cBspTree::RayHitInvBrush).  Each side's face = a big square on its
# plane cut by the other sides (what cBspTree::GenerateInvBrushesSides does);
# checked: on all 5332 Baja Ranch brushes the faces' box matches the stored
# box to 0.15 cm.
# Flags seen: 520 = (1 << 9) | (1 << 3) (rocks, plants, buildings, cliffs),
# 8 = (1 << 3) (bridge posts, docks, huts), 1032 = (1 << 10) | (1 << 3)
# (the 5-sided box around the whole map; the mod has its own walls, so left
# out).  What (1 << 9) means is not known; both 520 and 8 are used as solid.
# Brushes inside a rock or plant's box (and no other model's) are left out:
# those keep their _col.skin shape from step 5a.
#
# Water (step 6): each placed objects\water\water.skin tile (a flat 32 x
# 32 m square) as one square of 2 triangles at its height, grouped the same
# way, in pieces of their own with SADX's water flag (1 << 1), SADXEnums.h
# ColFlags_Water: Sonic swims and splashes, and the truck's water depth
# (sadxtruck.c, the water list) finds it.
#
# maps\<map>_col.bin, little-endian:
#   "SXCL", u32 version 2, u32 pieces, u32 water reflection (water_env()),
#   per piece: u32 flags (SADX landtable flags: (1 << 0) solid, (1 << 1)
#   water), u32 points, u32 triangles, points (f32 x y z), normals
#   (f32 x y z), triangles (3 u16, padded to 4 bytes).
# Points in SADX units, mirrored (x = -x), without the mod's map offset.
# Brushes are grouped by where their middle is, in squares of BC_CELL
# (164 feet), one landtable piece per square: SADX lists at most 128
# pieces near the player.  Each face has its own points so its normal is
# exactly the face's; triangles (a, b, c) have (b - a) x (c - a) pointing
# out of the brush, like the ground's point up.
BC_CELL = 500.0

def brush_faces(planes, sides, eps=0.01):
    out = []
    for i in sides:
        n, dd = planes[i][0:3], planes[i][3]
        a = max(range(3), key=lambda k: abs(n[k]))
        u = [0.0, 0.0, 0.0]; u[(a + 1) % 3] = 1.0
        dot = sum(u[k] * n[k] for k in range(3)); u = [u[k] - n[k] * dot for k in range(3)]
        l = math.sqrt(sum(x * x for x in u)); u = [x / l for x in u]
        v = [n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]]
        o = [n[k] * dd for k in range(3)]; B = 1e6
        poly = [[o[k] + (s * u[k] + w * v[k]) * B for k in range(3)] for s, w in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        for j in sides:
            if j == i: continue
            m, e = planes[j][0:3], planes[j][3]
            new = []
            for q in range(len(poly)):
                P, Q = poly[q], poly[(q + 1) % len(poly)]
                dp = sum(m[k] * P[k] for k in range(3)) - e
                dq = sum(m[k] * Q[k] for k in range(3)) - e
                if dp <= eps: new.append(P)
                if (dp < -eps and dq > eps) or (dp > eps and dq < -eps):
                    tt = dp / (dp - dq); new.append([P[k] + (Q[k] - P[k]) * tt for k in range(3)])
            poly = new
            if len(poly) < 3: break
        if len(poly) >= 3: out.append((n, poly))
    return out

def brushes(get, name, mod, paths, d41, p41, ni):
    c = chunks(get('bsp\\%s.bsp' % name))
    p0 = c[0]
    planes = [struct.unpack_from('<4f', p0, 4 + i * 20) for i in range(struct.unpack_from('<I', p0, 0)[0])]
    d = c[39]; n = struct.unpack_from('<I', d, 0)[0]
    boxes = []                                  # (plant?, box) of every placement
    wcells = {}                                 # water squares, by cell
    nwater = 0
    for j in range(ni):
        r = struct.unpack_from('<I3f3f3f3I6f', d41, p41 + j * 80)
        pth = paths[r[0] & 255]
        boxes.append(('/rocks/' in pth or '/veget/' in pth, r[13:19]))
        if pth == 'objects/water/water.skin':
            pos, ang, sc = r[1:4], r[4:7], r[7:10]
            M = mul3(mul3(rot(1, ang[1]), rot(0, ang[0])), rot(2, ang[2]))
            P = []
            for cx, cz in ((-16, -16), (16, -16), (16, 16), (-16, 16)):
                v = (cx * sc[0] * 100, 0.0, cz * sc[2] * 100)
                w = [pos[i] + sum(M[i][k] * v[k] for k in range(3)) for i in range(3)]
                P.append((-w[0] * 0.1, w[1] * 0.1, w[2] * 0.1))
            mx, mz = sum(q[0] for q in P) / 4, sum(q[2] for q in P) / 4
            key = (int(math.floor(mx / BC_CELL)), int(math.floor(mz / BC_CELL)))
            pts, nrm, tris = wcells.setdefault(key, ([], [], []))
            e1 = [P[1][k] - P[0][k] for k in range(3)]; e2 = [P[2][k] - P[0][k] for k in range(3)]
            if e1[2] * e2[0] - e1[0] * e2[2] < 0: P.reverse()       # (b - a) x (c - a) up
            base = len(pts)
            pts += P; nrm += [(0.0, 1.0, 0.0)] * 4
            tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
            nwater += 1
    cells = {}
    q = 4 + n * 40
    used = plants = world = 0
    for i in range(n):
        b = struct.unpack_from('<4I6f', d, 4 + i * 40)
        sides = struct.unpack_from('<%dI' % b[1], d, q); q += 4 * b[1]
        if b[0] & (1 << 10): world += 1; continue
        mid = [(b[4 + k] + b[7 + k]) / 2 for k in range(3)]
        inside = [pl for pl, bx in boxes if all(bx[k] - 50 <= mid[k] <= bx[3 + k] + 50 for k in range(3))]
        if inside and all(inside): plants += 1; continue
        used += 1
        key = (int(math.floor(-mid[0] * 0.1 / BC_CELL)), int(math.floor(mid[2] * 0.1 / BC_CELL)))
        pts, nrm, tris = cells.setdefault(key, ([], [], []))
        for nv, poly in brush_faces(planes, sides):
            P = [(-x[0] * 0.1, x[1] * 0.1, x[2] * 0.1) for x in poly]
            N = (-nv[0], nv[1], nv[2])
            s = 0.0                             # the whole face's turn, not just its first corner's
            for k in range(1, len(P) - 1):
                e1 = [P[k][i] - P[0][i] for i in range(3)]; e2 = [P[k + 1][i] - P[0][i] for i in range(3)]
                cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
                s += sum(cr[i] * N[i] for i in range(3))
            if s < 0: P.reverse()
            base = len(pts)
            pts += P; nrm += [N] * len(P)
            for k in range(1, len(P) - 1):      # slivers (near-equal corners from the cutting) left out
                e1 = [P[k][i] - P[0][i] for i in range(3)]; e2 = [P[k + 1][i] - P[0][i] for i in range(3)]
                cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
                if sum(cr[i] * N[i] for i in range(3)) > 0.01: tris.append((base, base + k, base + k + 1))
    out = b''
    for flags, group in ((1 << 0, cells), (1 << 1, wcells)):
        for key in sorted(group):
            pts, nrm, tris = group[key]
            assert len(pts) < 65536, (key, len(pts))
            out += struct.pack('<3I', flags, len(pts), len(tris)) + b''.join(struct.pack('<3f', *x) for x in pts) \
                + b''.join(struct.pack('<3f', *x) for x in nrm) + pad4(b''.join(struct.pack('<3H', *x) for x in tris))
    out = b'SXCL' + struct.pack('<3I', 2, len(cells) + len(wcells), water_env(get, name)) + out
    path = os.path.join(mod, 'maps', name + '_col.bin')
    open(path, 'wb').write(out)
    print('%s: %d bytes, %d brushes in %d pieces (%d left to rocks and plants, %d of the box around the map left out), %d water tiles in %d pieces'
          % (path, len(out), used, len(cells), plants, world, nwater, len(wcells)))

# ---- Grass (step 5c) ---------------------------------------------------------
# maps\<map>_grass.bin.  BSP chunk 34 (cTerrainGrass::Load, case 0x22 of
# cBspTree::Load): f32 version 3.15, char[256] shader, char[256] mask file,
# f32 edge (m), i32 particles per edge, f32 width, f32 height (m).
# cTerrainGrass::Init and UpdateFull: a square of edge x edge meters around
# the camera, particles x particles grass tufts (rounded down to a multiple
# of 8), each nudged by a fixed random table, drawn where the mask bit is
# set, at the ground height + height * 0.3, width x height meters.
# Mask file: u16 256, i32 width, i32 height (1024 x 1024), then 1 bit per
# pixel, rows along z, bit (x & 7) of byte (row * width / 8 + x / 8); one
# pixel per ground cell (the mask's green matches the color map's grass).
# Texture: SHADERS\GRASS_SHADERS.SCRIPT, the shader's Texture("...").
# Alaska's three maps have an empty mask and no shader: no file.
#
# maps\<map>_grass.bin, little-endian:
#   "SXGR", u32 version 1, f32 spacing between tufts (SADX units), u32 0,
#   f32 width, f32 height (SADX units), f32 reach (edge / 2, SADX units),
#   u32 texture (in the map's SXOBJ_<MAP> pack), u32 mask width, u32 mask height,
#   f32 x of mask column 0's edge, f32 x step per column (negative: mirrored),
#   f32 z of mask row 0's edge, f32 z step per row (SADX units, without the
#   mod's map offset), then the mask bits as in the mask file.
def grass(get, name, mod, texs):
    path = os.path.join(mod, 'maps', name + '_grass.bin')
    d = chunks(get('bsp\\%s.bsp' % name)).get(34)
    if os.path.exists(path): os.remove(path)
    if not d: return None
    sh = d[4:260].split(b'\0')[0].decode().lower()
    mask = d[260:516].split(b'\0')[0].decode()
    edge, parts, w, h = struct.unpack_from('<fiff', d, 516)
    t = get('shaders\\grass_shaders.script').decode('latin1')
    tex = None
    for m in re.finditer(r'function\s+(\w+)\s*\(\)(.*?)\nend', t, re.S):
        if m.group(1).lower() == sh:
            tx = re.findall(r'Texture\s*\("([^"]+)"', m.group(2))
            if tx: tex = tx[0].replace('/', '\\').lower()
    md = get(mask.replace('/', '\\'))
    ver, mw, mh = struct.unpack_from('<Hii', md, 0)
    bits = md[10:10 + mw * mh // 8]
    if tex is None or not any(bits): return None
    c = chunks(get('bsp\\%s.bsp' % name))
    n, levels = struct.unpack_from('<2I', c[33], 0)
    x0, y0, z0, sx, sy, sz = struct.unpack_from('<6f', c[33], 8)
    parts = parts & ~7
    texs.append(tex)
    S = 0.1
    out = b'SXGR' + struct.pack('<IfI3f3I', 1, edge / parts * 100 * S, 0, w * 100 * S, h * 100 * S, edge / 2 * 100 * S,
                                len(texs) - 1, mw, mh)
    out += struct.pack('<4f', -x0 * S, -(n - 1) * sx / mw * S, z0 * S, (n - 1) * sz / mh * S) + bits
    open(path, 'wb').write(out)
    return len(texs) - 1


if __name__ == '__main__':
    main()
