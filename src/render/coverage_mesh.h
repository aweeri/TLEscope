#pragma once

#include "../core/types.h"

/* LOD tessellation: HIGH 12x60, MEDIUM 8x40, LOW 4x20 */
typedef enum {
    COVERAGE_LOD_HIGH = 0,
    COVERAGE_LOD_MEDIUM = 1,
    COVERAGE_LOD_LOW = 2,
    COVERAGE_LOD_COUNT = 3
} CoverageMeshLOD;

/** Cached mesh/model for a specific altitude (rounded to 10 km) and LOD. */
typedef struct {
    Mesh mesh;
    Model model;
    float cached_altitude_km;
    bool is_valid;
} CoverageMeshCache;

/* generate a coverage cone mesh in a local Earth-centred frame (+Y toward the satellite) */
Mesh GenerateCoverageMesh(float altitude_km, CoverageMeshLOD lod);

/* get or create a cached coverage mesh for the given altitude and LOD */
Model* GetCachedCoverageMesh(float altitude_km, CoverageMeshLOD lod, Shader shader);

/** Free all cached coverage meshes (GPU and CPU). */
void ClearCoverageMeshCache();

/* compute a satellite's horizon angle (radians) and surface coverage radius (km) */
void CalculateCoverageParams(const Satellite *sat, float *out_theta, float *out_radius);

/* pick an LOD from the camera distance to the sub-satellite point */
CoverageMeshLOD SelectCoverageLOD(const Satellite *sat, Camera camera);

/** Simple dot-product frustum cull for a satellite's coverage. */
bool IsCoverageVisible(const Satellite *sat, Camera camera);

/** Number of concentric rings for an LOD level. */
int GetLODRings(CoverageMeshLOD lod);

/** Number of segments per ring for an LOD level. */
int GetLODSegments(CoverageMeshLOD lod);

/* CPU-side cached cap geometry mirroring the GPU mesh, so the 2D map path skips the trigonometry */
typedef struct {
    int rings;              /* number of concentric rings */
    int segments;           /* number of segments per ring */
    const float *vertices;  /* (rings + 1) * segments * 3 floats, draw units */
} CoverageCapData;

/* get or create the cached CPU cap geometry; pointer valid until ClearCoverageMeshCache() */
const CoverageCapData *GetCachedCoverageCap(float altitude_km, CoverageMeshLOD lod);

/* pick an LOD for the 2D map from the on-screen cap size (angular radius and zoom) */
CoverageMeshLOD SelectCoverageLOD2D(const Satellite *sat, float zoom, float map_w);
