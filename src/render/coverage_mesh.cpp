/**
 * @file coverage_mesh.cpp
 * @brief Coverage mesh generation and caching.
 */

#include "coverage_mesh.h"
#include "../util/log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

#include <raymath.h>

// key: (altitude_rounded_10km << 2) | lod
static std::map<int, CoverageMeshCache> g_coverage_cache;

/** rings/segments per LOD level */
int GetLODRings(CoverageMeshLOD lod) {
    switch (lod) {
        case COVERAGE_LOD_HIGH:   return 12;
        case COVERAGE_LOD_MEDIUM: return 8;
        case COVERAGE_LOD_LOW:    return 4;
        default:                  return 8;
    }
}

int GetLODSegments(CoverageMeshLOD lod) {
    switch (lod) {
        case COVERAGE_LOD_HIGH:   return 60;
        case COVERAGE_LOD_MEDIUM: return 40;
        case COVERAGE_LOD_LOW:    return 20;
        default:                  return 40;
    }
}

/**
 * @brief Build the local-frame cap vertices shared by the GPU mesh and CPU cache.
 *
 * +Y points from Earth's centre toward the satellite; `out_verts` must hold
 * (rings + 1) * segments * 3 floats.
 */
static bool BuildCoverageCapVertices(float altitude_km, CoverageMeshLOD lod,
                                     int *out_rings, int *out_segments, float *out_verts) {
    int rings = GetLODRings(lod);
    int segments = GetLODSegments(lod);
    *out_rings = rings;
    *out_segments = segments;

    float r = EARTH_RADIUS_KM + altitude_km;
    if (r <= EARTH_RADIUS_KM)
        return false;

    float theta = acosf(EARTH_RADIUS_KM / r);

    // Lift the cap by the polygon's sag below the true sphere (R*(1-cos(d)),
    // d = triangle angular circumradius) so wide caps don't z-fight the Earth.
    // Radial only, so the 2D projection is unaffected.
    float ring_step = theta / (float)rings;
    float seg_step = (2.0f * PI) / (float)segments;
    float seg_arc = sinf(theta) * seg_step; /* widest at the outer ring */
    float diag = sqrtf(ring_step * ring_step + seg_arc * seg_arc);
    float sag_km = EARTH_RADIUS_KM * (1.0f - cosf(0.5f * diag));
    float lift_km = sag_km + 0.05f; /* never below the previous minimum */
    float surface_km = EARTH_RADIUS_KM + lift_km;
    float surface_draw = surface_km / DRAW_SCALE;

    for (int ring = 0; ring <= rings; ring++) {
        float a = theta * ((float)ring / rings);
        float d_plane = surface_draw * cosf(a);   // along the +Y (satellite) axis
        float r_circle = surface_draw * sinf(a);  // ring radius in the X-Z plane

        for (int seg = 0; seg < segments; seg++) {
            float alpha = (2.0f * PI * seg) / segments;
            int idx = ring * segments + seg;

            out_verts[idx * 3 + 0] = cosf(alpha) * r_circle;
            out_verts[idx * 3 + 1] = d_plane;
            out_verts[idx * 3 + 2] = sinf(alpha) * r_circle;
        }
    }
    return true;
}

/**
 * @brief Generate a coverage cone mesh.
 *
 * Local Earth-centred frame with +Y toward the satellite; the vertex shader
 * rotates it per satellite.
 */
Mesh GenerateCoverageMesh(float altitude_km, CoverageMeshLOD lod) {
    int rings = GetLODRings(lod);
    int segments = GetLODSegments(lod);

    float r = EARTH_RADIUS_KM + altitude_km;
    if (r <= EARTH_RADIUS_KM) {
        Mesh mesh = {0};
        return mesh;
    }

    int vertexCount = (rings + 1) * segments;
    int triangleCount = rings * segments * 2;

    Mesh mesh = {0};
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;

    mesh.vertices = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)MemAlloc(vertexCount * 2 * sizeof(float));
    mesh.normals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.indices = (unsigned short*)MemAlloc(triangleCount * 3 * sizeof(unsigned short));

    int gen_rings = 0, gen_segments = 0;
    BuildCoverageCapVertices(altitude_km, lod, &gen_rings, &gen_segments, mesh.vertices);

    // u = radial distance [0,1] for edge falloff, v = angular
    for (int ring = 0; ring <= rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            int idx = ring * segments + seg;

            mesh.texcoords[idx * 2 + 0] = (float)ring / rings;
            mesh.texcoords[idx * 2 + 1] = (float)seg / segments;

            Vector3 pos = {mesh.vertices[idx * 3 + 0], mesh.vertices[idx * 3 + 1], mesh.vertices[idx * 3 + 2]};
            Vector3 norm = Vector3Normalize(pos);
            mesh.normals[idx * 3 + 0] = norm.x;
            mesh.normals[idx * 3 + 1] = norm.y;
            mesh.normals[idx * 3 + 2] = norm.z;
        }
    }
    
    int triIdx = 0;
    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            int next = (seg + 1) % segments;
            int i0 = ring * segments + seg;
            int i1 = ring * segments + next;
            int i2 = (ring + 1) * segments + seg;
            int i3 = (ring + 1) * segments + next;
            
            mesh.indices[triIdx * 3 + 0] = i0;
            mesh.indices[triIdx * 3 + 1] = i2;
            mesh.indices[triIdx * 3 + 2] = i1;
            triIdx++;
            
            mesh.indices[triIdx * 3 + 0] = i1;
            mesh.indices[triIdx * 3 + 1] = i2;
            mesh.indices[triIdx * 3 + 2] = i3;
            triIdx++;
        }
    }
    
    UploadMesh(&mesh, false);
    
    return mesh;
}

/**
 * @brief Get or create a cached coverage mesh.
 */
Model* GetCachedCoverageMesh(float altitude_km, CoverageMeshLOD lod, Shader shader) {
    // bucket to 10 km, but generate from the real altitude so low-altitude
    // satellites don't collapse to a degenerate zero-altitude cap
    if (altitude_km < 0.0f) altitude_km = 0.0f;
    int altitude_bucket = (int)(altitude_km / 10.0f);
    
    int cache_key = (altitude_bucket << 2) | (int)lod;
    
    auto it = g_coverage_cache.find(cache_key);
    if (it != g_coverage_cache.end() && it->second.is_valid) {
        return &it->second.model;
    }
    
    Mesh mesh = GenerateCoverageMesh(altitude_km, lod);
    
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].shader = shader;
    
    CoverageMeshCache cache_entry;
    cache_entry.mesh = mesh;
    cache_entry.model = model;
    cache_entry.cached_altitude_km = altitude_km;
    cache_entry.is_valid = true;
    
    g_coverage_cache[cache_key] = cache_entry;
    
    return &g_coverage_cache[cache_key].model;
}

/**
 * @brief CPU-side cached cap geometry for the 2D map path (no GPU upload).
 *
 * Shares the GPU cache's key scheme; `vertices` points into the map node's
 * vector, which is stable across insertions.
 */
struct CoverageCapEntry {
    std::vector<float> verts;
    CoverageCapData data;
};
static std::map<int, CoverageCapEntry> g_coverage_cap_cache;

const CoverageCapData *GetCachedCoverageCap(float altitude_km, CoverageMeshLOD lod) {
    if (altitude_km < 0.0f) altitude_km = 0.0f;
    int altitude_bucket = (int)(altitude_km / 10.0f);
    int cache_key = (altitude_bucket << 2) | (int)lod;

    auto it = g_coverage_cap_cache.find(cache_key);
    if (it != g_coverage_cap_cache.end())
        return &it->second.data;

    int rings = GetLODRings(lod);
    int segments = GetLODSegments(lod);
    std::vector<float> verts((size_t)(rings + 1) * segments * 3, 0.0f);

    int gen_rings = 0, gen_segments = 0;
    if (!BuildCoverageCapVertices(altitude_km, lod, &gen_rings, &gen_segments, verts.data()))
        return NULL;

    auto res = g_coverage_cap_cache.emplace(cache_key, CoverageCapEntry{});
    CoverageCapEntry &stored = res.first->second;
    stored.verts = std::move(verts);
    stored.data.rings = gen_rings;
    stored.data.segments = gen_segments;
    stored.data.vertices = stored.verts.data();

    return &stored.data;
}

/**
 * @brief Clear all cached coverage meshes.
 */
void ClearCoverageMeshCache() {
    for (auto& pair : g_coverage_cache) {
        if (pair.second.is_valid) {
            UnloadModel(pair.second.model);
        }
    }
    g_coverage_cache.clear();
    g_coverage_cap_cache.clear();
}

/**
 * @brief Calculate coverage parameters for a satellite.
 */
void CalculateCoverageParams(const Satellite *sat, float *out_theta, float *out_radius) {
    if (!sat || !sat->is_active) {
        *out_theta = 0.0f;
        *out_radius = 0.0f;
        return;
    }
    
    float r = Vector3Length(sat->current_pos);
    if (r <= EARTH_RADIUS_KM) {
        *out_theta = 0.0f;
        *out_radius = 0.0f;
        return;
    }
    
    *out_theta = acosf(EARTH_RADIUS_KM / r);
    *out_radius = EARTH_RADIUS_KM * sinf(*out_theta);
}

/**
 * @brief Select appropriate LOD level based on camera distance.
 *
 * Measured to the sub-satellite surface point, not the satellite: using the
 * satellite forced high-orbit caps to LOW LOD even when zoomed onto the cap.
 */
CoverageMeshLOD SelectCoverageLOD(const Satellite *sat, Camera camera) {
    Vector3 sat_pos = Vector3Scale(sat->current_pos, 1.0f / DRAW_SCALE);
    float r = Vector3Length(sat_pos);
    
    float earth_draw = EARTH_RADIUS_KM / DRAW_SCALE;
    Vector3 centre = (r > 0.0f) ? Vector3Scale(sat_pos, earth_draw / r) : sat_pos;
    float dist = Vector3Distance(camera.position, centre);
    
    if (dist < 5.0f) return COVERAGE_LOD_HIGH;
    if (dist < 15.0f) return COVERAGE_LOD_MEDIUM;
    return COVERAGE_LOD_LOW;
}

/**
 * @brief Select an LOD level for the 2D map based on on-screen cap size.
 */
CoverageMeshLOD SelectCoverageLOD2D(const Satellite *sat, float zoom, float map_w) {
    if (!sat || !sat->is_active)
        return COVERAGE_LOD_LOW;

    float r = Vector3Length(sat->current_pos);
    if (r <= EARTH_RADIUS_KM)
        return COVERAGE_LOD_LOW;

    // cap spans theta / (2*PI) of the map width, scaled by the current zoom
    float theta = acosf(EARTH_RADIUS_KM / r);
    float radius_px = (theta / (2.0f * PI)) * map_w * zoom;

    if (radius_px > 180.0f) return COVERAGE_LOD_HIGH;
    if (radius_px > 60.0f)  return COVERAGE_LOD_MEDIUM;
    return COVERAGE_LOD_LOW;
}

/**
 * @brief Check if satellite's coverage is visible from camera.
 *
 * Tests the sub-satellite point (not the satellite) and expands the accepted
 * cone by the cap's angular radius, so large high-orbit caps aren't culled
 * when the satellite itself is behind the camera.
 */
bool IsCoverageVisible(const Satellite *sat, Camera camera) {
    if (!sat || !sat->is_active)
        return false;
    
    Vector3 sat_pos = Vector3Scale(sat->current_pos, 1.0f / DRAW_SCALE);
    float r = Vector3Length(sat_pos);
    if (r <= 0.0f)
        return false;
    
    float earth_draw = EARTH_RADIUS_KM / DRAW_SCALE;
    Vector3 sub_point = Vector3Scale(sat_pos, earth_draw / r);
    
    Vector3 to_sub = Vector3Subtract(sub_point, camera.position);
    Vector3 view_dir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    
    float dot = Vector3DotProduct(Vector3Normalize(to_sub), view_dir);
    
    // only cull when the whole cap is behind the camera
    float theta = acosf(fminf(EARTH_RADIUS_KM / r, 1.0f));
    return dot > -sinf(theta);
}
