"""从只读场景 POL 提取真实山墙表面，生成镜头使用的三角形查询树。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np
import lzokay


def triangles_from_pol(data):
    magic, version, meshes = struct.unpack_from("<4sII", data)
    assert magic == b"POLY" and version in (100, 200), (magic, version)
    pos = 12 + meshes * 52
    if version > 100:
        extra = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        for _ in range(extra):
            string_size = struct.unpack_from("<I", data, pos + 100)[0]
            pos += 104 + string_size
    triangles = []
    materials = {}
    for mesh_index in range(meshes):
        name_start = 12 + mesh_index * 52
        name = data[name_start:name_start + 32].split(b"\0", 1)[0]
        flags, count = struct.unpack_from("<II", data, pos + 24)
        pos += 32
        assert flags & 1 and flags & 16
        stride = 20 + 12 * bool(flags & 2) + 4 * bool(flags & 4) + 4 * bool(flags & 8)
        stride += 8 * bool(flags & 32) + 8 * bool(flags & 64) + 8 * bool(flags & 128) + 16 * bool(flags & 256)
        assert flags & ~511 == 0
        vertices = np.ndarray((count, 3), dtype="<f4", buffer=data, offset=pos, strides=(stride, 4)).copy()
        pos += count * stride
        groups = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        for subset in range(groups):
            alpha = struct.unpack_from("<I", data, pos)[0]
            textures = struct.unpack_from("<I", data, pos + 72)[0]
            pos += 76 + textures * 64
            faces = struct.unpack_from("<I", data, pos + 12)[0]
            pos += 16
            indices = np.frombuffer(data, dtype="<u2", count=faces * 3, offset=pos).reshape(-1, 3)
            pos += faces * 6
            assert not indices.size or indices.max() < count
            materials[alpha] = materials.get(alpha, 0) + faces
            # 半透明水面、叶片和装饰不作为整块山墙，避免不断把镜头挤到人物身上。
            if alpha == 0 and faces:
                triangles.append(vertices[indices])
    assert pos == len(data), (pos, len(data))
    result = np.concatenate(triangles) if triangles else np.empty((0, 3, 3), dtype="<f4")
    assert np.isfinite(result).all()
    return result, materials


def write_tree(path, triangles):
    nodes, ordered = [], []
    centers = triangles.mean(axis=1)
    lower, upper = triangles.min(axis=1), triangles.max(axis=1)

    def build(indices):
        index = len(nodes)
        lo, hi = lower[indices].min(axis=0), upper[indices].max(axis=0)
        nodes.append(None)
        if len(indices) <= 12:
            first = len(ordered)
            ordered.extend(indices.tolist())
            nodes[index] = (*lo, *hi, first, len(indices), 0, 0)
        else:
            axis = int((hi - lo).argmax())
            indices = indices[np.argsort(centers[indices, axis], kind="stable")]
            mid = len(indices) // 2
            left, right = build(indices[:mid]), build(indices[mid:])
            nodes[index] = (*lo, *hi, 0, 0, left, right)
        return index

    if len(triangles):
        build(np.arange(len(triangles)))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(struct.pack("<4sIII", b"P3CM", 1, len(nodes), len(triangles)))
        for node in nodes:
            output.write(struct.pack("<6f4I", *node))
        output.write(triangles[ordered].astype("<f4").tobytes())
    return len(nodes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world")
    parser.add_argument("--游戏目录", type=Path, required=True)
    parser.add_argument("--输出", type=Path)
    args = parser.parse_args()
    game = args.游戏目录.resolve()
    if not (game/'PAL3.exe').is_file() or not (game/'scene').is_dir():
        parser.error('游戏目录必须包含 PAL3.exe 和 scene')
    output = (args.输出 or game/'hd/camera').resolve()
    if output == game or output.is_relative_to(game/'scene') or output.is_relative_to(game/'save'):
        parser.error('输出不能覆盖原版场景或存档')
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for package in sorted((game / "scene").glob("*.cpk")):
        if args.world and package.stem.lower() != args.world.lower():
            continue
        with package.open("rb") as stream:
            header = struct.unpack("<32I", stream.read(128))
            entries = [struct.unpack("<7I", stream.read(28)) for _ in range(header[5])]
            names = []
            for entry in entries:
                stream.seek(entry[3] + entry[4])
                names.append(stream.read(entry[6]).split(b"\0", 1)[0].decode("gbk"))
            indices = {entry[0]: i for i, entry in enumerate(entries)}
            for index, entry in enumerate(entries):
                if not entry[1] & 1 or entry[1] & 0x10:
                    continue
                components, parent = [names[index]], entry[2]
                while parent in indices:
                    j = indices[parent]
                    components.insert(0, names[j])
                    parent = entries[j][2]
                relative = "/".join(components)
                match = re.fullmatch(r"([^/]+)/\1\.pol", relative, re.I)
                if not match:
                    continue
                stream.seek(entry[3])
                raw = stream.read(entry[4])
                if not entry[1] & 0x10000:
                    raw = lzokay.decompress(raw, entry[5])
                triangles, materials = triangles_from_pol(raw)
                destination = output / package.stem / (match[1] + ".p3cm")
                nodes = write_tree(destination, triangles)
                row = {"world": package.stem, "scene": match[1], "triangles": len(triangles),
                       "nodes": nodes, "materials": materials, "bytes": destination.stat().st_size,
                       "source_sha256": hashlib.sha256(raw).hexdigest()}
                results.append(row)
                print(json.dumps(row, ensure_ascii=False), flush=True)
    (output / "地图数据索引.json").write_text(
        json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
