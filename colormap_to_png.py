# Cabela's 4x4 (2001) colormap -> PNG.
# 2048 x 2048, 2 bytes per pixel. RGB565 is a guess.
import sys, struct, zlib
res, off, csize, dst, step = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], int(sys.argv[5])
with open(res, 'rb') as f:
    f.seek(off)
    d = zlib.decompress(f.read(csize))
N = 2048
px = struct.unpack('<%dH' % (N * N), d)
rows = []
for y in range(0, N, step):
    row = bytearray(b'\0')
    for x in range(0, N, step):
        p = px[y * N + x]
        r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
        row += bytes((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    rows.append(bytes(row))
W = N // step
def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
open(dst, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', W, W, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b''))
