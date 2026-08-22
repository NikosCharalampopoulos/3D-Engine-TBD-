#include "engine/cluster_light_culler.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace engine {

namespace {

// The classic "deferred shading light volume" radius formula (LearnOpenGL,
// "Deferred Shading" chapter's own light-volume-culling section) -- solves
// attenuationFor(constant, linear, quadratic, r) == 5/256 for r, i.e. "the
// distance at which this light's contribution has fallen to a
// visually-negligible fraction of its own peak brightness." Using the
// light's own brightest color channel as that peak (rather than a single
// fixed absolute cutoff shared by every light) matters a lot for this
// engine specifically: application.cpp's kPointLights carries HDR
// intensities up to 6.0 in one channel (see its own Phase 7b comment), and
// a light that bright is still visibly contributing well beyond the range
// a naive "attenuation < 0.01" cutoff would give a color-1.0 light.
//
// Deliberately generous, not tight: this is a *culling* radius, and the
// costly mistake direction is asymmetric -- an over-generous radius just
// means a light gets tested against (and possibly kept in) a few more
// clusters than strictly necessary, costing a handful of wasted
// sphere-vs-AABB tests; a too-small radius would drop a light from a
// cluster it still visibly affects, which is a silent lighting-correctness
// bug (see the phase brief's own emphasis on this). 5/256 is the standard
// reference cutoff for exactly this reason -- generous enough in practice
// that this engine's own light table (verified by hand against the
// distances in kPointLights/kSpotLights' own comments) never needs a
// radius anywhere near this scene's ~20-unit extent to fully cover every
// surface each light actually lights.
float computeLightRadius(const glm::vec3& color, float constant, float linear, float quadratic) {
    const float lightMax = std::max({color.r, color.g, color.b});
    if (quadratic <= 0.0f) {
        // Every light in this engine's own tables (application.cpp's
        // kPointLights/kSpotLights) has a strictly positive quadratic term,
        // so this never actually triggers -- guarded anyway rather than
        // dividing by zero below if some future light table ever omits it.
        return 1000.0f;
    }
    constexpr float kCutoff = 256.0f / 5.0f;
    const float c = constant - lightMax * kCutoff;
    const float discriminant = linear * linear - 4.0f * quadratic * c;
    if (discriminant <= 0.0f) {
        // Only possible if the light is so dim/short-range that even
        // distance 0 already exceeds the cutoff -- treat it as "reaches
        // effectively nowhere" rather than producing a NaN from sqrt() of a
        // negative number.
        return 0.0f;
    }
    return (-linear + std::sqrt(discriminant)) / (2.0f * quadratic);
}

}  // namespace

ClusterLightCuller::ClusterLightCuller(const std::string& aabbComputePath, const std::string& cullComputePath)
    : aabbShader_(aabbComputePath), cullShader_(cullComputePath) {
    GL_CHECK(glGenBuffers(1, &aabbBuffer_));
    GL_CHECK(glGenBuffers(1, &lightListBuffer_));

    // Struct sizes here must match cluster_aabb.comp's/cluster_cull.comp's
    // own std430 struct layouts exactly -- both are plain vec4/uint members
    // with no vec3 (std430's one real padding trap) anywhere in either
    // struct, so std430's tight 4-byte-scalar/16-byte-vec4 packing rules
    // give byte-for-byte the same layout this arithmetic computes.
    constexpr std::size_t kAABBStructBytes = 2 * 4 * sizeof(float);  // vec4 minPoint + vec4 maxPoint
    constexpr std::size_t kLightListStructBytes =
        sizeof(unsigned int) * (1 + kMaxPointLights + 1 + kMaxSpotLights);  // pointCount+indices+spotCount+indices

    GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, aabbBuffer_));
    GL_CHECK(glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(kClusterCount * kAABBStructBytes),
                           nullptr, GL_DYNAMIC_DRAW));

    GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightListBuffer_));
    GL_CHECK(glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(kClusterCount * kLightListStructBytes),
                           nullptr, GL_DYNAMIC_DRAW));

    GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));

    // Binds each buffer to its fixed indexed target once, for the whole
    // run -- see this class's header comment for why neither compute
    // shader nor either fragment shader needs to rebind these per
    // dispatch/draw call.
    GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kClusterAABBBinding, aabbBuffer_));
    GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kClusterLightListBinding, lightListBuffer_));

    LOG_INFO("ClusterLightCuller: " + std::to_string(kGridX) + "x" + std::to_string(kGridY) + "x" +
              std::to_string(kGridZ) + " = " + std::to_string(kClusterCount) +
              " clusters (AABB buffer " + std::to_string(kClusterCount * kAABBStructBytes) +
              " bytes, light-list buffer " + std::to_string(kClusterCount * kLightListStructBytes) + " bytes)");
}

ClusterLightCuller::~ClusterLightCuller() {
    if (lightListBuffer_ != 0) {
        glDeleteBuffers(1, &lightListBuffer_);
    }
    if (aabbBuffer_ != 0) {
        glDeleteBuffers(1, &aabbBuffer_);
    }
}

void ClusterLightCuller::computeClusterAABBs(const glm::mat4& projection, float nearPlane, glm::vec2 screenSize) {
    const glm::mat4 inverseProjection = glm::inverse(projection);

    aabbShader_.use();
    aabbShader_.setMat4("uInverseProjection", inverseProjection);
    aabbShader_.setFloat("uNearPlane", nearPlane);
    aabbShader_.setFloat("uFarPlane", kClusterFarDistance);
    aabbShader_.setVec2("uScreenSize", screenSize);

    // local_size_x/y/z = 4 in cluster_aabb.comp; kGridX/Y/Z (12, 8, 24) all
    // divide evenly by 4, so every dispatched workgroup is fully in-bounds
    // (no partial/edge workgroup whose extra invocations the shader has to
    // bounds-check away -- it still does, defensively, but there's none in
    // practice with these grid dimensions).
    constexpr unsigned int kLocalSize = 4;
    static_assert(kGridX % kLocalSize == 0 && kGridY % kLocalSize == 0 && kGridZ % kLocalSize == 0,
                  "kGridX/Y/Z must divide evenly by cluster_aabb.comp's local_size_x/y/z (4)");
    aabbShader_.dispatch(kGridX / kLocalSize, kGridY / kLocalSize, kGridZ / kLocalSize);

    // Every cluster's AABB write must be visible before cullLights() (run
    // later -- possibly many frames later, since this only runs once, see
    // this class's header comment) reads it.
    GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));
}

void ClusterLightCuller::cullLights(const glm::mat4& view, const ClusterLightInput* pointLights,
                                     std::size_t numPointLights, const ClusterLightInput* spotLights,
                                     std::size_t numSpotLights) {
    cullShader_.use();

    cullShader_.setInt("uNumPointLights", static_cast<int>(numPointLights));
    for (std::size_t i = 0; i < numPointLights; ++i) {
        const glm::vec3 viewPos = glm::vec3(view * glm::vec4(pointLights[i].worldPosition, 1.0f));
        const float radius = computeLightRadius(pointLights[i].color, pointLights[i].constant, pointLights[i].linear,
                                                 pointLights[i].quadratic);
        const std::string prefix = "uPointLightsView[" + std::to_string(i) + "].";
        cullShader_.setVec3(prefix + "viewPosition", viewPos);
        cullShader_.setFloat(prefix + "radius", radius);
    }

    cullShader_.setInt("uNumSpotLights", static_cast<int>(numSpotLights));
    for (std::size_t i = 0; i < numSpotLights; ++i) {
        const glm::vec3 viewPos = glm::vec3(view * glm::vec4(spotLights[i].worldPosition, 1.0f));
        const float radius = computeLightRadius(spotLights[i].color, spotLights[i].constant, spotLights[i].linear,
                                                 spotLights[i].quadratic);
        const std::string prefix = "uSpotLightsView[" + std::to_string(i) + "].";
        cullShader_.setVec3(prefix + "viewPosition", viewPos);
        cullShader_.setFloat(prefix + "radius", radius);
    }

    // local_size_x = 256 in cluster_cull.comp; kClusterCount (2304) divides
    // evenly by 256 (9 groups exactly) -- 1D dispatch over the flat cluster
    // index is simpler than mirroring the AABB pass's 3D dispatch here,
    // since light culling has no natural 3-axis structure of its own (it's
    // just "for every cluster, test every light").
    constexpr unsigned int kLocalSize = 256;
    static_assert(kClusterCount % kLocalSize == 0, "kClusterCount must divide evenly by cluster_cull.comp's local_size_x (256)");
    cullShader_.dispatch(kClusterCount / kLocalSize, 1, 1);

    // Every fragment shader draw call this frame reads lightListBuffer_ via
    // its own layout(std430, binding = kClusterLightListBinding) block --
    // this dispatch's writes must be visible before any of them runs.
    GL_CHECK(glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT));
}

ClusterOccupancyStats ClusterLightCuller::readOccupancyStats() const {
    // Must match cluster_cull.comp's ClusterLightList layout exactly:
    // pointCount, pointIndices[kMaxPointLights], spotCount,
    // spotIndices[kMaxSpotLights] -- all plain uints, so std430 packs them
    // at a flat 4-byte stride with no padding to account for (the same
    // "no vec3 anywhere in this struct" property the constructor's own
    // kLightListStructBytes comment already relies on).
    constexpr std::size_t kUintsPerCluster = 1 + kMaxPointLights + 1 + kMaxSpotLights;
    std::vector<unsigned int> raw(kClusterCount * kUintsPerCluster);

    // Bug-review fix: cullLights()'s own glMemoryBarrier(GL_SHADER_STORAGE_
    // BARRIER_BIT) only guarantees a *shader* (draw/dispatch) reading this
    // SSBO afterward sees cullLights()'s writes -- it does NOT cover this
    // function's own CPU-side glGetBufferSubData read below, which the GL
    // 4.3 spec instead gates on GL_BUFFER_UPDATE_BARRIER_BIT. Without this,
    // this read-back is technically unsynchronized against the light-culling
    // dispatch that (possibly moments ago, see this call's call site in
    // Application::render()) wrote it -- undefined per spec even though
    // most desktop GL drivers happen to synchronize broadly enough for this
    // to not visibly misbehave in practice.
    GL_CHECK(glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT));

    GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightListBuffer_));
    GL_CHECK(glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                                 static_cast<GLsizeiptr>(raw.size() * sizeof(unsigned int)), raw.data()));
    GL_CHECK(glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0));

    ClusterOccupancyStats stats;
    stats.totalClusters = kClusterCount;
    std::size_t totalLightsAcrossOccupiedClusters = 0;
    for (std::size_t cluster = 0; cluster < kClusterCount; ++cluster) {
        const unsigned int* clusterData = &raw[cluster * kUintsPerCluster];
        const unsigned int pointCount = clusterData[0];
        // spotCount sits right after pointCount's own kMaxPointLights-entry
        // index array -- see the struct layout comment above.
        const unsigned int spotCount = clusterData[1 + kMaxPointLights];
        const unsigned int totalCount = pointCount + spotCount;
        if (totalCount > 0) {
            ++stats.occupiedClusters;
            totalLightsAcrossOccupiedClusters += totalCount;
        }
    }
    stats.averageLightsPerOccupiedCluster = stats.occupiedClusters > 0
                                                 ? static_cast<double>(totalLightsAcrossOccupiedClusters) /
                                                       static_cast<double>(stats.occupiedClusters)
                                                 : 0.0;
    return stats;
}

}  // namespace engine
