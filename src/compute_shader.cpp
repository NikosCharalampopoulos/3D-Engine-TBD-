#include "engine/compute_shader.hpp"

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

// Identical to shader.cpp's own readFile()/compileStage() -- duplicated
// rather than shared, matching this engine's established "no #include
// across GL source/translation-unit boundaries, just duplicate the small
// amount of logic" convention (see e.g. basic.frag/pbr.frag's own
// duplicated shadow-sampling code).
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open compute shader file: " + path);
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

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
        LOG_ERROR("Compute shader compilation failed (" + debugName + "):\n" + std::string(log.data()));
        glDeleteShader(shaderId);
        throw std::runtime_error("Compute shader compilation failed: " + debugName);
    }

    return shaderId;
}

}  // namespace

ComputeShader::ComputeShader(const std::string& computePath) {
    const std::string src = readFile(computePath);
    const unsigned int computeId = compileStage(GL_COMPUTE_SHADER, src, computePath);

    GL_CHECK(programId_ = glCreateProgram());
    GL_CHECK(glAttachShader(programId_, computeId));
    GL_CHECK(glLinkProgram(programId_));

    GL_CHECK(glDetachShader(programId_, computeId));
    glDeleteShader(computeId);

    GLint success = GL_FALSE;
    glGetProgramiv(programId_, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        GLint logLength = 0;
        glGetProgramiv(programId_, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(logLength > 0 ? logLength : 1));
        glGetProgramInfoLog(programId_, static_cast<GLsizei>(log.size()), nullptr, log.data());
        LOG_ERROR("Compute shader program linking failed (" + computePath + "):\n" + std::string(log.data()));
        glDeleteProgram(programId_);
        programId_ = 0;
        throw std::runtime_error("Compute shader program linking failed: " + computePath);
    }

    LOG_INFO("Compute shader program linked (" + computePath + ")");
}

ComputeShader::~ComputeShader() {
    if (programId_ != 0) {
        glDeleteProgram(programId_);
    }
}

ComputeShader::ComputeShader(ComputeShader&& other) noexcept : programId_(other.programId_) {
    other.programId_ = 0;
}

ComputeShader& ComputeShader::operator=(ComputeShader&& other) noexcept {
    if (this != &other) {
        if (programId_ != 0) {
            glDeleteProgram(programId_);
        }
        programId_ = other.programId_;
        other.programId_ = 0;
    }
    return *this;
}

void ComputeShader::use() const {
    GL_CHECK(glUseProgram(programId_));
}

void ComputeShader::dispatch(unsigned int numGroupsX, unsigned int numGroupsY, unsigned int numGroupsZ) const {
    GL_CHECK(glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ));
}

void ComputeShader::setInt(const std::string& name, int value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniform1i(location, value));
}

void ComputeShader::setFloat(const std::string& name, float value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniform1f(location, value));
}

void ComputeShader::setVec2(const std::string& name, const glm::vec2& value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniform2f(location, value.x, value.y));
}

void ComputeShader::setVec3(const std::string& name, const glm::vec3& value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniform3f(location, value.x, value.y, value.z));
}

void ComputeShader::setMat4(const std::string& name, const glm::mat4& value) const {
    const GLint location = glGetUniformLocation(programId_, name.c_str());
    GL_CHECK(glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]));
}

}  // namespace engine
