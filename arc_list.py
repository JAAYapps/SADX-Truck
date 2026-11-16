# Lists a FUN Labs Cora3 .arc archive.
# Footer "ARC\0": version, dir_block, dir_size, dir_csize, count (blocks are 2048 bytes)
# Entry "ARCH": block, nblocks, size, csize, 0xFFFFFFFF, namelen, name\0
import sys, struct, zlib, collections
d = open(sys.argv[1], 'rb').read()
i = d.rfind(b'ARC\x00')
ver, blk, usz, csz, cnt = struct.unpack_from('<5I', d, i + 4)
dr = zlib.decompress(d[blk*2048:blk*2048+csz])
p = 0; out = []
for _ in range(cnt):
    assert dr[p:p+4] == b'ARCH', p
    b, nb, sz, cs, ff, nl = struct.unpack_from('<6I', dr, p + 4)
    name = dr[p+28:p+28+nl-1].decode('latin1')
    out.append((name, b, sz, cs)); p += 28 + nl
for n, b, sz, cs in out: print(f'{sz:>10} {b:>7} {n}')
