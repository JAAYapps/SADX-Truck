#!/usr/bin/env python3
# List the bones in a Cabela's 3 .MODEL file (FBMF, "BoneSkeleton").
#
# usage: model_bones.py <file.MODEL>
#
# Record layout, read from CAR1_0.MODEL by hand:
#   "Bone", 1 byte name length, name,
#   2 bytes (always 3 so far),
#   position  3 floats (x, y, z, meters, Y up),
#   rotation  3 floats (x, y, z angles in degrees),
#   scale     3 floats,
#   then a few bytes before the next "Bone".
# Most positions look like model space (weighCenter_back sits exactly at
# the rear wheels' height and Z, same as axes_1), but the BoneNN ones are
# clearly relative to a parent bone (Bone06 at 0.648, 0, 0).  Which bone
# is whose parent is not decoded.

import struct
import sys


def bones(data):
    at = data.find(b"Bone", 0)
    while at >= 0:
        n = data[at + 4]
        name = data[at + 5:at + 5 + n]
        if n == 0 or not name.isascii() or not name.replace(b"_", b"").isalnum():
            at = data.find(b"Bone", at + 4)
            continue
        p = at + 5 + n + 2
        if p + 36 > len(data):
            break
        vals = struct.unpack_from("<9f", data, p)
        yield name.decode(), vals[0:3], vals[3:6], vals[6:9]
        at = data.find(b"Bone", p + 36)


def main():
    data = open(sys.argv[1], "rb").read()
    for name, pos, rot, scale in bones(data):
        print("%-20s pos %9.6f %9.6f %9.6f   rot %8.3f %8.3f %8.3f   scale %5.3f %5.3f %5.3f"
              % ((name,) + tuple(pos) + tuple(rot) + tuple(scale)))


if __name__ == "__main__":
    main()
