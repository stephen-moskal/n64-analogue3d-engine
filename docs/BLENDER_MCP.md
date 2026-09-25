# Blender MCP

Claude Code can drive a running Blender through the Blender Lab MCP server: inspect the scene, run `bpy` code, take screenshots and render. In this project it is how assets get from Blender into the engine until the Phase 4 glTF/Tiny3D pipeline exists (ROADMAP_v2 §8, which adds `BLENDER_SETUP.md` and `ASSET_PIPELINE.md` for Fast64):

- **Meshes** become C builder code for the CPU mesh path (`tools/blender_mesh_export.py`).
- **Textures and billboard sprites** become 32×32 PNGs in `assets/`.

Phase 3 adds a runtime path (ROADMAP_v2 §7, stage S4): meshes exported as JSON into `assets/meshes/`, built into ROM files the engine loads at run time, and previewed in a mesh viewer scene. Until then the exporter writes C. The MCP tools appear only in Claude Code sessions started after the server is registered, with Blender running.

## Quick start

1. **Blender 5.1 or later with Blender Lab's MCP add-on.** In Preferences → Get Extensions → Repositories, add the remote repository `https://lab.blender.org/`, then install and enable **MCP**. Preferences → System → Network → **Allow Online Access** must be on, or the add-on refuses to start. With Auto Start (the default) it listens on `localhost:9876` whenever Blender runs; its preferences say "Server is running".
2. **uv**, which provides `uvx`: `winget install --id=astral-sh.uv -e` (Windows) or `brew install uv` (macOS). It installs to `~/.local/bin`: open a new terminal afterwards.
3. **Register the server with Claude Code**, once per machine. The VS Code extension does not put `claude` on PATH, so on Windows this one-liner finds its bundled `claude.exe` (paste it as one line):

   ```powershell
   $c = (Get-ChildItem "$env:USERPROFILE\.vscode\extensions\anthropic.claude-code-*\resources\native-binary\claude.exe" | Sort-Object LastWriteTime | Select-Object -Last 1).FullName; & $c mcp add blender -e "BLENDER_PATH=C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -- uvx --from "git+https://projects.blender.org/lab/blender_mcp.git#subdirectory=mcp" blender-mcp; & $c mcp get blender
   ```

   macOS, or wherever `claude` is on PATH:

   ```sh
   claude mcp add blender -e BLENDER_PATH=/Applications/Blender.app/Contents/MacOS/Blender -- uvx --from "git+https://projects.blender.org/lab/blender_mcp.git#subdirectory=mcp" blender-mcp
   ```

4. **Check it:** `claude mcp get blender` shows `✔ Connected`. The first start builds the server (about a minute); if that times out, run it again. The tools appear in Claude Code sessions started afterwards (`/mcp` lists them).

Notes:

- The default scope is `local`: private to you, for this project only, stored in `~/.claude.json`; nothing is committed. `-s user` makes it available in every project. To change the registration, `mcp remove blender -s local` first.
- `BLENDER_PATH` is only for the `*_for_cli` tools, which start a background Blender; everything else talks to the running one.
- **Not `uvx blender-mcp`.** PyPI's `blender-mcp` is a different project (ahujasid's, now `mcp-for-blender`) with its own add-on. It uses the same port with another protocol: against Blender Lab's add-on its handshake stalls for ~40 s and Claude Code gives up after 30 s. Blender Lab's server has the same command name but is not on PyPI, hence `--from git+…`.

## Tools

| Tool | Use |
|---|---|
| `get_objects_summary` | Collections and their objects (type, parent, data, selection, visibility). Start here. |
| `get_object_detail_summary(name)` | One object: transforms, modifiers, constraints, materials, parent and children |
| `get_blendfile_summary_datablocks`, `_path_info`, `_missing_files`, `_of_linked_libraries`, `_usage_guess` | The file: data-block counts, path and save state, missing images, linked libraries |
| `get_screenshot_of_window_as_image`, `get_screenshot_of_area_as_image(area_ui_type)` | See what is on screen (PNG) |
| `get_screenshot_of_window_as_json` | Window layout, active object and selection as JSON |
| `jump_to_view3d_object_by_name`, `jump_to_view3d_object_data_by_name`, `jump_to_tab_by_name`, `jump_to_tab_by_space_type` | Point the viewport or the workspace at something |
| `render_viewport_to_path(output_path)`, `render_thumbnail_to_path(output_path)` | Render with the scene's settings, or a quick thumbnail. Only the file name of `output_path` is used: the image lands in Blender's temp folder (`<tempdir>/blender_mcp/`) and the result gives the real path. |
| `execute_blender_code(code)` | Any `bpy` code in the running Blender. Assign a JSON-serialisable dict to `result` to get data back. The server calls it a last resort: prefer the tools above. |
| `*_for_cli(blend_file, …)` | The same tools on a saved `.blend` in a background Blender, without the UI (needs `BLENDER_PATH`; 120 s limit) |
| `search_api_docs`, `search_manual_docs`, `get_python_api_docs(identifier)` | The bundled Blender Python API reference and manual: check operator names and arguments here instead of guessing |

The server's own rules, which apply here too: inspect the scene before assuming anything; do not modify or delete the user's objects without asking; set the mode, active object and selection explicitly before calling operators. `execute_blender_code` runs with the user's full permissions (files, network), so treat it like a shell command. Environment variables: `BLENDER_MCP_HOST` / `BLENDER_MCP_PORT` (default `localhost:9876`; must match the add-on's preferences), `BLENDER_PATH`.

## Engine conventions for assets

| | In Blender | In the engine |
|---|---|---|
| Up axis | +Z | +Y (right-handed). The exporter maps (x, y, z) → (x, z, −y): Blender's front view (looking along +Y) shows the engine's +Z side. |
| Front faces | Normals point outward (Mesh ▸ Normals ▸ Recalculate Outside) | Counter-clockwise seen from outside ([MESH_SYSTEM.md](MESH_SYSTEM.md)) |
| Size | Model around the origin at about unit size (±1) | The object's `scale` sizes it: the demo's cube is ±80 units, the floor 1000 × 1000 in 100-unit tiles |
| Shading | Flat faces; smooth normals are ignored | Flat, one lit colour per face (Gouraud: Phase 3 S5) |
| Colour | Principled BSDF Base Color | `Material.base_color` (sRGB bytes), multiplied by the lighting |
| Texture | Image Texture into Base Color; UVs inside 0..1 | `MATERIAL_TEXTURED`: 32×32 RGBA16 sprite, UVs 0..32 texels, V flipped. Sprites clamp, they do not tile. The exporter's `tex_size` is one value for U and V, so textures must be square (the JSON path of Phase 3 S4 scales by each image's own size). |
| Per mesh | | 512 vertices, 341 triangles, 8 materials, 16 face groups. Flat faces duplicate their vertices, so about 250 flat triangles fit in 512 vertices |

**Triangle budget.** The CPU path costs about 18 µs per drawn triangle in `mesh_draw`'s triangle loop, ~23 µs for the whole object path (Phase 2 exit, [BENCHMARKS.md](BENCHMARKS.md)): the benchmark holds 24 pillars (379 drawn triangles) at 60 FPS with 7 ms to spare. v1's targets per object: environment prop 20–50, architecture 30–80, character 150–300, boss 300–500 (ROADMAP_v2 §8). A boss is over one mesh's limit (341 triangles, fewer when flat), so split it across meshes.

**Where files go.**

- `.blend` sources: `assets/blender/<name>.blend`. The Makefile converts only top-level `assets/*.png`, so nothing in that folder reaches the ROM; Blender's `*.blend1` backups are git-ignored.
- Textures and sprites: `assets/<name>.png` (top level), loaded as `rom:/<name>.sprite`.
- Generated mesh C: in the module that owns the mesh. Shared shapes go in `src/render/mesh_defs.c`, a scene's own in its `.c` or in a new `src/scenes/<scene>_meshes.c`. The Makefile compiles only `src/*.c` and `src/*/*.c`, so deeper folders are not built.

## Making a mesh

1. **Model** in Blender, or ask Claude to. Keep it low-poly with flat faces and one material per surface colour. The exporter reads the mesh in its own local space with modifiers applied: the object's location, rotation and scale are ignored, so apply them (Ctrl+A) if they are part of the shape.
2. **Export.** Claude runs [tools/blender_mesh_export.py](../tools/blender_mesh_export.py) with `execute_blender_code`: the code is one line of arguments followed by the file's text.

   ```python
   EXPORT_ARGS = {"object_name": "Crate", "c_name": "crate"}
   # ...then the contents of tools/blender_mesh_export.py
   ```

   | Argument | Default | Meaning |
   |---|---|---|
   | `object_name` | active object | Blender object to export |
   | `c_name` | object name | Prefix of the generated tables and of `<c_name>_build()` |
   | `scale` | 1.0 | Multiplies every position |
   | `tex_size` | 32 | Sprite size the UVs are scaled to |
   | `out_path` | none | Write the C to this file instead of returning it |

   The result holds `c` (the source), `stats` (vertex, triangle and group counts, materials, local bounds), `warnings` and `errors`. The same script works without the UI: `blender --background assets/blender/crate.blend --python tools/blender_mesh_export.py -- --object Crate --c-name crate --out build/crate_mesh.c`.
3. **Read the errors and warnings.** Errors are the engine's limits (vertices, indices, materials, groups); the mesh must be simplified before it can be used. Faces are grouped by material and plane, so each group is planar and is culled and lit once. A shape with more than 16 distinct planes (a 32-sided cone has 33) gets one curved group per material instead, like the sphere, and is culled and lit per triangle. Fewer sides keep the cheaper path.
4. **Use it.** The output is `static const` vertex, index and group tables plus `void <c_name>_build(Mesh *mesh)`, which runs the builder sequence from [EXTENDING.md](EXTENDING.md#add-a-mesh) and ends with `mesh_finalize()`. Paste it after `#include "../render/mesh.h"`. Then build the mesh in `on_init` (after `scene_init()` has reset the geometry placement), add objects with it and a `scale` through `scene_add_object()` (the demo's `spawn_object()` is a helper private to `demo_scene.c`), and call `mesh_cleanup()` in `on_cleanup`. A textured material references `<C_NAME>_SLOT_<IMAGE>`, which does not compile until you `#define` it to the slot that loads `rom:/<image>.sprite` ([TEXTURES.md](TEXTURES.md)).
5. **Check it** as for any mesh: a shared shape gets its `count_bad_winding()` and planar-group checks in `tests/host/test_mesh.c`, and the demo HUD's `T:` shows the triangle cost. Then check it in ares and on the A3D.

## Making a texture or billboard sprite

Set up an orthographic camera looking at the subject, then let Claude render it straight into `assets/` with `execute_blender_code`:

```python
import bpy
scene = bpy.context.scene
r = scene.render
r.resolution_x, r.resolution_y, r.resolution_percentage = 32, 32, 100
r.film_transparent = True                       # transparent background: billboards cut it out
r.filter_size = 0.0                             # hard edges: RGBA16 keeps 1 bit of alpha
r.image_settings.file_format = "PNG"
r.image_settings.color_mode = "RGBA"
r.image_settings.color_depth = "8"
scene.view_settings.view_transform = "Standard" # colours as authored (AgX shifts them)
r.filepath = r"C:\path\to\n64-analogue3d-engine\assets\tree.png"
bpy.ops.render.render(write_still=True)
result = {"file": r.filepath}
```

- The next `libdragon make` converts it to `rom:/tree.sprite` (RGBA16, 5 bits per channel). Pick a slot and declare it on the scene: [EXTENDING.md](EXTENDING.md#add-a-texture).
- 32×32 is what the engine's quads assume (`BB_TEX_SIZE`, the cube's UVs). A 64×32 RGBA16 texture fills all 4 KB of TMEM, leaving no room for a second texture, a palette or mipmaps: keep 32×32.
- A texture for a mesh is multiplied by the lit base colour, so render flat colour without baked lighting.
- `render_viewport_to_path` works too, but writes to Blender's temp folder (see Tools): copy the file into `assets/` afterwards.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `claude` or `uvx` not found in PowerShell | The VS Code extension bundles `claude.exe` (use the one-liner above); `uvx` is in `~\.local\bin`, which a terminal opened before installing uv does not see yet |
| `Failed to connect … timed out after 30000ms` | The first start is still building: run `mcp get blender` again. If it keeps failing, check that the registration uses `--from git+…`, not `uvx blender-mcp`. |
| Connected, but no Blender tools in the session | Tools load when a session starts: reload the VS Code window or start a new conversation |
| Tool calls fail with a connection error | Blender is not running, the add-on is disabled, Allow Online Access is off, or its server is stopped (add-on preferences → Start MCP Bridge Server) |
| `*_for_cli` asks for `BLENDER_PATH` | Register with `-e BLENDER_PATH=…` (Quick start) |
| A PowerShell snippet fails with `Unexpected token '&'` | The line breaks were lost when pasting: join the commands with `;` |

## Source files

| File | Purpose |
|---|---|
| [tools/blender_mesh_export.py](../tools/blender_mesh_export.py) | Blender mesh → C builder tables for `Mesh` (runs inside Blender) |
| [src/render/mesh.h](../src/render/mesh.h) | `Mesh`, `Material`, `MeshVertex`, the builder API |
| Blender Lab MCP | https://www.blender.org/lab/mcp-server/ · source: https://projects.blender.org/lab/blender_mcp |
