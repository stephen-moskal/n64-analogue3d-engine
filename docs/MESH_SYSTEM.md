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
3. **Per-group RDP setup** — Sets combiner mode and uploads texture once per group
4. **Per-triangle processing:**
   - Normal transform (model matrix upper 3x3 + re-normalize for scale)
   - Backface culling (dot product with camera direction)
   - Blinn-Phong lighting via `lighting_calculate()`
   - Vertex transform (MVP → perspective divide → NDC → screen coordinates)
   - `rdpq_triangle(&TRIFMT_ZBUF_TEX, ...)` with Z-buffer

## Usage Example: Cube

The existing cube uses 6 materials (one per face, each with a different texture and base color) and 6 face groups (one quad = 2 triangles each).

```c
static Mesh cube_mesh;
static mat4_t model_matrix;

void cube_init(void) {
    mesh_init(&cube_mesh);
    cube_mesh.backface_cull = true;

    // 6 materials — one per face
    for (int f = 0; f < 6; f++) {
        mesh_add_material(&cube_mesh, (Material){
            .type = MATERIAL_TEXTURED,
            .texture_slot = face_tex_slot[f],
            .base_color = {face_colors[f][0], face_colors[f][1], face_colors[f][2]}
        });
    }

    // 6 face groups — each a quad (4 verts, 2 triangles)
    for (int f = 0; f < 6; f++) {
        mesh_begin_group(&cube_mesh, f);
        int base = cube_mesh.vertex_count;
        for (int v = 0; v < 4; v++) {
            mesh_add_vertex(&cube_mesh, (MeshVertex){
                .position = {face_verts[f][v][0], face_verts[f][v][1], face_verts[f][v][2]},
                .normal   = {face_normals[f][0], face_normals[f][1], face_normals[f][2]},
                .uv       = {face_uvs[v][0], face_uvs[v][1]}
            });
        }
        mesh_add_triangle(&cube_mesh, base+0, base+1, base+2);
        mesh_add_triangle(&cube_mesh, base+0, base+2, base+3);
        mesh_end_group(&cube_mesh);
    }

    mesh_compute_bounds(&cube_mesh);
}

void cube_draw(const Camera *cam, const LightConfig *light) {
    mesh_draw(&cube_mesh, &model_matrix, cam, light);
}

void cube_cleanup(void) {
    mesh_cleanup(&cube_mesh);
}
```

## Usage Example: Simple Flat-Color Object

A flat-colored object (no texture) with a single material:

```c
static Mesh platform_mesh;

void platform_init(void) {
    mesh_init(&platform_mesh);
    platform_mesh.backface_cull = false;  // Visible from both sides

    mesh_add_material(&platform_mesh, (Material){
        .type = MATERIAL_FLAT_COLOR,
        .texture_slot = -1,
        .base_color = {180, 160, 140}
    });

    mesh_begin_group(&platform_mesh, 0);
    // Add vertices and triangles...
    mesh_end_group(&platform_mesh);

    mesh_compute_bounds(&platform_mesh);
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

## Performance Notes

- **Frustum culling**: Entire mesh rejected with one bounding sphere test (6 plane dot products). This is the same optimization as before.
- **Backface culling**: Per-triangle dot product. Skips ~50% of triangles on convex objects.
- **RDP state batching**: Mode set once per group, not per triangle. For a single-material mesh, the RDP mode is set exactly once.
- **Bounding sphere scaling**: The draw function extracts column lengths from the model matrix to compute the world-space bounding radius. This handles non-uniform scale correctly.

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
| [src/render/cube.c](../src/render/cube.c) | Cube geometry built using Mesh (reference usage) |
