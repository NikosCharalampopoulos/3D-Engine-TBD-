#ifndef ENGINE_SHADER_HPP
#define ENGINE_SHADER_HPP

// RAII wrapper around a linked GL shader program (one vertex + one fragment
// stage, which is all this phase and the near-term future needs -- geometry/
// compute stages can be added later if a phase actually needs them).
//
// Construction reads both source files from disk, compiles each stage, links
// them into a program, and checks compile/link status at every step --
// failures are logged via LOG_ERROR with the real GL info log (not just a
// silent bad program id) and surfaced as a thrown std::runtime_error, mirroring
// Window's "throw on first failure, clean up what was already created"
// pattern from Phase 1.
//
// Move-only: a GL program name is a scarce handle owned by exactly one
// Shader at a time. Copying would let two destructors glDeleteProgram the
// same name (double free / use-after-delete of the name once one instance
// dies), so copy is disabled and move transfers ownership, leaving the
// moved-from Shader's id cleared to 0 so its destructor is a no-op.

#include <glm/glm.hpp>

#include <string>

namespace engine {

class Shader {
public:
    // vertexPath/fragmentPath are file paths (e.g. under assets/shaders/)
    // read relative to the process's current working directory. Throws
    // std::runtime_error if either file can't be read, either stage fails to
    // compile, or linking fails.
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    // Binds this program as the current one for subsequent draw calls.
    void use() const;
    // Alias for use() -- both names are common in the wild; keep the one
    // callers reach for by feel.
    void bind() const { use(); }

    // Uniform setters. Each looks up the uniform's location by name on every
    // call rather than caching it -- fine for this phase's uniform count; a
    // caching layer (e.g. name -> location map) is a later optimization, not
    // required here. A missing/optimized-out uniform yields location -1,
    // which glUniform* calls silently ignore per the GL spec, so this never
    // throws -- it's only appropriate to call after use()/bind().
    void setMat4(const std::string& name, const glm::mat4& value) const;
    // For the normal matrix (transpose(inverse(mat3(model)))) and any other
    // mat3 uniform -- added in Phase 4 alongside lighting, which is the
    // first thing that needs one.
    void setMat3(const std::string& name, const glm::mat3& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setFloat(const std::string& name, float value) const;
    void setInt(const std::string& name, int value) const;

    unsigned int id() const { return programId_; }

private:
    unsigned int programId_ = 0;
};

}  // namespace engine

#endif  // ENGINE_SHADER_HPP
