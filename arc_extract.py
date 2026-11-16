# Extracts one file from a FUN Labs Cora3 .arc archive (format: see arc_list.py).
# Usage: arc_extract.py <archive.arc> <name inside, e.g. BSP\BJ_A.BSP> <output file>
# Small entries are stored raw; compressed ones start with 'x' (zlib).
import sys, struct, zlib
arc, want, dst = sys.argv[1], sys.argv[2], sys.argv[3]
d = open(arc, 'rb').read()
i = d.rfind(b'ARC\x00')
ver, blk, usz, csz, cnt = struct.unpack_from('<5I', d, i + 4)
dr = zlib.decompress(d[blk*2048:blk*2048+csz])
p = 0
for _ in range(cnt):
    b, nb, sz, cs, ff, nl = struct.unpack_from('<6I', dr, p + 4)
    name = dr[p+28:p+28+nl-1].decode('latin1')
    p += 28 + nl
    if name.lower() == want.lower():
        raw = d[b*2048:b*2048+cs]
        data = zlib.decompress(raw) if raw[:1] == b'x' and cs != sz else d[b*2048:b*2048+sz]
        assert len(data) == sz, (len(data), sz)
        open(dst, 'wb').write(data)
        print(name, sz, 'bytes ->', dst)
        break
else:
    sys.exit('not found: ' + want)
