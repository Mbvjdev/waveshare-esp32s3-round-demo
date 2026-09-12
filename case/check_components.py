#!/usr/bin/env python3
"""Find connected components in an STL and report their bounding boxes."""
import struct
import sys
from collections import defaultdict


def load_stl(path):
    data = open(path, "rb").read()
    if data[:5] == b"solid" and b"facet" in data[:200]:
        # ASCII
        tris = []
        cur = []
        for line in data.decode("utf-8", "replace").splitlines():
            line = line.strip()
            if line.startswith("vertex"):
                parts = line.split()
                cur.append(tuple(float(x) for x in parts[1:4]))
                if len(cur) == 3:
                    tris.append(tuple(cur))
                    cur = []
        return tris
    n = struct.unpack_from("<I", data, 80)[0]
    tris = []
    off = 84
    for _ in range(n):
        vals = struct.unpack_from("<12f", data, off)
        tris.append((vals[3:6], vals[6:9], vals[9:12]))
        off += 50
    return tris


def main(path):
    tris = load_stl(path)
    # Union-find over triangles sharing an edge (quantised vertices).
    parent = list(range(len(tris)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    edge_map = defaultdict(list)
    for i, t in enumerate(tris):
        q = [tuple(round(c, 4) for c in v) for v in t]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            key = tuple(sorted((q[a], q[b])))
            edge_map[key].append(i)

    for key, idxs in edge_map.items():
        for j in idxs[1:]:
            union(idxs[0], j)

    groups = defaultdict(list)
    for i in range(len(tris)):
        groups[find(i)].append(i)

    print(f"{path}: {len(tris)} triangles, {len(groups)} connected component(s)")
    for gi, (root, members) in enumerate(
        sorted(groups.items(), key=lambda kv: -len(kv[1])), 1
    ):
        xs = [c[0] for i in members for c in tris[i]]
        ys = [c[1] for i in members for c in tris[i]]
        zs = [c[2] for i in members for c in tris[i]]
        print(
            f"  part {gi}: {len(members):6d} tris  "
            f"X[{min(xs):7.2f},{max(xs):7.2f}] "
            f"Y[{min(ys):7.2f},{max(ys):7.2f}] "
            f"Z[{min(zs):7.2f},{max(zs):7.2f}]"
        )


if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
