# Mesh System

A general-purpose 3D mesh type that separates geometry definition from rendering. Any 3D object — cube, pillar, crate, character model — uses the same `Mesh` struct and `mesh_draw()` rendering path.

## Why

Before this system, adding a new 3D object meant writing ~180 lines of C with hardcoded vertex positions, face normals, UV coordinates, RDP mode setup, transform math, backface culling, and lighting calculation. The cube was the only renderable object, and its geometry was inseparable from its rendering code.

The Mesh abstraction separates **what** to draw (geometry + materials) from **how** to draw it (transforms + RDP rendering). This makes adding new objects trivial and is the foundation for every subsequent engine feature: multi-object scenes, model loading, and T3D RSP-accelerated rendering.

## Architecture

```
Mesh (geometry + materials)                     built with mesh_build.c
├── MeshVertex[]     — positions, normals, UVs in local space
├── uint16_t[]       — triangle index buffer
├── Material[]       — RDP rendering state (texture/color per group)
├── MeshFaceGroup[]  — groups of triangles sharing a material, with a
│                      precomputed centre, normal and planar flag
└── Bounding sphere  — for frustum culling

mesh_draw(mesh, model_matrix, camera, light)    mesh.c (hot path)
├── Frustum cull: mesh_world_bounds() + camera_sphere_visible()
├── Build MVP = VP * Model and the normal matrix
├── For each face group:
│   ├── Reset the RDP mode when material type or alpha cutout changes
│   ├── Planar group: back-face test + lighting once (skip the group if it faces away)
│   ├── Upload the texture (textured material, unless this draw already uploaded that slot)
│   └── For each triangle:
│       ├── Transform the 3 corners → screen space (reject near plane / guard band)
│       ├── Curved group: winding back-face test + lighting per triangle
│       └── rdpq_triangle()
└── Stats: draws, culled, groups, triangles
```

## Data Types

### Material

Captures the RDP rendering state for a group of faces.

```c
typedef enum {
    MATERIAL_TEXTURED,      // Texture * flat color (RDPQ_COMBINER_TEX_FLAT)
    MATERIAL_FLAT_COLOR,    // Flat color only (RDPQ_COMBINER_FLAT)
} MaterialType;

typedef struct {
    MaterialType type;
    int texture_slot;       // Index into texture system (-1 = no texture)
    uint8_t base_color[3];  // RGB base color (modulated by lighting)
    bool alpha_cutout;      // Discard pixels with alpha=0 (for sprites)
} Material;
```

| Field | Description |
|-------|-------------|
| `type` | Determines RDP combiner mode. `TEXTURED` samples a texture and multiplies by flat color. `FLAT_COLOR` uses only the flat color. |
| `texture_slot` | Which slot in the texture system to upload. -1 for untextured materials. |
| `base_color` | RGB color modulated by lighting. For textured materials, this tints the texture. For flat materials, this is the surface color. |
| `alpha_cutout` | Enables alpha compare so transparent texels are discarded (the billboard quad). |

### MeshVertex

Per-vertex data in local (model) space.

```c
typedef struct {
    float position[3];      // Local-space XYZ
    float normal[3];        // Local-space normal
    float uv[2];            // Texture coordinates (S, T)
} MeshVertex;
```

Normals are per-vertex (not per-face). For flat shading, all vertices of a face share the same normal — functionally equivalent to per-face normals. Per-vertex normals also support smooth shading (averaged normals) when needed in the future.

### MeshFaceGroup

A contiguous range of triangles that share the same material.

```c
typedef struct {
    int material_index;     // Index into Mesh.materials[]
    int index_start;        // First index in Mesh.indices[]
    int index_count;        // Number of indices (must be multiple of 3)

    // Filled by mesh_compute_bounds() (mesh_analyze_group)
    float center[3];        // Average vertex position (local space)
    float normal[3];        // Shared normal if planar, else normalised average
    bool  planar;           // One normal, one plane: cull/light the group once
} MeshFaceGroup;
```

Groups minimize RDP state changes. Within a group, the texture is uploaded at most once and all triangles are drawn with it. A **planar** group (every vertex shares one normal and lies on one plane: cube faces, pillar sides) is culled and lit once, so the primitive color is set once. A **curved** group (the sphere's latitude bands) is culled and lit per triangle. `mesh_compute_bounds()` decides which, so build each flat face as its own group if it should get the cheap path.

### Mesh

The core type.

```c
#define MESH_MAX_VERTICES    512
#define MESH_MAX_INDICES     1024
#define MESH_MAX_MATERIALS   8
#define MESH_MAX_GROUPS      16

typedef struct {
    MeshVertex *vertices;       // Vertex array (heap)
    uint16_t *indices;          // Index array, 3 per triangle (heap)
    int vertex_count;
    int index_count;
    int vertex_capacity;        // Allocated entries while building
    int index_capacity;
    void *block;                // After mesh_finalize(): the one allocation holding both arrays
    bool finalized;             // No more vertices or triangles can be added

    Material materials[MESH_MAX_MATERIALS];
    int material_count;
    MeshFaceGroup groups[MESH_MAX_GROUPS];
    int group_count;

    vec3_t bound_center;        // Local-space bounding sphere center
    float bound_radius;         // Bounding sphere radius

    bool backface_cull;         // Enable CPU backface culling (default: true)
} Mesh;
```

| Field | Description |
|-------|-------------|
| `vertices`, `indices` | While building: separate arrays that start at 16 vertices / 48 indices and double as needed. After `mesh_finalize()`: one exact-size block (vertices, then indices) at a fixed D-cache colour (see [Geometry placement](#geometry-placement-s91-d26)), inside the allocation owned by `block` (roadmap D8; every mesh used to reserve 512 vertices + 1024 indices, about 18 KB). |
| `finalized` | Set by `mesh_finalize()`; `mesh_add_vertex()` / `mesh_add_triangle()` then fail (assert in debug builds). |
| `materials[]` | Fixed-size array (max 8). Inline to avoid extra allocation. |
| `groups[]` | Fixed-size array (max 16). Each group references a material and a range of indices. |
| `bound_center/radius` | Computed by `mesh_finalize()` (through `mesh_compute_bounds()`). Used for frustum culling. |
| `backface_cull` | Default true. Disable for double-sided surfaces (e.g., foliage, thin walls). |

## API

### Lifecycle

```c
void mesh_init(Mesh *mesh);
void mesh_cleanup(Mesh *mesh);
```

`mesh_init` zeroes the struct and sets defaults (`backface_cull = true`). `mesh_cleanup` frees the geometry (the finalized block, or the build arrays) and resets the mesh so it can be built again.

### Building

Build a mesh by adding materials, then adding face groups with vertices and triangles. The builder lives in `mesh_build.c`, which has no rendering dependencies and is compiled into the host unit tests.

```c
int  mesh_add_material(Mesh *mesh, Material mat);
int  mesh_add_vertex(Mesh *mesh, MeshVertex vert);
void mesh_add_triangle(Mesh *mesh, uint16_t i0, uint16_t i1, uint16_t i2);
int  mesh_begin_group(Mesh *mesh, int material_index);
void mesh_end_group(Mesh *mesh);
void mesh_finalize(Mesh *mesh);         // last step: bounds + group analysis + exact-size geometry block
void mesh_compute_bounds(Mesh *mesh);   // bounding sphere + mesh_analyze_group() for every group
void mesh_analyze_group(const Mesh *mesh, MeshFaceGroup *group);
```

Helpers in `mesh.h`, also used by the projected shadows:

```c
void  mesh_world_bounds(const Mesh *mesh, const mat4_t *model, vec3_t *center, float *radius);
void  mesh_normal_matrix(const mat4_t *model, float cof[3][3]);   // cofactor matrix for normals
float mesh_screen_area2(const float a[2], const float b[2], const float c[2]);  // < 0: front face
```

**Builder pattern:**

```
mesh_init(&mesh)
    ↓
mesh_add_material(...)           // Add all materials first
    ↓
mesh_begin_group(material_idx)   // Start a face group
mesh_add_vertex(...)             // Add vertices (returns index)
mesh_add_triangle(i0, i1, i2)   // Add triangles (auto-tracked by group)
mesh_end_group()                 // Close the group
    ↓ (repeat for each group)
mesh_finalize(&mesh)             // Bounds, group analysis, exact-size geometry block
```

| Function | Returns | Notes |
|----------|---------|-------|
| `mesh_add_material` | Material index (0-7) or -1 | Must be added before groups reference it |
| `mesh_add_vertex` | Vertex index (0-511) or -1 | Grows the build array as needed; -1 after `mesh_finalize()` |
| `mesh_add_triangle` | void | Adds 3 indices; auto-updates current group's count |
| `mesh_begin_group` | Group index (0-15) or -1 | Sets index_start to current index count |
| `mesh_end_group` | void | No-op (group count tracked automatically) |
| `mesh_finalize` | void | Calls `mesh_compute_bounds()`, then packs vertices and indices into one exact-size block at a fixed D-cache colour. Every builder ends with it |
| `mesh_placement_reset` | void | Restarts the geometry placement at the window's first colour; `scene_init()` calls it |
| `mesh_compute_bounds` | void | Computes centroid + max-distance bounding sphere and analyses every group |

### Rendering

```c
void mesh_draw(const Mesh *mesh, const mat4_t *model,
               const Camera *cam, const LightConfig *light);
```

Draws the entire mesh with the given model matrix. Handles:

1. **Frustum culling** — `mesh_world_bounds()` transforms the bounding sphere to world space (including scale), `camera_sphere_visible()` tests it against the camera frustum. Entire mesh skipped if off-screen.
2. **MVP computation** — `MVP = VP * Model`, plus the normal matrix (`mesh_normal_matrix()`)
3. **RDP mode setup** — Only resets RDP mode (`rdpq_set_mode_standard()`) when the material **type** or its **alpha cutout** changes between groups, so a material without cutout never inherits alpha compare (D5). Texture uploads happen per group, after the cull, and are skipped when the previous upload of this draw (since the last mode reset) was the same slot.
4. **Per-group processing** (flat shading). `mesh_compute_bounds()` also analyses every group (`mesh_analyze_group()` in `mesh_build.c`): its centre, its normal, and whether it is **planar** (every vertex shares one normal and lies on one plane).
   - **Planar groups** (cube faces, pillar sides): one exact back-face test, whether the camera is in front of the group's own plane, then lighting once and `rdpq_set_prim_color()` once.
   - **Curved groups** (sphere bands wrap around the mesh, so no single normal describes them): culled per triangle by the sign of the projected screen area (`mesh_screen_area2()`), and lit per triangle with the average of its vertex normals.
   - Normals reach world space through the cofactor matrix of the model matrix, so non-uniform scale keeps them perpendicular to their faces.
   - Before S2 every group was culled and lit with its first vertex's normal against the object-centre direction; spheres vanished when seen from their −Z side (D3, D24).
5. **Per-triangle processing:**
   - Vertex transform (MVP → perspective divide → NDC → screen coordinates); a triangle with a corner behind the near plane or outside the guard band is dropped
   - `rdpq_triangle()` with Z-buffer, in the format that matches the combiner: `TRIFMT_ZBUF_TEX` (textured) or `TRIFMT_ZBUF` (flat), and `TRIFMT_ZBUF_SHADE_TEX` / `TRIFMT_ZBUF_SHADE` when fog is on

## Usage Example: Cube

The cube uses 6 materials (one per face, each with a different texture and base color) and 6 face groups. `cube_init()` builds the mesh; `cube_get_mesh()` returns a pointer for use by SceneObject callbacks. Rendering is handled by the generic `object_draw` callback in demo_scene.c, which calls `mesh_draw()` with the object's model matrix.

```c
static Mesh cube_mesh;

void cube_init(void) {
    mesh_init(&cube_mesh);
    cube_mesh.backface_cull = true;

    for (int f = 0; f < 6; f++) {
        mesh_add_material(&cube_mesh, (Material){
            .type = MATERIAL_TEXTURED,
            .texture_slot = face_tex_slot[f],
            .base_color = {face_colors[f][0], face_colors[f][1], face_colors[f][2]}
        });
    }

    for (int f = 0; f < 6; f++) {
        mesh_begin_group(&cube_mesh, f);
        int base = cube_mesh.vertex_count;
        for (int v = 0; v < 4; v++) {
            mesh_add_vertex(&cube_mesh, (MeshVertex){...});
        }
        mesh_add_triangle(&cube_mesh, base+0, base+1, base+2);
        mesh_add_triangle(&cube_mesh, base+0, base+2, base+3);
        mesh_end_group(&cube_mesh);
    }
    mesh_finalize(&cube_mesh);
}

const Mesh *cube_get_mesh(void) { return &cube_mesh; }
void cube_cleanup(void) { mesh_cleanup(&cube_mesh); }
```

## Shape Library (mesh_defs)

Factory functions for reusable mesh primitives. Each shape is a static `Mesh` in local space, centered at origin, unit scale; `mesh_defs_init()` builds all four (a second call does nothing until `mesh_defs_cleanup()` frees them). The SceneObject's transform handles position/rotation/scale.

```c
void mesh_defs_init(void);              // Build all shapes
void mesh_defs_cleanup(void);           // Free all mesh geometry
const Mesh *mesh_defs_get_pillar(void);
const Mesh *mesh_defs_get_platform(void);
const Mesh *mesh_defs_get_pyramid(void);
const Mesh *mesh_defs_get_sphere(void);
```

| Shape | Geometry | Triangles | Material | Color |
|-------|----------|-----------|----------|-------|
| Pillar | 8-sided cylinder, height [-1,1], radius 1.0 | 32 | `MATERIAL_FLAT_COLOR` | Stone gray (180, 160, 140) |
| Platform | Box 4.0 x 0.5 x 2.0 | 12 | `MATERIAL_FLAT_COLOR` | Dark wood (140, 100, 60) |
| Pyramid | 4-sided pyramid, base [-1,1] XZ, apex Y=1 | 6 | `MATERIAL_FLAT_COLOR` | Sand gold (200, 180, 100) |
| Sphere | UV sphere, 6 latitude × 6 longitude segments, radius 1 | 60 | `MATERIAL_FLAT_COLOR` | Red (200, 60, 60) |

The flat shapes use one planar face group per face for proper per-face lighting normals (e.g., pillar has 10 groups: 8 sides + top cap + bottom cap). The sphere has one curved group per latitude band (6 groups), so it takes the per-triangle path; it is the demo's static sphere and the physics ball.

Adding a shape or any other mesh: [EXTENDING.md](EXTENDING.md).

## SceneObject Integration

Objects are managed through the scene system via generic callbacks. An `ObjectData` struct stored in `SceneObject.data` holds the mesh reference and behavior flags:

```c
typedef struct {
    const Mesh *mesh;           // Shared mesh reference (NOT owned)
    const char *name;           // Display name for HUD
    bool auto_rotate;
    float rotate_speed_x, rotate_speed_y;
} ObjectData;

// Generic draw callback (used by all mesh objects):
static void object_draw(SceneObject *obj, const Camera *cam, const LightConfig *light) {
    ObjectData *data = (ObjectData *)obj->data;
    mat4_t model;
    mat4_from_srt(&model, &obj->scale, obj->rotation.x, obj->rotation.y,
                  obj->rotation.z, &obj->position);
    mesh_draw(data->mesh, &model, cam, light);
}
```

## N64 Constraints

| Constraint | Value | Impact |
|-----------|-------|--------|
| Max vertices per mesh | 512 | ~170 triangles. N64 games typically use 50-500 triangles per object. |
| Max indices per mesh | 1024 | Up to 341 triangles per mesh. |
| Max materials per mesh | 8 | One material = one RDP mode + one texture. |
| Max groups per mesh | 16 | Grouping triangles by material minimizes RDP state changes. |
| TMEM | 4 KB | Only one texture tile loaded at a time. Groups upload their texture once. |
| Vertex memory | 32 B per vertex, 2 B per index | Exact size after `mesh_finalize()` (the pillar: 50 vertices + 96 indices = 1.8 KB), plus up to ~4 KB of alignment in front of it (below). The `Mesh` struct's own place in the 8 KB D-cache still matters: on the render stack's lines it costs up to ~8 % CPU (D26; measure with Bench = Layout). |

### Geometry placement (S9.1, D26)

The VR4300's data cache is 8 KB and direct-mapped, so a mesh's vertex data evicts whatever else sits at the same address modulo 8 KB (its *colour*). Heap placement used to decide that: in S9, 4 KB of new static data moved the heap and put the pillar's vertices on the render stack's lines, and every mesh step lost 8–11 %. `mesh_finalize()` therefore allocates each geometry block 8 KB-aligned and places the data at an offset inside a colour window, `ENGINE_GEOMETRY_COLOUR_LO`–`_HI` (0x0520–0x0F1F, [src/engine/hot.h](../src/engine/hot.h)). The window is clear of the render stack (≈0x1700–0x1E10) and of the pinned data the mesh phase reads (0x0000–0x0517).

- Blocks are packed one after another through the window and wrap to its start. A block larger than the window starts at `LO`: up to 4.5 KB it ends below `ENGINE_GEOMETRY_COLOUR_MAX` (0x1700, where the stack's colours begin); a bigger mesh reaches them.
- `mesh_placement_reset()` restarts the packing. `scene_init()` calls it, so a scene's meshes take the same colours after every boot and reset.
- `tools/hot_data.py` (CI) fails if the window shares a line with the mesh or shadow phase's stack or with pinned data they read.
- Cost: the bytes in front of each block's colour are unused, 1.3–3.8 KB per mesh (+22 KB of heap in the benchmark scene).
- Not placed: the `Mesh` struct itself (face groups and materials, read per group) lives wherever its owner puts it. A `LAYOUT_PAD=448` build still moves mesh steps by 1–2.5 % (BENCHMARKS.md, S9.1); the P3.1 vertex cache replaces per-mesh reads in the triangle loop.

## Performance Notes & Optimization Lessons

- **Frustum culling**: Entire mesh rejected with one bounding sphere test (6 plane dot products).
- **Backface culling**: once per planar group (skips ~50 % of groups on convex objects); per triangle for curved groups.
- **Winding**: front faces are counter-clockwise seen from outside the mesh. The per-triangle cull and the projected shadows depend on it; `tests/host/test_mesh.c` checks the `mesh_defs` shapes (winding, planar groups, the sphere visible from every side).
- **Code placement**: `mesh_draw` and its per-triangle callees are `ENGINE_HOT` (linked in the hot-text block). A triangle loop that collides with `rdpq_triangle_rsp` in the direct-mapped I-cache costs ~1,100 cycles per triangle (HARDWARE.md, D25).
- **RDP mode batching**: `rdpq_set_mode_standard()` is expensive — it resets the entire RDP pipeline. Only called when the material **type** or **alpha cutout** changes between groups, not per-group. For a mesh where all groups share the same type (e.g., all textured), mode is set exactly once.
- **Per-group lighting**: Normal transform + `lighting_calculate()` computed once per planar group. An early version computed these per-triangle, which doubled the lighting work for no visual difference in flat shading; only curved groups still light per triangle.
- **Bounding sphere scale**: Uses squared column lengths with a single `sqrtf` at the end, rather than 3 separate `sqrtf` calls. `sqrtf` is expensive on the N64's MIPS FPU.

### Optimization History

The initial mesh_draw implementation caused a significant frame rate regression (sub-30 FPS) on hardware despite rendering the same 12-triangle cube. Root causes:

| Issue | Cost | Fix |
|-------|------|-----|
| `rdpq_set_mode_standard()` called 6x (per group) instead of 1x | Each call resets entire RDP pipeline | Only reset when material type changes |
| Normal transform + lighting per-triangle (12x) instead of per-face (6x) | Doubled `lighting_calculate()` + `sqrtf` calls | Compute once per group |
| 3x `sqrtf` for bounding sphere scale extraction | Expensive FPU ops | Compare squared lengths, one `sqrtf` at end |

**Lesson**: On the N64, minimizing RDP state changes is as important as minimizing triangle count. A single `rdpq_set_mode_standard()` call can cost more than several `rdpq_triangle()` calls.

## Future: T3D Migration

When Milestone 1 (T3D RSP-accelerated rendering) arrives, the Mesh struct gains a `void *t3d_model` field. `mesh_draw()` checks this pointer:

- `t3d_model != NULL` → Dispatch to T3D RSP rendering (transforms on vector processor)
- `t3d_model == NULL` → Use current CPU rendering path (educational reference / fallback)

This means the CPU renderer built here becomes the reference implementation while T3D handles production rendering. No changes to calling code — `mesh_draw()` remains the single entry point.

## Source Files

| File | Purpose |
|------|---------|
| [src/render/mesh.h](../src/render/mesh.h) | Mesh, Material, MeshVertex, MeshFaceGroup structs, API, inline helpers |
| [src/render/mesh_build.c](../src/render/mesh_build.c) | Builder functions, bounds and group analysis (host-testable) |
| [src/render/mesh.c](../src/render/mesh.c) | `mesh_draw()` (hot path) |
| [src/render/mesh_defs.h](../src/render/mesh_defs.h) | Shape library API (pillar, platform, pyramid, sphere) |
| [src/render/mesh_defs.c](../src/render/mesh_defs.c) | Geometry generators for each shape |
| [src/render/cube.c](../src/render/cube.c) | Cube geometry (textured, built on Mesh) |
| [tests/host/test_mesh.c](../tests/host/test_mesh.c) | Winding and planarity tests for the shape library |
