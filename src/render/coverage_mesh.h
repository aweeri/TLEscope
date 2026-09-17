/**
 * @file coverage_mesh.h
 * @brief Coverage mesh generation, caching, LOD and culling.
 */
#pragma once

#include "../core/types.h"

/**
 * @brief Level of Detail for coverage mesh tessellation.
 *
 * HIGH: 12 rings x 60 segments, MEDIUM: 8 x 40, LOW: 4 x 20.
 */
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

/**
 * @brief Generate a coverage cone mesh for the given altitude and LOD.
 *
 * The cap is built in a local Earth-centred frame (+Y toward the satellite)
 * that the vertex shader rotates per satellite.
 *
 * @param altitude_km Satellite altitude above Earth surface (km)
 * @param lod Level of detail
 * @return Generated mesh ready for GPU upload
 */
Mesh GenerateCoverageMesh(float altitude_km, CoverageMeshLOD lod);

/**
 * @brief Get or create a cached coverage mesh for the given altitude and LOD.
 */
Model* GetCachedCoverageMesh(float altitude_km, CoverageMeshLOD lod, Shader shader);

/** Free all cached coverage meshes (GPU and CPU). */
void ClearCoverageMeshCache();

/**
 * @brief Compute a satellite's horizon angle and surface coverage radius.
 *
 * @param out_theta Output horizon angle in radians (0 if below Earth)
 * @param out_radius Output coverage radius at Earth surface in km
 */
void CalculateCoverageParams(const Satellite *sat, float *out_theta, float *out_radius);

/**
 * @brief Pick an LOD from the camera distance to the sub-satellite point.
 */
CoverageMeshLOD SelectCoverageLOD(const Satellite *sat, Camera camera);

/** Simple dot-product frustum cull for a satellite's coverage. */
bool IsCoverageVisible(const Satellite *sat, Camera camera);

/** Number of concentric rings for an LOD level. */
int GetLODRings(CoverageMeshLOD lod);

/** Number of segments per ring for an LOD level. */
int GetLODSegments(CoverageMeshLOD lod);

/**
 * @brief CPU-side cached cap geometry (Earth-centred local frame).
 *
 * Mirrors the GPU mesh so the 2D map path can project the same cap shape
 * without regenerating the trigonometry each frame.
 */
typedef struct {
    int rings;              /* number of concentric rings */
    int segments;           /* number of segments per ring */
    const float *vertices;  /* (rings + 1) * segments * 3 floats, draw units */
} CoverageCapData;

/**
 * @brief Get or create the cached CPU cap geometry for an altitude and LOD.
 *
 * Pointer stays valid until ClearCoverageMeshCache().
 */
const CoverageCapData *GetCachedCoverageCap(float altitude_km, CoverageMeshLOD lod);

/**
 * @brief Pick an LOD for the 2D map from the on-screen cap size.
 *
 * Unlike the 3D distance-based selector, screen size depends on the cap's
 * angular radius (grows with altitude) and the current map zoom.
 */
CoverageMeshLOD SelectCoverageLOD2D(const Satellite *sat, float zoom, float map_w);
