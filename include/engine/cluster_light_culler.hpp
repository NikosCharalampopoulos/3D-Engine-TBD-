#ifndef ENGINE_CLUSTER_LIGHT_CULLER_HPP
#define ENGINE_CLUSTER_LIGHT_CULLER_HPP

// Phase 13d: clustered forward light culling -- see README.md's Phase 13d
// section for the full write-up. This replaces basic.frag/pbr.frag's old
// "loop over every point/spot light for every fragment, unconditionally"
// approach with the standard clustered-shading technique (Olsson &
// Assarsson; the same logarithmic-Z-slicing scheme popularized by Doom
// (2016)'s and Angry Birds' clustered forward renderers): the camera's view
// frustum is divided into a 3D grid of "clusters" (kGridX x kGridY tiles on
// screen, kGridZ logarithmic depth slices); each cluster gets its own list
// of which lights actually reach it; a fragment shader looks up its own
// cluster's list instead of iterating every light in the scene.
//
// Two GPU-resident pieces, both plain SSBOs (Shader Storage Buffer
// Objects) built by a dedicated compute shader each and read directly by
// basic.frag/pbr.frag at the fixed binding points below (glBindBufferBase,
// done once in the constructor -- an SSBO binding is ordinary persistent GL
// state, like a texture unit binding, not something that needs rebinding
// per draw call):
//   - kClusterAABBBinding: every cluster's own view-space axis-aligned
//     bounding box (computeClusterAABBs()).
//   - kClusterLightListBinding: every cluster's own point/spot light index
//     list (cullLights()), each light's actual PBR/Phong data (position,
//     color, attenuation, spot cone) still living in basic.frag/pbr.frag's
//     existing uPointLights[]/uSpotLights[] uniform arrays exactly as
//     before this phase -- clustering only changes *which* and *how many*
//     of those array entries a given fragment loops over, never how a
//     light's own contribution is computed once selected.
//
// Why AABBs are built once (not every frame) but light culling runs every
// frame: a cluster's view-space AABB is a pure function of the *projection*
// matrix (screen tile bounds + a near/far Z slice, both unprojected through
// the projection's own inverse -- see cluster_aabb.comp) and the window's
// pixel dimensions -- it does NOT depend on the view matrix (the camera's
// position/orientation) at all, because "view space" by definition already
// factors the camera's pose out. This engine's window size is fixed for
// its whole run (Window has no resize handling) and Camera's FOV/near/far
// are fixed constants never mutated after construction, so the projection
// matrix -- and therefore every cluster's AABB -- never changes after
// startup; computeClusterAABBs() only needs to run once. Light culling, by
// contrast, tests each light's *view-space* position against those AABBs,
// and a light's view-space position changes every time the camera moves
// (even though this engine's own lights are all static world-space
// constants -- see application.cpp's kPointLights/kSpotLights -- the camera
// itself moves every frame in both free-fly and the scripted demo paths),
// so cullLights() must run every frame.

#include <glm/glm.hpp>

#include <cstddef>
#include <string>

#include "engine/compute_shader.hpp"

namespace engine {

// One point/spot light's input to the culling pass, in the form it needs:
// a world-space position (cullLights() transforms it to view space itself,
// once per light per frame -- cheap, at this engine's light counts) plus
// its color/attenuation, which computeLightRadius() (cluster_light_culler.cpp)
// turns into an effective culling radius.
struct ClusterLightInput {
    glm::vec3 worldPosition{0.0f};
    glm::vec3 color{1.0f};
    float constant = 1.0f;
    float linear = 0.0f;
    float quadratic = 0.0f;
};

// Read back from the light-list SSBO purely for logging/verification (see
// ClusterLightCuller::readOccupancyStats()) -- proof that cullLights() is
// actually building varied, non-trivial per-cluster light lists rather
// than "compiles and doesn't crash" (the phase brief's own bar). Never
// consumed by rendering itself, only by Application's periodic log line.
struct ClusterOccupancyStats {
    std::size_t occupiedClusters = 0;
    std::size_t totalClusters = 0;
    double averageLightsPerOccupiedCluster = 0.0;
};

class ClusterLightCuller {
public:
    // Cluster grid dimensions. Kept here (not just .cpp-local) because
    // Application::render() needs kClusterFarDistance/kGridX/Y/Z to size
    // compute dispatches and to upload the matching uniforms to
    // shader_/pbrShader_ -- and basic.frag/pbr.frag need the *same* numbers
    // again, hand-duplicated the same "no #include across GLSL files"
    // way every other GLSL/C++-shared constant in this engine already is
    // (see application.cpp's MAX_POINT_LIGHTS/kPointLights comment).
    //
    // 12x8 tiles x 24 depth slices = 2304 clusters: 12x8 is a plain, modest
    // screen-space tiling (this engine's fixed 800x600 window is 4:3, not
    // 16:9, so 12x8 -- the same 1.5:1 aspect -- tiles it evenly rather than
    // forcing the common "16x9" reference grid's own aspect onto a
    // differently-shaped window); 24 Z-slices is the commonly-cited slice
    // count for the logarithmic scheme (Doom 2016's own published cluster
    // grid uses 24 as well) and is deliberately NOT reduced further even
    // though this scene's actual light count (a handful) doesn't strictly
    // need this much resolution -- the point of this phase is proving the
    // *architecture* scales, and finer Z-slicing is what makes the
    // logarithmic depth split actually pay off close to the camera, which a
    // too-coarse grid would make hard to demonstrate/see in the debug
    // visualization (ENGINE_CLUSTER_DEBUG). All three are also chosen to
    // divide evenly into small local_size_x/y/z workgroup sizes (see
    // cluster_light_culler.cpp) with no partial/edge workgroups to worry
    // about.
    static constexpr unsigned int kGridX = 12;
    static constexpr unsigned int kGridY = 8;
    static constexpr unsigned int kGridZ = 24;
    static constexpr unsigned int kClusterCount = kGridX * kGridY * kGridZ;

    // How far, in view-space depth units, the logarithmic Z-slicing's range
    // reaches -- deliberately far less than Camera's own 100-unit far plane
    // (camera.hpp's farPlane_ default): this hand-authored scene's entire
    // shadow-relevant content fits within roughly a 20-unit radius (see
    // application.cpp's kCascadeShadowDistance/kCascadeLightBackoff
    // comments, the same reasoning applied there for CSM's cascade
    // distance), so spending this grid's Z resolution out to 100 units
    // would burn most of it on empty space nothing is ever drawn into.
    static constexpr float kClusterFarDistance = 25.0f;

    // Mirrors basic.frag/pbr.frag's MAX_POINT_LIGHTS/MAX_SPOT_LIGHTS (and
    // application.cpp's kPointLights/kSpotLights static_asserts against
    // them) -- the fixed capacity a cluster's own light-index list can
    // hold, which only ever needs to be as large as the total number of
    // lights that could ever exist, not some independent per-cluster
    // budget.
    static constexpr unsigned int kMaxPointLights = 8;
    static constexpr unsigned int kMaxSpotLights = 4;

    // Fixed SSBO binding points, shared by both compute shaders (which
    // write them) and basic.frag/pbr.frag (which read them) -- see this
    // header's own comment on why glBindBufferBase only needs to run once.
    static constexpr unsigned int kClusterAABBBinding = 0;
    static constexpr unsigned int kClusterLightListBinding = 1;

    ClusterLightCuller(const std::string& aabbComputePath, const std::string& cullComputePath);
    ~ClusterLightCuller();

    ClusterLightCuller(const ClusterLightCuller&) = delete;
    ClusterLightCuller& operator=(const ClusterLightCuller&) = delete;
    ClusterLightCuller(ClusterLightCuller&&) = delete;
    ClusterLightCuller& operator=(ClusterLightCuller&&) = delete;

    // Builds every cluster's view-space AABB from `projection` alone (see
    // this header's own comment on why the view matrix plays no part here).
    // screenSize is the real framebuffer size in pixels -- the same
    // tiling basic.frag/pbr.frag's own cluster-index computation uses via
    // gl_FragCoord, so both sides agree on exactly where each tile's edges
    // fall.
    void computeClusterAABBs(const glm::mat4& projection, float nearPlane, glm::vec2 screenSize);

    // Rebuilds every cluster's light index list against this frame's real
    // camera `view` matrix -- see this header's own comment on why this
    // must run every frame even though the lights themselves never move.
    // pointLights/spotLights arrays need only their first numPointLights/
    // numSpotLights entries populated (mirrors basic.frag's own
    // "fixed-size array + live count" uniform convention); both counts must
    // be <= kMaxPointLights/kMaxSpotLights (this engine's own light tables
    // are statically asserted against those same bounds already, see
    // application.cpp).
    void cullLights(const glm::mat4& view, const ClusterLightInput* pointLights, std::size_t numPointLights,
                     const ClusterLightInput* spotLights, std::size_t numSpotLights);

    // Reads the whole light-list SSBO back to the CPU (glGetBufferSubData)
    // and summarizes it -- deliberately NOT called every frame (a GPU->CPU
    // buffer read-back is exactly the kind of stall a real per-frame hot
    // path should avoid); Application calls this only periodically, purely
    // to log concrete proof that clustering is doing real, varying work
    // (see this header's own comment on ClusterOccupancyStats).
    ClusterOccupancyStats readOccupancyStats() const;

private:
    unsigned int aabbBuffer_ = 0;
    unsigned int lightListBuffer_ = 0;
    ComputeShader aabbShader_;
    ComputeShader cullShader_;
};

}  // namespace engine

#endif  // ENGINE_CLUSTER_LIGHT_CULLER_HPP
