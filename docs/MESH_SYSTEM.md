# Mesh System

A general-purpose 3D mesh type that separates geometry definition from rendering. Any 3D object — cube, pillar, crate, character model — uses the same `Mesh` struct and `mesh_draw()` rendering path.

## Why

Before this system, adding a new 3D object meant writing ~180 lines of C with hardcoded vertex positions, face normals, UV coordinates, RDP mode setup, transform math, backface culling, and lighting calculation. The cube was the only renderable object, and its geometry was inseparable from its rendering code.

The Mesh abstraction separates **what** to draw (geometry + materials) from **how** to draw it (transforms + RDP rendering). This makes adding new objects trivial and is the foundation for every subsequent engine feature: multi-object scenes, model loading, and T3D RSP-accelerated rendering.

## Architecture

```
Mesh (geometry + materials)
├── MeshVertex[]     — positions, normals, UVs in local space
├── uint16_t[]       — triangle index buffer
├── Material[]       — RDP rendering state (texture/color per group)
├── MeshFaceGroup[]  — groups of triangles sharing a material
└── Bounding sphere  — for frustum culling

mesh_draw(mesh, model_matrix, camera, light)
├── Frustum culling (bounding sphere vs camera frustum)
├── Build MVP = VP * Model
├── For each face group:
│   ├── Set RDP mode from material
│   ├── Upload texture (if textured material)
│   └── For each triangle:
│       ├── Transform normal → world space
│       ├── Backface cull
│       ├── Compute lighting
│       ├── Transform vertices → screen space
│       └── rdpq_triangle()
└── Update texture stats
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
} Material;
```

| Field | Description |
|-------|-------------|
| `type` | Determines RDP combiner mode. `TEXTURED` samples a texture and multiplies by flat color. `FLAT_COLOR` uses only the flat color. |
| `texture_slot` | Which slot in the texture system to upload. -1 for untextured materials. |
| `base_color` | RGB color modulated by lighting. For textured materials, this tints the texture. For flat materials, this is the surface color. |

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
} MeshFaceGroup;
```

Groups minimize RDP state changes. Within a group, the RDP mode and texture are set once, then all triangles are drawn. Only the primitive color changes per-triangle (due to per-face lighting).

### Mesh

The core type.

```c
#define MESH_MAX_VERTICES    512
#define MESH_MAX_INDICES     1024
#define MESH_MAX_MATERIALS   8
#define MESH_MAX_GROUPS      16

typedef struct {
    MeshVertex *vertices;       // Heap-allocated vertex array
    uint16_t *indices;          // Heap-allocated index array (triangles)
    int vertex_count;
    int index_count;

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
| `vertices` | Heap-allocated. Lazy-allocated on first `mesh_add_vertex()`. |
| `indices` | Heap-allocated. Lazy-allocated on first `mesh_add_triangle()`. |
| `materials[]` | Fixed-size array (max 8). Inline to avoid extra allocation. |
| `groups[]` | Fixed-size array (max 16). Each group references a material and a range of indices. |
| `bound_center/radius` | Computed by `mesh_compute_bounds()`. Used for frustum culling. |
| `backface_cull` | Default true. Disable for double-sided surfaces (e.g., foliage, thin walls). |

## API

### Lifecycle

```c
void mesh_init(Mesh *mesh);
void mesh_cleanup(Mesh *mesh);
```

`mesh_init` zeroes the struct and sets defaults. `mesh_cleanup` frees the heap-allocated vertex and index arrays.

### Building

Build a mesh by adding materials, then adding face groups with vertices and triangles.

```c
int  mesh_add_material(Mesh *mesh, Material mat);
int  mesh_add_vertex(Mesh *mesh, MeshVertex vert);
void mesh_add_triangle(Mesh *mesh, uint16_t i0, uint16_t i1, uint16_t i2);
int  mesh_begin_group(Mesh *mesh, int material_index);
void mesh_end_group(Mesh *mesh);
void mesh_compute_bounds(Mesh *mesh);
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
mesh_compute_bounds(&mesh)       // Compute bounding sphere
```

| Function | Returns | Notes |
|----------|---------|-------|
| `mesh_add_material` | Material index (0-7) or -1 | Must be added before groups reference it |
| `mesh_add_vertex` | Vertex index (0-511) or -1 | Lazy-allocates vertex array on first call |
| `mesh_add_triangle` | void | Adds 3 indices; auto-updates current group's count |
| `mesh_begin_group` | Group index (0-15) or -1 | Sets index_start to current index count |
| `mesh_end_group` | void | No-op (group count tracked automatically) |
| `mesh_compute_bounds` | void | Computes centroid + max-distance bounding sphere |

### Rendering

```c
void mesh_draw(const Mesh *mesh, const mat4_t *model,
               const Camera *cam, const LightConfig *light);
```

Draws the entire mesh with the given model matrix. Handles:

1. **Frustum culling** — Transforms bounding sphere to world space (including scale), tests against camera frustum. Entire mesh skipped if off-screen.
2. **MVP computation** — `MVP = VP * Model`
3. **RDP mode setup** — Only resets RDP mode (`rdpq_set_mode_standard()`) when the material **type** changes between groups. Texture uploads happen per-group.
4. **Per-group processing** (flat shading):
   - Normal transform + lighting computed **once per group** (all triangles in a flat-shaded group share the same normal)
   - Backface cull skips entire groups, not individual triangles
   - `rdpq_set_prim_color()` set once per group
5. **Per-triangle processing:**
   - Vertex transform (MVP → perspective divide → NDC → screen coordinates)
   - `rdpq_triangle(&TRIFMT_ZBUF_TEX, ...)` with Z-buffer

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
    mesh_compute_bounds(&cube_mesh);
}

const Mesh *cube_get_mesh(void) { return &cube_mesh; }
void cube_cleanup(void) { mesh_cleanup(&cube_mesh); }
```

## Shape Library (mesh_defs)

Factory functions for reusable mesh primitives. Each shape is a lazy-initialized static `Mesh` in local space, centered at origin, unit scale. The SceneObject's transform handles position/rotation/scale.

```c
void mesh_defs_init(void);              // Build all shapes
void mesh_defs_cleanup(void);           // Free all mesh geometry
const Mesh *mesh_defs_get_pillar(void);
const Mesh *mesh_defs_get_platform(void);
const Mesh *mesh_defs_get_pyramid(void);
```

| Shape | Geometry | Triangles | Material | Color |
|-------|----------|-----------|----------|-------|
| Pillar | 8-sided cylinder, height [-1,1], radius 1.0 | 32 | `MATERIAL_FLAT_COLOR` | Stone gray (180, 160, 140) |
| Platform | Box 4.0 x 0.5 x 2.0 | 12 | `MATERIAL_FLAT_COLOR` | Dark wood (140, 100, 60) |
| Pyramid | 4-sided pyramid, base [-1,1] XZ, apex Y=1 | 6 | `MATERIAL_FLAT_COLOR` | Sand gold (200, 180, 100) |

Each shape uses multiple face groups for proper per-face lighting normals (e.g., pillar has 10 groups: 8 sides + top cap + bottom cap).

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
| Vertex memory | ~16 KB for 512 verts | MeshVertex is 32 bytes. Heap-allocated in RDRAM. |

## Performance Notes & Optimization Lessons

- **Frustum culling**: Entire mesh rejected with one bounding sphere test (6 plane dot products).
- **Backface culling**: Per-group (not per-triangle). Skips ~50% of groups on convex objects.
- **RDP mode batching**: `rdpq_set_mode_standard()` is expensive — it resets the entire RDP pipeline. Only called when the material **type** changes between groups, not per-group. For a mesh where all groups share the same type (e.g., all textured), mode is set exactly once.
- **Per-group lighting**: Normal transform + `lighting_calculate()` computed once per group. An early version computed these per-triangle, which doubled the lighting work for no visual difference in flat shading.
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
| [src/render/mesh.h](../src/render/mesh.h) | Mesh, Material, MeshVertex, MeshFaceGroup structs and API |
| [src/render/mesh.c](../src/render/mesh.c) | Builder functions, bounding volume, rendering |
| [src/render/mesh_defs.h](../src/render/mesh_defs.h) | Shape library API (pillar, platform, pyramid) |
| [src/render/mesh_defs.c](../src/render/mesh_defs.c) | Geometry generators for each shape |
| [src/render/cube.c](../src/render/cube.c) | Cube geometry (textured, built on Mesh) |
