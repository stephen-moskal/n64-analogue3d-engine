# Blender sources

`.blend` files that author the engine's meshes and textures ([docs/BLENDER_MCP.md](../../docs/BLENDER_MCP.md)). Nothing in this folder is built into the ROM:

- meshes are exported to C with `tools/blender_mesh_export.py`, or, from Phase 3 S4, to `assets/meshes/*.json`, which the build turns into ROM files;
- textures and sprites are rendered to top-level `assets/*.png`.

`.blend` files are committed as binary (`.gitattributes`). Blender's `*.blend1` backups are git-ignored.

Phase 3 S4 adds `engine_template.blend`, set up with the engine's conventions:
- authoring in Blender's Z-up, which the exporter maps to the engine's Y-up;
- unit-size models, sized in the engine by the object's scale;
- a 60° camera with the engine's near and far planes (20, 2000);
- a sun matching the default lighting.
