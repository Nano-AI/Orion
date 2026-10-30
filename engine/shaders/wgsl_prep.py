#!/usr/bin/env python3
"""Turns slangc's WGSL for one kernel into what the Dawn backend loads (#375).

    wgsl_prep.py KERNEL.wgsl.raw METAL.json WGSL.json OUT.wgsl

The host was written against Metal and stays that way: textures bind in
declaration order from slot 0, and the parameter block is the C++ struct,
laid out the way Metal lays it out. WGSL differs in three ways this script
settles at build time, so the backend does no shader parsing beyond reading a
header:

1. Bindings. Each texture's Metal slot is mapped to its WGSL @binding, and the
   parameter block's binding is named. A parameter Slang dropped (never read)
   has no @binding in the text and is left out, because an auto layout rejects
   a bind group entry the shader does not declare.
2. The parameter block. A WGSL uniform follows std140-like rules: an array of
   scalars gets a 16-byte stride, a nested struct 16-byte alignment. Metal packs
   both tightly. Slang reports each target's offsets, so the two reflections
   are walked side by side and every leaf becomes a (wgsl offset, metal offset,
   bytes) copy, merged where contiguous. The host repacks with those copies.
3. Storage textures. Slang writes every RWTexture2D as `read_write` with a
   format guessed from the element type. WGSL needs the format of the texture
   actually bound, and core WGSL allows `read_write` on almost no formats. The
   format becomes a token `ORION_FMT_<binding>` the backend fills per bound
   format, and the access becomes `write` unless the kernel reads it back.

Header lines (one per fact, all comments, so the file is still valid WGSL once
the tokens are filled):
    // orion-texture <metal slot> <binding>
    // orion-storage <binding> <slang's format> <write|read_write>
    // orion-params <binding> <wgsl struct bytes> <metal struct bytes>
    // orion-copy <wgsl offset> <metal offset> <bytes>
"""
import json
import re
import sys


def entry(path):
    return json.load(open(path))["entryPoints"][0]


def leaves(t, wgsl_t, w_off, m_off, out):
    """Walk one type in both layouts; append (w_off, m_off, size) for leaves."""
    kind = t["kind"]
    if kind == "struct":
        same(t["fields"], wgsl_t["fields"], "fields of " + t.get("name", "a struct"))
        for f, g in zip(t["fields"], wgsl_t["fields"]):
            leaves(f["type"], g["type"], w_off + g["binding"]["offset"],
                   m_off + f["binding"]["offset"], out)
    elif kind == "array":
        n = t["elementCount"]
        ms, ws = t["uniformStride"], wgsl_t["uniformStride"]
        for i in range(n):
            leaves(t["elementType"], wgsl_t["elementType"], w_off + i * ws, m_off + i * ms, out)
    elif kind in ("scalar", "vector", "matrix"):
        size = uniform_size(t)
        out.append((w_off, m_off, size))
    else:
        raise SystemExit(f"wgsl_prep: unhandled parameter type {kind}")


def same(a, b, what):
    """The two targets' reflections must list the same things in the same
    order; zip() would silently drop a tail and mis-pair the rest."""
    names_a, names_b = [x.get("name") for x in a], [x.get("name") for x in b]
    if names_a != names_b:
        raise SystemExit(f"wgsl_prep: Metal and WGSL disagree on the {what}: {names_a} vs {names_b}")


def uniform_size(t):
    for s in t.get("sizes", []):
        if s["kind"] == "uniform":
            return s["value"]
    if t["kind"] == "scalar":
        return 4
    if t["kind"] == "vector":
        return 4 * t["elementCount"]
    raise SystemExit(f"wgsl_prep: no size for {t}")


def merge(spans):
    out = []
    for w, m, n in sorted(spans):
        if out and out[-1][0] + out[-1][2] == w and out[-1][1] + out[-1][2] == m:
            out[-1] = (out[-1][0], out[-1][1], out[-1][2] + n)
        else:
            out.append((w, m, n))
    return out


def main(raw_path, metal_json, wgsl_json, out_path):
    text = open(raw_path).read()
    metal, wgsl = entry(metal_json), entry(wgsl_json)
    declared = {int(b) for b in re.findall(r"@binding\((\d+)\)", text)}
    header = []
    same(metal["parameters"], wgsl["parameters"], "entry point parameters")
    for mp, wp in zip(metal["parameters"], wgsl["parameters"]):
        if "binding" not in wp:
            continue   # the thread id
        binding = wp["binding"]["index"]
        if binding not in declared:
            continue
        kind = wp["type"]["kind"]
        if kind == "resource":
            header.append(f"// orion-texture {mp['binding']['index']} {binding}")
            if wp["type"].get("access") == "readWrite":
                decl = re.compile(
                    rf"(@binding\({binding}\) @group\(0\) var (\w+) : )texture_storage_2d<(\w+), read_write>")
                m = decl.search(text)
                if not m:
                    raise SystemExit(f"wgsl_prep: no storage declaration for binding {binding}")
                # ⚠ Matches slangc's spelling of the declaration and of
                # `textureLoad((name)`. If a slangc update changes either, this
                # raises above, or leaves `write` on a texture the kernel reads,
                # which Tint rejects when Kernel::create prewarms the pipeline:
                # loudly, at graph compile, never as a wrong picture.
                name, fmt = m.group(2), m.group(3)
                access = "read_write" if f"textureLoad(({name})" in text else "write"
                text = text[:m.start()] + f"{m.group(1)}texture_storage_2d<ORION_FMT_{binding}, {access}>" \
                    + text[m.end():]
                header.append(f"// orion-storage {binding} {fmt} {access}")
        elif kind == "constantBuffer":
            mt, wt = mp["type"]["elementType"], wp["type"]["elementType"]
            spans = []
            leaves(mt, wt, 0, 0, spans)
            wsize = (max(uniform_size(wt), max(w + n for w, _, n in spans)) + 15) // 16 * 16
            # The Metal size is the C++ struct's sizeof: the backend refuses a
            # host block of any other size rather than zero-fill or truncate.
            header.append(f"// orion-params {binding} {wsize} {uniform_size(mt)}")
            header += [f"// orion-copy {w} {m} {n}" for w, m, n in merge(spans)]
        else:
            raise SystemExit(f"wgsl_prep: unhandled parameter kind {kind}")
    with open(out_path, "w") as f:
        f.write("\n".join(header) + "\n\n" + text)


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
