# Lists a Cabela's 4x4 (2001) data*.res file.
# Header: magic 4660, blob_csize. Then zlib blob. Then dir_csize, zlib dir.
# Dir entry: 256-byte name, then 3 x uint32 (offset, size, flags).
import sys, struct, zlib
d = open(sys.argv[1], 'rb').read()
t = 8 + struct.unpack_from('<I', d, 4)[0]
x = zlib.decompress(d[t+4:])
ESZ = 268  # 256-byte name + offset, size, flags
for i in range(0, len(x) - ESZ + 1, ESZ):
    name = x[i:i+256].split(b'\0')[0].decode('latin1')
    off, sz, fl = struct.unpack_from('<3I', x, i + 256)
    print(f'{sz:>10} {off:>10} {fl:>8} {name}')
