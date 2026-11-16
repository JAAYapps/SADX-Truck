# Cabela's 4x4 (2001) heightmap -> OBJ.
# File: 5 floats (x0, y0, z0, cell, hscale), then 2048 x 2048 uint16 heights.
# Height formula y0 + h * hscale is a guess, not read from the EXE.
import sys, struct
src, dst, out_n = sys.argv[1], sys.argv[2], int(sys.argv[3])
d = open(src, 'rb').read()
x0, y0, z0, cell, hscale = struct.unpack_from('<5f', d, 0)
N = 2048
step = N // out_n
h = struct.unpack_from('<%dH' % (N * N), d, 20)
with open(dst, 'w') as f:
    f.write('# Baja from Cabela\'s 4x4 Off-Road Adventure, %d x %d\nmtllib baja_256.mtl\no baja\n' % (out_n, out_n))
    for r in range(out_n):
        for c in range(out_n):
            f.write('v %.3f %.3f %.3f\n' % (x0 + c * step * cell, y0 + h[r * step * N + c * step] * hscale, z0 + r * step * cell))
    for r in range(out_n):
        for c in range(out_n):
            f.write('vt %.5f %.5f\n' % (c / (out_n - 1), 1 - r / (out_n - 1)))
    f.write('usemtl baja\n')
    for r in range(out_n - 1):
        for c in range(out_n - 1):
            a = r * out_n + c + 1
            b, e, g = a + 1, a + out_n, a + out_n + 1
            f.write('f %d/%d %d/%d %d/%d\n' % (a, a, e, e, b, b))
            f.write('f %d/%d %d/%d %d/%d\n' % (b, b, e, e, g, g))
