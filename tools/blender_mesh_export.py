"""Export a Blender mesh object as a C builder for the engine's Mesh (docs/BLENDER_MCP.md).

Runs inside Blender, not in the container. Two ways to run it:

    Blender MCP: execute_blender_code with this file's text, preceded by a line such as
        EXPORT_ARGS = {"object_name": "Crate", "c_name": "crate"}
    Command line (no running Blender needed):
        blender --background assets/blender/crate.blend --python tools/blender_mesh_export.py \
            -- --object Crate --c-name crate --out build/crate_mesh.c

The C follows docs/EXTENDING.md "Add a mesh": vertex, index and group tables plus
`<c_name>_build(Mesh *)`, which calls mesh_init, mesh_add_material, one
mesh_begin_group/mesh_end_group per face group, and mesh_finalize.

Conversions:
- Axes: Blender is Z-up, the engine Y-up (both right-handed): (x, y, z) -> (x, z, -y).
  A rotation, so Blender's counter-clockwise front faces stay counter-clockwise.
- Coordinates are the object's local (mesh) space times `scale`, modifiers applied;
  the SceneObject transform places and sizes the mesh in the engine.
- Normals are face normals (the engine shades flat). Faces are grouped per material
  and plane, so each group is planar (culled and lit once); if that needs more than
  16 groups, one curved group per material instead (culled and lit per triangle).
- UVs become texels of a `tex_size` sprite (default 32), V flipped: (u*S, (1-v)*S).
- Material colour: the Principled BSDF Base Color (sRGB bytes), else the viewport
  display colour. A Base Color fed by an Image Texture makes the material
  MATERIAL_TEXTURED with slot `<C_NAME>_SLOT_<IMAGE>`, which the caller #defines.

`result` (the dict the MCP returns) holds "c" (the source), "stats" and "warnings";
"errors" lists limit violations (512 vertices, 1024 indices, 8 materials, 16 groups).
"""
import math
import re
import sys

import bpy

MESH_MAX_VERTICES = 512
MESH_MAX_INDICES = 1024
MESH_MAX_MATERIALS = 8
MESH_MAX_GROUPS = 16


def _to_engine(v):
    return (v[0], v[2], -v[1])


def _linear_to_srgb_byte(c):
    c = max(0.0, min(1.0, c))
    s = 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055
    return int(round(s * 255.0))


def _ident(name):
    s = re.sub(r"[^0-9A-Za-z]+", "_", name).strip("_").lower()
    return s if s and not s[0].isdigit() else "m_" + s


def _material_info(mat):
    """(name, (r, g, b) bytes, image name or None)."""
    if mat is None:
        return ("(no material)", (200, 200, 200), None)
    color = mat.diffuse_color[:3]
    image = None
    tree = getattr(mat, "node_tree", None)
    if tree is not None:
        bsdf = next((n for n in tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if bsdf is not None:
            base = bsdf.inputs.get("Base Color")
            if base is not None:
                if base.is_linked and base.links[0].from_node.type == "TEX_IMAGE":
                    img = base.links[0].from_node.image
                    image = img.name if img else None
                else:
                    color = base.default_value[:3]
    return (mat.name, tuple(_linear_to_srgb_byte(c) for c in color), image)


def _f(x):
    s = "{:.6f}".format(x + 0.0).rstrip("0")
    return (s + "0" if s.endswith(".") else s) + "f"


def n64_export(object_name=None, c_name=None, scale=1.0, tex_size=32, out_path=None):
    obj = bpy.data.objects.get(object_name) if object_name else bpy.context.active_object
    if obj is None or obj.type != "MESH":
        return {"errors": ["no mesh object named {!r} (or no active mesh object)".format(object_name)]}
    c_name = _ident(c_name or obj.name)
    warnings, errors = [], []

    depsgraph = bpy.context.evaluated_depsgraph_get()
    eval_obj = obj.evaluated_get(depsgraph)
    mesh = eval_obj.to_mesh()
    try:
        mesh.calc_loop_triangles()
        uv_layer = mesh.uv_layers.active
        slots = [s.material for s in obj.material_slots] or [None]
        tris = []   # (material slot, normal, plane d, [(pos, uv)] * 3)
        uv_out_of_range = False
        for lt in mesh.loop_triangles:
            if lt.area <= 1e-12:
                continue
            n = _to_engine(mesh.polygons[lt.polygon_index].normal)
            corners = []
            for li in lt.loops:
                p = _to_engine(mesh.vertices[mesh.loops[li].vertex_index].co * scale)
                u, v = uv_layer.data[li].uv if uv_layer else (0.0, 0.0)
                uv_out_of_range |= not (-1e-4 <= u <= 1.0001 and -1e-4 <= v <= 1.0001)
                corners.append((p, (u * tex_size, (1.0 - v) * tex_size)))
            d = sum(n[i] * corners[0][0][i] for i in range(3))
            tris.append((min(lt.material_index, len(slots) - 1), n, d, corners))
    finally:
        eval_obj.to_mesh_clear()
    if not tris:
        return {"errors": ["{!r} has no faces".format(obj.name)]}

    # Materials actually used, in slot order
    used = sorted({t[0] for t in tris})
    mat_index = {slot: i for i, slot in enumerate(used)}
    materials = [_material_info(slots[s]) for s in used]
    if len(materials) > MESH_MAX_MATERIALS:
        errors.append("{} materials (max {})".format(len(materials), MESH_MAX_MATERIALS))

    # Group per material and plane; fall back to one curved group per material
    extent = max(abs(c) for t in tris for p, _ in t[3] for c in p) or 1.0
    groups = {}
    for t in tris:
        key = (mat_index[t[0]],) + tuple(round(c, 3) for c in t[1]) + (round(t[2] / extent, 3),)
        groups.setdefault(key, []).append(t)
    planar = len(groups) <= MESH_MAX_GROUPS
    if not planar:
        warnings.append("{} planes need more than {} groups: one curved group per material "
                        "(culled and lit per triangle)".format(len(groups), MESH_MAX_GROUPS))
        groups = {}
        for t in tris:
            groups.setdefault((mat_index[t[0]],), []).append(t)
        if len(groups) > MESH_MAX_GROUPS:
            errors.append("{} groups (max {})".format(len(groups), MESH_MAX_GROUPS))

    vertices, indices, group_rows = [], [], []
    for key in sorted(groups, key=lambda k: k[0]):
        gtris = groups[key]
        if planar:   # one normal for the whole group, so mesh_analyze_group() finds it planar
            nx, ny, nz = (sum(t[1][i] for t in gtris) for i in range(3))
            ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
            gn = (nx / ln, ny / ln, nz / ln)
        first_v, first_i, lookup = len(vertices), len(indices), {}
        for t in gtris:
            n = gn if planar else t[1]
            for p, uv in t[3]:
                vk = tuple(round(c, 5) for c in p + n + uv)
                if vk not in lookup:
                    lookup[vk] = len(vertices)
                    vertices.append((p, n, uv))
                indices.append(lookup[vk])
        group_rows.append((key[0], first_v, len(vertices) - first_v, first_i, len(indices) - first_i))

    if len(vertices) > MESH_MAX_VERTICES:
        errors.append("{} vertices (max {})".format(len(vertices), MESH_MAX_VERTICES))
    if len(indices) > MESH_MAX_INDICES:
        errors.append("{} indices = {} triangles (max {} = {} triangles)".format(
            len(indices), len(indices) // 3, MESH_MAX_INDICES, MESH_MAX_INDICES // 3))
    if uv_out_of_range:
        warnings.append("UVs outside 0..1: the engine's sprites clamp, they do not tile")
    images = sorted({m[2] for m in materials if m[2]})
    if images and not uv_layer:
        warnings.append("textured material but no UV map")

    lo = [min(v[0][i] for v in vertices) for i in range(3)]
    hi = [max(v[0][i] for v in vertices) for i in range(3)]
    radius = max(math.sqrt(sum(c * c for c in v[0])) for v in vertices)
    upper = c_name.upper()
    slot_macro = {img: "{}_SLOT_{}".format(upper, _ident(img.rsplit(".", 1)[0]).upper()) for img in images}

    out = []
    out.append("// Generated by tools/blender_mesh_export.py from Blender object \"{}\" (mesh \"{}\")."
               .format(obj.name, obj.data.name))
    out.append("// Engine axes (Y up), counter-clockwise front faces, UVs in texels of a {0}x{0} sprite."
               .format(tex_size))
    out.append("// {} vertices, {} triangles, {} {} groups, {} material(s); local bounds "
               "({}, {}, {}) .. ({}, {}, {}), radius {:.3f}.".format(
                   len(vertices), len(indices) // 3, len(group_rows),
                   "planar" if planar else "curved", len(materials),
                   *("{:.3f}".format(c) for c in lo + hi), radius))
    for img in images:
        out.append("// {}: the slot holding rom:/{}.sprite (assets/{}.png), #define it before this code."
                   .format(slot_macro[img], _ident(img.rsplit(".", 1)[0]), _ident(img.rsplit(".", 1)[0])))
    out.append("")
    out.append("static const MeshVertex {}_vertices[{}] = {{".format(c_name, len(vertices)))
    for p, n, uv in vertices:
        out.append("    {{{{{}, {}, {}}}, {{{}, {}, {}}}, {{{}, {}}}}},".format(
            *(_f(c) for c in p + n + uv)))
    out.append("};")
    out.append("")
    out.append("static const uint16_t {}_indices[{}] = {{".format(c_name, len(indices)))
    for i in range(0, len(indices), 12):
        out.append("    " + " ".join("{},".format(x) for x in indices[i:i + 12]))
    out.append("};")
    out.append("")
    out.append("// Face groups: material, first vertex, vertex count, first index, index count")
    out.append("static const uint16_t {}_groups[{}][5] = {{".format(c_name, len(group_rows)))
    for row in group_rows:
        out.append("    {{{}, {}, {}, {}, {}}},".format(*row))
    out.append("};")
    out.append("")
    out.append("void {}_build(Mesh *mesh) {{".format(c_name))
    out.append("    mesh_init(mesh);")
    for name, rgb, img in materials:
        out.append("    mesh_add_material(mesh, (Material){{   // \"{}\"".format(name))
        if img:
            out.append("        .type = MATERIAL_TEXTURED,")
            out.append("        .texture_slot = {},".format(slot_macro[img]))
        else:
            out.append("        .type = MATERIAL_FLAT_COLOR,")
            out.append("        .texture_slot = -1,")
        out.append("        .base_color = {{{}, {}, {}}}".format(*rgb))
        out.append("    });")
    out.append("    for (int g = 0; g < {}; g++) {{".format(len(group_rows)))
    out.append("        const uint16_t *grp = {}_groups[g];".format(c_name))
    out.append("        mesh_begin_group(mesh, grp[0]);")
    out.append("        for (int v = grp[1]; v < grp[1] + grp[2]; v++)")
    out.append("            mesh_add_vertex(mesh, {}_vertices[v]);".format(c_name))
    out.append("        for (int i = grp[3]; i < grp[3] + grp[4]; i += 3)")
    out.append("            mesh_add_triangle(mesh, {0}_indices[i], {0}_indices[i + 1], {0}_indices[i + 2]);"
               .format(c_name))
    out.append("        mesh_end_group(mesh);")
    out.append("    }")
    out.append("    mesh_finalize(mesh);")
    out.append("}")
    source = "\n".join(out) + "\n"

    if out_path:
        with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(source)
    return {
        "c": source if not out_path else "(written to {})".format(out_path),
        "stats": {"object": obj.name, "c_name": c_name, "vertices": len(vertices),
                  "triangles": len(indices) // 3, "groups": len(group_rows), "planar": planar,
                  "materials": [{"name": m[0], "base_color": m[1], "image": m[2]} for m in materials],
                  "bounds_min": lo, "bounds_max": hi, "radius": radius},
        "warnings": warnings,
        "errors": errors,
    }


def _cli_args(argv):
    import argparse
    parser = argparse.ArgumentParser(prog="blender_mesh_export.py")
    parser.add_argument("--object", dest="object_name")
    parser.add_argument("--c-name", dest="c_name")
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--tex-size", dest="tex_size", type=int, default=32)
    parser.add_argument("--out", dest="out_path")
    return vars(parser.parse_args(argv))


if "EXPORT_ARGS" in globals():
    result = n64_export(**EXPORT_ARGS)  # noqa: F821 (set by the MCP call)
elif "--" in sys.argv:
    result = n64_export(**_cli_args(sys.argv[sys.argv.index("--") + 1:]))
    for line in result.get("warnings", []) + result.get("errors", []):
        print("blender_mesh_export: " + line, file=sys.stderr)
    if "c" in result and not result["c"].startswith("(written"):
        print(result["c"])
    if result.get("errors"):
        sys.exit(1)
