#include "engine/shader.hpp"

#include <glad/glad.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace engine {

namespace {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path);
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// Compiles one shader stage from source, logging the real GL info log (not
// just "compilation failed") on error. Returns the shader id; on failure it
// deletes the (already-created) shader object itself before throwing so no
// partially-compiled shader is leaked.
unsigned int compileStage(GLenum stage, const std::string& source, const std::string& debugName) {
    GLuint shaderId = 0;
    GL_CHECK(shaderId = glCreateShader(stage));

    const char* src = source.c_str();
    GL_CHECK(glShaderSource(shaderId, 1, &src, nullptr));
    GL_CHECK(glCompileShader(shaderId));

    GLint success = GL_FALSE;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        GLint logLength = 0;
        glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(logLength > 0 ? logLength : 1));
        glGetShaderInfoLog(shaderId, static_cast<GLsizei>(log.size()), nullptr, log.data());
        LOG_ERROR("Shader compilation failed (" + debugName + "):\n" + std::string(log.data()));
        glDeleteShader(shaderId);
        throw std::runtime_error("Shader compilation failed: " + debugName);
    }

    return shaderId;
}

}  // namespace

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
    const std::string vertexSrc = readFile(vertexPath);
    const std::string fragmentSrc = readFile(fragmentPath);

    const unsigned int vertexId = compileStage(GL_VERTEX_SHADER, vertexSrc, vertexPath);
    const unsigned int fragmentId = compileStage(GL_FRAGMENT_SHADER, fragmentSrc, fragmentPath);

    GL_CHECK(programId_ = glCreateProgram());
    GL_CHECK(glAttachShader(programId_, vertexId));
    GL_CHECK(glAttachShader(programId_, fragmentId));
    GL_CHECK(glLinkProgram(programId_));

    // Both stages are attached copies at this point (link doesn't need the
    // shader objects afterward); detach + delete them regardless of link
    // success so this function never leaks the intermediate shader objects,
    // matching the "clean up what was already created before throwing"
    // pattern used elsewhere (Window's constructor).
    GL_CHECK(glDetachShader(programId_, vertexId));
    GL_CHECK(glDetachShader(programId_, fragmentId));
    glDeleteShader(vertexId);
    glDeleteShader(fragmentId);

    GLint success = GL_FALSE;
    glGetProgramiv(programId_, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        GLint logLength = 0;
        glGetProgramiv(programId_, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(logLength > 0 ? logLength : 1));
        glGetProgramInfoLog(programId_, static_cast<GLsizei>(log.size()), nullptr, log.data());
        LOG_ERROR("Shader program linking failed (" + vertexPath + " + " + fragmentPath +
                   "):\n" + std::string(log.data()));
        glDeleteProgram(programId_);
        programId_ = 0;
        throw std::runtime_error("Shader program linking failed: " + vertexPath + " + " + fragmentPath);
    }

    LOG_INFO("Shader program linked (" + vertexPath + " + " + fragmentPath + ")");
}

Shader::~Shader() {
    if (programId_ != 0) {
        glDeleteProgram(programId_);
    }
}

Shader::Shader(Shader&& other) noexcept : programId_(other.programId_) {
    other.programId_ = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (programId_ != 0) {
            glDeleteProgram(programId_);
        }
        programId_ = other.programId_;
        other.programId_ = 0;
    }
    return *this;
}

void Shader::use() const {
    GL_CHECK(glUseProgram(programId_));
}

void Shader::setMat4(const std::string& name, const glm::mat4& value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]));
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniform3f(location, value.x, value.y, value.z));
}

void Shader::setFloat(const std::string& name, float value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniform1f(location, value));
}

void Shader::setInt(const std::string& name, int value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniform1i(location, value));
}

}  // namespace engine
