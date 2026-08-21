#ifndef ENGINE_GL_DEBUG_HPP
#define ENGINE_GL_DEBUG_HPP

// Debug-build GL error checking. GL_CHECK(expr) runs `expr` and then, in
// debug builds only (NDEBUG not defined -- CMake's default/Debug configs,
// not Release), drains glGetError() and logs every error it finds via
// LOG_ERROR, tagged with the source call and location. In NDEBUG builds it
// expands to just `expr` with no extra glGetError() calls, so it's cheap
// enough to wrap every GL call with rather than a few chosen ones.

#include <glad/glad.h>

#include <cstdio>
#include <string>

#include "engine/log.hpp"

namespace engine {

inline const char* glErrorString(GLenum err) {
    switch (err) {
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
        default:
            return "UNKNOWN_GL_ERROR";
    }
}

// Drains every pending GL error (there can be more than one) and logs each
// one. Call after a GL call site to attribute errors to it precisely.
inline void checkGlErrors(const char* file, int line, const char* expr) {
    GLenum err = glGetError();
    while (err != GL_NO_ERROR) {
        LOG_ERROR(std::string("GL error ") + glErrorString(err) + " (0x" +
                   [err] {
                       char buf[8];
                       std::snprintf(buf, sizeof(buf), "%04X", err);
                       return std::string(buf);
                   }() +
                   ") after `" + expr + "` at " + file + ":" + std::to_string(line));
        err = glGetError();
    }
}

}  // namespace engine

#ifndef NDEBUG
#define GL_CHECK(expr)                                          \
    do {                                                        \
        expr;                                                   \
        ::engine::checkGlErrors(__FILE__, __LINE__, #expr);      \
    } while (0)
#else
#define GL_CHECK(expr) expr
#endif

#endif  // ENGINE_GL_DEBUG_HPP
