#!/usr/bin/env python3
"""Generate a deterministic synthetic MO2 instance for the refresh benchmark.

    gen_instance.py --mods 2000 --files 200000 --seed 1 <dir>

Creates <dir>/mods/mod_NNNN/ with a Bethesda-style tree (meshes/, textures/, scripts/,
interface/), file and directory names in mixed case, about 10% of the files of each mod
at a path that an earlier mod already has (sometimes with a different case, so the
case-insensitive merge of the data tree is exercised), files of 0-4 KiB, an empty
<dir>/data (the game data directory) and <dir>/profiles/Default/modlist.txt with every
mod enabled. The same arguments always produce the same instance.
"""

import argparse
import os
import random
import sys

OVERLAP = 0.10
MAX_SIZE = 4096

ROOTS = {
    "meshes": [".nif", ".nif", ".nif", ".kf", ".hkx"],
    "textures": [".dds", ".dds", ".dds", ".png", ".tga"],
    "scripts": [".pex", ".pex", ".psc", ".psc"],
    "interface": [".swf", ".swf", ".txt", ".xml"],
}
ROOT_WEIGHTS = [4, 5, 2, 1]
SUBDIRS = [
    "actors", "armor", "weapons", "clothes", "architecture", "landscape", "creatures",
    "character", "effects", "furniture", "clutter", "dungeons", "interiors", "sky",
    "fonts", "translations", "source", "fx", "magic", "ships", "plants", "ui",
]
WORDS = [
    "iron", "steel", "ebony", "glass", "dragon", "nord", "imperial", "falmer", "troll",
    "bear", "wolf", "chest", "door", "wall", "floor", "roof", "banner", "torch", "lamp",
    "sword", "shield", "helmet", "boots", "gauntlets", "cuirass", "bow", "arrow",
    "table", "chair", "bed", "barrel", "crate", "rock", "tree", "grass", "snow",
    "ice", "fire", "frost", "shock", "heal", "ward", "alpha", "beta", "gamma", "delta",
]
SUFFIXES = ["", "_d", "_n", "_s", "_g", "_01", "_02", "_lod", "_low", "_hi", "_glow"]


def mixed_case(rng, s):
    style = rng.randrange(5)
    if style == 0:
        return s.lower()
    if style == 1:
        return s.upper()
    if style == 2:
        return s.title()
    if style == 3:
        return s[:1].upper() + s[1:]
    return "".join(c.upper() if rng.random() < 0.5 else c for c in s)


def new_path(rng):
    root = rng.choices(list(ROOTS), ROOT_WEIGHTS)[0]
    parts = [root]
    for _ in range(rng.randint(1, 4)):
        parts.append(rng.choice(SUBDIRS) + (str(rng.randrange(8)) if rng.random() < 0.2 else ""))
    base = rng.choice(WORDS) + rng.choice(SUFFIXES)
    if rng.random() < 0.5:
        base += str(rng.randrange(1000))
    parts.append(base + rng.choice(ROOTS[root]))
    return "/".join(mixed_case(rng, p) if i else p for i, p in enumerate(parts))


def recase(rng, path):
    return "/".join(mixed_case(rng, p) for p in path.split("/"))


def split_counts(rng, mods, files):
    # uneven mods (1 file up to a few times the average) that add up to exactly `files`
    weights = [rng.random() ** 2 + 0.05 for _ in range(mods)]
    total = sum(weights)
    counts = [max(1, int(files * w / total)) for w in weights]
    i = 0
    while sum(counts) != files:
        d = files - sum(counts)
        j = rng.randrange(mods)
        if d > 0:
            counts[j] += 1
        elif counts[j] > 1:
            counts[j] -= 1
        i += 1
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--mods", type=int, default=2000)
    ap.add_argument("--files", type=int, default=200000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("dir")
    args = ap.parse_args()

    if args.mods < 1 or args.files < args.mods:
        ap.error("need at least one mod and one file per mod")

    out = os.path.abspath(args.dir)
    if os.path.exists(os.path.join(out, "mods")):
        sys.exit(f"{out}/mods already exists")

    rng = random.Random(args.seed)
    counts = split_counts(rng, args.mods, args.files)
    pool = []  # every path written so far, as written
    names = []
    total_bytes = 0
    overlapped = 0

    for m in range(args.mods):
        name = f"mod_{m:04d}"
        names.append(name)
        mod_dir = os.path.join(out, "mods", name)
        os.makedirs(mod_dir, exist_ok=True)

        wanted = counts[m]
        seen = set()
        paths = []

        # paths an earlier mod already has (the conflicts), maybe in another case
        for _ in range(round(wanted * OVERLAP) if pool else 0):
            p = rng.choice(pool)
            if rng.random() < 0.5:
                p = recase(rng, p)
            if p.lower() not in seen:
                seen.add(p.lower())
                paths.append(p)
                overlapped += 1

        guard = 0
        while len(paths) < wanted:
            p = new_path(rng)
            guard += 1
            if p.lower() in seen:
                if guard > 50 * wanted:
                    sys.exit("name space too small for the requested size")
                continue
            seen.add(p.lower())
            paths.append(p)
            pool.append(p)

        for p in paths:
            full = os.path.join(mod_dir, p)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            size = rng.randint(0, MAX_SIZE)
            with open(full, "wb") as f:
                f.write(rng.randbytes(size))
            total_bytes += size

    os.makedirs(os.path.join(out, "data"), exist_ok=True)
    profile = os.path.join(out, "profiles", "Default")
    os.makedirs(profile, exist_ok=True)
    with open(os.path.join(profile, "modlist.txt"), "w") as f:
        f.write("# This file was automatically generated by Mod Organizer.\n")
        # highest priority first, like Mod Organizer writes it
        for name in reversed(names):
            f.write(f"+{name}\n")

    print(f"generated mods={args.mods} files={args.files} seed={args.seed} "
          f"overlapping={overlapped} bytes={total_bytes} dir={out}")


if __name__ == "__main__":
    main()
