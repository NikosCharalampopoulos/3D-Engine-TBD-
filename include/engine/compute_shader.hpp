#ifndef ENGINE_COMPUTE_SHADER_HPP
#define ENGINE_COMPUTE_SHADER_HPP

// Phase 13d: RAII wrapper around a linked GL *compute-only* program -- one
// GL_COMPUTE_SHADER stage, no vertex/fragment stage at all. engine::Shader
// (shader.hpp) deliberately only ever links exactly one vertex + one
// fragment stage (see its own header comment); a compute program is a
// fundamentally different kind of GL object (no rasterization pipeline
// stage involved whatsoever -- see glDispatchCompute), so this is a small
// sibling class rather than an overload bolted onto Shader, mirroring
// Shader's own RAII/move-only/no-uniform-caching shape (same rationale:
// this engine's uniform count per program is small enough that looking up
// each uniform's location by name on every call is not a cost worth
// optimizing away yet).
//
// Used by engine::ClusterLightCuller (cluster_light_culler.hpp) for the two
// compute passes clustered lighting needs: building each cluster's
// view-space AABB, and culling every light against those AABBs to build a
// per-cluster light index list -- see that class for how these are
// dispatched and barriered against the fragment shaders that read their
// SSBO output.

#include <glm/glm.hpp>

#include <string>

namespace engine {

class ComputeShader {
public:
    // computePath is a single GLSL source file containing exactly one
    // layout(local_size_x = ..., ...) in; compute shader stage. Throws
    // std::runtime_error (after logging the real GL info log) if the file
    // can't be read, fails to compile, or fails to link -- same contract as
    // Shader's constructor.
    explicit ComputeShader(const std::string& computePath);
    ~ComputeShader();

    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;
    ComputeShader(ComputeShader&& other) noexcept;
    ComputeShader& operator=(ComputeShader&& other) noexcept;

    // Binds this program as the current one for subsequent uniform uploads
    // and dispatch() calls.
    void use() const;

    // Dispatches numGroupsX/Y/Z workgroups (glDispatchCompute). Caller is
    // responsible for sizing these against this program's own
    // layout(local_size_x/y/z = ...) declaration, and for issuing whatever
    // glMemoryBarrier() the next reader of this dispatch's writes needs --
    // deliberately not done inside dispatch() itself, since the right
    // barrier bit/timing depends on what reads the result next (a later
    // compute dispatch vs. a fragment shader draw call), which only the
    // call site knows.
    void dispatch(unsigned int numGroupsX, unsigned int numGroupsY, unsigned int numGroupsZ) const;

    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setMat4(const std::string& name, const glm::mat4& value) const;

    unsigned int id() const { return programId_; }

private:
    unsigned int programId_ = 0;
};

}  // namespace engine

#endif  // ENGINE_COMPUTE_SHADER_HPP
