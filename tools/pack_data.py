#!/usr/bin/env python3
# pack_data.py [source dir] [output.zip] [--extras]
#
# ZIP the Blood 1.21 files the board needs, laid out for the SD card
# (blood/BLOOD.RFF, ...), from an installed copy of the game: no EXEs, no
# setup or network files. For your own card only; not for distribution.
#
#   source dir   the game's folder (default /Volumes/1TB/Documents/Blood)
#   output.zip   default build/blood-data.zip
#   --extras     also the attract demos (BLOOD00x.DEM, also from
#                ~/Downloads-style folders given with --demos DIR) and the
#                Cryptic Passage files
#
# File names are matched without regard to case and stored in upper case.
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: GPL-2.0-or-later
import os
import sys
import zipfile

REQUIRED = ["BLOOD.INI", "BLOOD.RFF", "GUI.RFF", "SOUNDS.RFF", "SURFACE.DAT", "VOXEL.DAT"] + \
           ["TILES%03d.ART" % i for i in range(18)]
RECOMMENDED = ["TABLES.DAT"]           # the engine reads it when present
EXTRAS_PREFIX = ["BLOOD0", "CP", "CRYPTIC"]
EXTRAS_SUFFIX = [".DEM", ".MAP", ".AR_", ".INI", ".SMK", ".WAV"]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    extras = "--extras" in sys.argv
    demos_dirs = []
    if "--demos" in sys.argv:
        i = sys.argv.index("--demos")
        demos_dirs.append(sys.argv[i + 1])
        args = [a for a in args if a != sys.argv[i + 1]]
    src = args[0] if args else "/Volumes/1TB/Documents/Blood"
    here = os.path.dirname(os.path.abspath(__file__))
    out = args[1] if len(args) > 1 else os.path.join(here, "..", "build", "blood-data.zip")
    out = os.path.abspath(out)

    by_upper = {f.upper(): os.path.join(src, f) for f in os.listdir(src)
                if os.path.isfile(os.path.join(src, f))}
    missing = [f for f in REQUIRED if f not in by_upper]
    if missing:
        sys.exit("missing in %s: %s" % (src, " ".join(missing)))

    files = [(f, by_upper[f]) for f in REQUIRED + RECOMMENDED if f in by_upper]
    if extras:
        for f, p in sorted(by_upper.items()):
            if any(f.startswith(a) for a in EXTRAS_PREFIX) and any(f.endswith(b) for b in EXTRAS_SUFFIX):
                if f not in dict(files):
                    files.append((f, p))
        for d in demos_dirs:
            for f in sorted(os.listdir(d)):
                if f.upper().startswith("BLOOD") and f.upper().endswith(".DEM"):
                    files.append((f.upper(), os.path.join(d, f)))

    os.makedirs(os.path.dirname(out), exist_ok=True)
    total = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name, path in files:
            z.write(path, "blood/" + name)
            total += os.path.getsize(path)
    print("%s: %d files, %.1f MB of data, %.1f MB zipped" %
          (out, len(files), total / 1048576, os.path.getsize(out) / 1048576))
    print("Unzip to the root of the SD card (it makes /blood).")


if __name__ == "__main__":
    main()
