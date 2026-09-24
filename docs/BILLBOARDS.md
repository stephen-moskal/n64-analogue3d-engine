# Billboards

Camera-facing textured quads for trees, markers and other flat props. A billboard is an ordinary `SceneObject` whose draw callback is `billboard_draw()`; every billboard shares one quad mesh and is drawn through `mesh_draw()`, so it gets frustum culling, lighting, fog and the Z-buffer like any other mesh. Particles don't use this code: they have their own renderer ([PARTICLES.md](PARTICLES.md)).

## Modes

| Mode | Orientation | Typical use |
|---|---|---|
| `BILLBOARD_SPHERICAL` | faces the camera completely, turning in yaw and pitch | markers, pickups, anything seen from any angle |
| `BILLBOARD_CYLINDRICAL` | turns about the world Y axis only, so it stays upright | trees, NPC sprites |

## API (`billboard.h`)

```c
typedef enum {
    BILLBOARD_SPHERICAL,    // Fully faces camera (particles, markers)
    BILLBOARD_CYLINDRICAL,  // Only rotates around Y axis (trees, NPCs)
} BillboardMode;

typedef struct {
    int texture_slot;       // Index into texture system
    BillboardMode mode;
    float width, height;    // World-space dimensions
    uint8_t color[3];       // Tint / base color (modulated by lighting)
} BillboardData;

void billboard_init(void);      // builds the shared quad mesh (does nothing if already built)
void billboard_cleanup(void);   // frees it
void billboard_draw(SceneObject *obj, const Camera *cam, const LightConfig *light);  // SceneObject.on_draw
```

## Adding billboards to a scene

1. Load the texture into a slot, preferably declared in `Scene.texture_paths` ([EXTENDING.md](EXTENDING.md#add-a-texture)).
2. Call `billboard_init()` in `on_init`.
3. Add a `SceneObject` with `on_draw = billboard_draw` and `data` pointing at a `BillboardData`. The object keeps the pointer, so the `BillboardData` needs storage that lives as long as the object.
4. Call `billboard_cleanup()` in `on_cleanup`.

From `spawn_billboard()` in `demo_scene.c`, which takes the `BillboardData` from a static pool:

```c
BillboardData *data = alloc_billboard_data();
if (!data) return -1;

data->texture_slot = tex_slot;
data->mode = mode;
data->width = width;
data->height = height;
data->color[0] = r;
data->color[1] = g;
data->color[2] = b;

SceneObject obj = {
    .position = pos,
    .rotation = {0, 0, 0},
    .scale = {1, 1, 1},
    .active = true,
    .visible = true,
    .data = data,
    .on_update = NULL,
    .on_draw = billboard_draw,
};

return scene_add_object(scene, &obj);
```

`width` and `height` are in world units, and the quad is centred on `obj->position`: to stand a billboard on the ground, put its centre half its height above the floor (the demo's 160-unit tree at y = −20 stands on `FLOOR_Y` = −100). `SceneObject.rotation` and `.scale` are ignored.

## How they are drawn

1. **Shared quad.** `billboard_init()` builds one `Mesh`: a unit quad (−0.5 to 0.5) in the XY plane facing +Z, four vertices and two triangles, one `MATERIAL_TEXTURED` material with `alpha_cutout = true`, and `backface_cull = false`. Its UVs are texel coordinates for a 32×32 sprite (`BB_TEX_SIZE`).
2. **Orientation.** `billboard_draw()` builds a model matrix whose columns are `[right × width, up × height, forward, position]`:
   - spherical: `forward` points at the camera, `right = normalize(world_up × forward)`, `up = forward × right`;
   - cylindrical: `forward` is the direction to the camera flattened onto the XZ plane, `up` is the world Y axis, `right = up × forward`;
   - fallbacks: `forward` = +Z when the camera sits exactly at the billboard (spherical) or straight above or below it (cylindrical), and `right` = +X when the camera is straight above or below a spherical billboard.
3. **Per-instance state.** It writes the instance's `texture_slot` and `color` into the shared mesh's material, then calls `mesh_draw()`:

   ```c
   // Override shared mesh material for this instance
   bb_mesh.materials[0].texture_slot = data->texture_slot;
   bb_mesh.materials[0].base_color[0] = data->color[0];
   bb_mesh.materials[0].base_color[1] = data->color[1];
   bb_mesh.materials[0].base_color[2] = data->color[2];

   mesh_draw(&bb_mesh, &model, cam, light);
   ```
4. **`mesh_draw()`.** The quad is frustum-culled with its bounding sphere, and it is a single planar face group, so it is lit once, with its normal pointing at the camera, and never back-face culled. The RDP mode is set (textured, alpha compare on), the texture is uploaded to TMEM, and two triangles are sent: `TRIFMT_ZBUF_TEX`, or `TRIFMT_ZBUF_SHADE_TEX` when fog is on, with Z read and write.

**Alpha cutout.** RGBA16 sprites keep one bit of alpha, and the material's alpha compare discards the transparent texels. Nothing is blended, so billboards write Z like opaque geometry and need no sorting.

**Cost.** Each billboard is a separate `mesh_draw()` call: a render-mode set, a 2 KB texture upload, one lighting calculation and two triangles. There is no billboard counter: billboards show up in `mesh_draws`, `groups_drawn`, `tris_mesh` and `tex_uploads`, and in the profiler under `objects` and its `mesh_*` slots ([PROFILING.md](PROFILING.md)).

## In the demo

| Billboard | Mode | Texture slot | Size (w × h) | Position |
|---|---|---|---|---|
| marker | spherical | 6, `marker.sprite` (`TEX_BILLBOARD_MARKER`) | 50 × 50 | (0, 120, 350), floating above the pyramid |
| tree | cylindrical | 7, `tree.sprite` (`TEX_BILLBOARD_TREE`) | 120 × 160 | (350, −20, −150) |
| tree | cylindrical | 7 | 100 × 130 | (−350, −30, 200) |

All three use a white tint. Both textures are declared in the demo's `Scene.texture_paths`, so `scene_init()` and `scene_cleanup()` load and free them. The billboards carry neither `SCENE_OBJ_SELECTABLE` nor `SCENE_OBJ_CASTS_SHADOW`, so they can't be selected and cast no shadows ([SCENE_SYSTEM.md](SCENE_SYSTEM.md), "Object Flags").

## Limits

- UVs are fixed for 32×32 textures (`BB_TEX_SIZE`, shared by every billboard); there's no per-instance UV rectangle or atlas (an atlas for billboards and particles is planned with P3.3).
- One-bit cutout only: no translucent billboards.
- Lit like a flat face turned toward the camera, so the brightness changes as the camera moves relative to the sun; point lights affect it too.
- No shadows and no animation (animated billboards are part of Feature 8, ROADMAP_v2 §7.2).
- Every billboard costs a mode set and a texture upload per frame; nothing is batched.
- The shared quad is finalized to its exact size: four vertices and six indices (208 B).

## Source files

| File | Purpose |
|---|---|
| [src/render/billboard.h](../src/render/billboard.h) | `BillboardMode`, `BillboardData`, API |
| [src/render/billboard.c](../src/render/billboard.c) | shared quad mesh, orientation matrix, draw callback |
| [src/scenes/demo_scene.c](../src/scenes/demo_scene.c) | `spawn_billboard()`, billboard texture slots, the three demo billboards |
