# Cabela's 4x4 Off-Road Adventure 3 map ground -> OBJ + PNG, for looking at in Blender.
# Usage: map_to_obj.py <Cora3.arc> <map, e.g. bj_a> <output folder> [step]
#   step 1 = every grid point (1025 x 1025, about 2 million triangles); 2 = every other one.
#
# The .BSP file is a list of chunks: u32 id, u32 size, then size bytes
# (EngineDll cBspTree::Load reads them in a loop and seeks past each one).
# Chunk 33 is the ground (cBspTree::Load case 0x21 -> cTerrain::Load):
#   u32 n (grid points per side, 1025), u32 levels (10),
#   f32 x0 y0 z0 (corner), f32 sx sy sz (cell size x, height step, cell size z),
#   n * n u16 heights, row by row along z, x inside a row
#     (checked: Cabela's spawn points sit 1 to 7 feet above the ground this way,
#      the other way round some are 160 feet above it),
#   char[256] color map name, char[256] detail texture name, u32, then u32 count + count u32.
# Ground height = y0 + h * sy.  Units are centimeters (one cell is 99.9 = about 1 m,
# a spawn point is 1 to 2 m above the ground); the OBJ is written in meters.
# The color map is 16 TGA pictures, COLORMAP\<MAP>00.TGA to <MAP>15.TGA, 512 x 512 each.
import sys, os, struct, zlib, re

def arc_open(path):
    d = open(path, 'rb').read()
    i = d.rfind(b'ARC\x00')
    ver, blk, usz, csz, cnt = struct.unpack_from('<5I', d, i + 4)
    dr = zlib.decompress(d[blk * 2048:blk * 2048 + csz])
    files, p = {}, 0
    for _ in range(cnt):
        b, nb, sz, cs, ff, nl = struct.unpack_from('<6I', dr, p + 4)
        name = dr[p + 28:p + 28 + nl - 1].decode('latin1').lower()
        files[name] = (b, sz, cs)
        p += 28 + nl
    def get(name):
        b, sz, cs = files[name.lower()]
        raw = d[b * 2048:b * 2048 + cs]
        data = zlib.decompress(raw) if raw[:1] == b'x' and cs != sz else d[b * 2048:b * 2048 + sz]
        assert len(data) == sz
        return data
    get.names = list(files)                 # every file name in the archive, lower case
    return get

def chunks(bsp):
    p, out = 0, {}
    while p + 8 <= len(bsp):
        cid, size = struct.unpack_from('<2I', bsp, p)
        out.setdefault(cid, bsp[p + 8:p + 8 + size])
        p += 8 + size
    assert p == len(bsp), 'chunks do not end at the end of the file'
    return out

def tga(data):
    idlen, cmap, kind = data[0], data[1], data[2]
    w, h, bpp, desc = struct.unpack_from('<HHBB', data, 12)
    assert kind == 2 and cmap == 0 and bpp in (24, 32), (kind, cmap, bpp)
    px, bp = data[18 + idlen:], bpp // 8
    rows = [px[r * w * bp:(r + 1) * w * bp] for r in range(h)]
    if not desc & (1 << 5):                 # bottom-up picture
        rows.reverse()
    rgb = []
    for r in rows:
        out = bytearray(w * 3)
        out[0::3], out[1::3], out[2::3] = r[2::bp], r[1::bp], r[0::bp]
        rgb.append(bytes(out))
    return w, h, rgb

def png(path, w, rows):
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    raw = b''.join(b'\0' + r for r in rows)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', w, len(rows), 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def ground(bsp):
    """Chunk 33 of a map's .BSP: (n, x0, y0, z0, sx, sy, sz, heights, color map name)."""
    c = chunks(bsp)
    t = c[33]
    n, levels = struct.unpack_from('<2I', t, 0)
    x0, y0, z0, sx, sy, sz = struct.unpack_from('<6f', t, 8)
    h = struct.unpack_from('<%dH' % (n * n), t, 32)
    q = 32 + n * n * 2
    return c, n, x0, y0, z0, sx, sy, sz, h, t[q:q + 256].split(b'\0')[0].decode('latin1')

def spawns(c):
    """Cabela's spawn points from the entity text (chunk 9): [(name, x, y, z)] in cm."""
    ents = c.get(9, b'').decode('latin1')
    out = []
    for m in re.finditer(r'entity\("info_spawn_point"\)(.*?)(?=entity\(|\Z)', ents, re.S):
        pos = re.search(r'"position", "([-0-9.e ]+)"', m.group(1))
        tn = re.search(r'"target_name", "([^"]*)"', m.group(1))
        x, y, z = map(float, pos.group(1).split())
        out.append((tn.group(1) if tn else '', x, y, z))
    return out

def main():
    arc, name, out = sys.argv[1], sys.argv[2].lower(), sys.argv[3]
    step = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    get = arc_open(arc)
    os.makedirs(out, exist_ok=True)

    c = chunks(get('bsp\\%s.bsp' % name))
    t = c[33]
    n, levels = struct.unpack_from('<2I', t, 0)
    x0, y0, z0, sx, sy, sz = struct.unpack_from('<6f', t, 8)
    h = struct.unpack_from('<%dH' % (n * n), t, 32)
    q = 32 + n * n * 2
    cmap_name = t[q:q + 256].split(b'\0')[0].decode('latin1')
    print('%s ground: %d x %d points, cell %.1f cm, corner %.0f %.0f %.0f cm, height step %.4f cm'
          % (name, n, n, sx, x0, y0, z0, sy))
    print('  heights %.1f to %.1f m;  %.0f x %.0f m (%.2f x %.2f miles);  color map %s'
          % ((y0 + min(h) * sy) / 100, (y0 + max(h) * sy) / 100, (n - 1) * sx / 100, (n - 1) * sz / 100,
             (n - 1) * sx / 160934.4, (n - 1) * sz / 160934.4, cmap_name))

    # spawn points from the entity text (chunk 9)
    ents = c.get(9, b'').decode('latin1')
    for m in re.finditer(r'entity\("info_spawn_point"\)(.*?)(?=entity\(|\Z)', ents, re.S):
        pos = re.search(r'"position", "([-0-9.e ]+)"', m.group(1))
        tn = re.search(r'"target_name", "([^"]*)"', m.group(1))
        x, y, z = map(float, pos.group(1).split())
        r, col = int(round((z - z0) / sz)), int(round((x - x0) / sx))
        g = y0 + h[r * n + col] * sy if 0 <= r < n and 0 <= col < n else float('nan')
        print('  spawn %-10s at %8.1f %7.1f %8.1f m, %.1f ft above the ground'
              % (tn.group(1) if tn else '', x / 100, y / 100, z / 100, (y - g) / 30.48))

    # color map: 4 x 4 tiles of 512 x 512, tile k at column k % 4, row k // 4
    tiles = [tga(get('colormap\\%s%02d.tga' % (name, k))) for k in range(16)]
    tw = tiles[0][0]
    rows = []
    for tr in range(4):
        for y in range(tw):
            rows.append(b''.join(tiles[tr * 4 + tc][2][y] for tc in range(4)))
    png(os.path.join(out, name + '_color.png'), 4 * tw, rows)

    m = (n - 1) // step + 1
    with open(os.path.join(out, name + '.mtl'), 'w') as f:
        f.write('newmtl ground\nKd 1 1 1\nmap_Kd %s_color.png\n' % name)
    with open(os.path.join(out, name + '.obj'), 'w') as f:
        f.write("# %s ground from Cabela's 4x4 Off-Road Adventure 3, %d x %d points, meters\n" % (name, m, m))
        f.write('mtllib %s.mtl\no %s\n' % (name, name))
        for r in range(0, n, step):
            for col in range(0, n, step):
                f.write('v %.2f %.2f %.2f\n' % ((x0 + col * sx) / 100, (y0 + h[r * n + col] * sy) / 100, (z0 + r * sz) / 100))
        for r in range(m):
            for col in range(m):
                f.write('vt %.5f %.5f\n' % (col / (m - 1), 1 - r / (m - 1)))
        f.write('usemtl ground\n')
        for r in range(m - 1):
            for col in range(m - 1):
                a = r * m + col + 1
                b, e, g = a + 1, a + m, a + m + 1
                f.write('f %d/%d %d/%d %d/%d\n' % (a, a, e, e, b, b))
                f.write('f %d/%d %d/%d %d/%d\n' % (b, b, e, e, g, g))
    print('wrote %s.obj (%d triangles), %s.mtl, %s_color.png (%d x %d) in %s'
          % (name, 2 * (m - 1) ** 2, name, name, 4 * tw, 4 * tw, out))


if __name__ == '__main__':
    main()
