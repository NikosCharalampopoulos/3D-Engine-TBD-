#include "engine/window.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <string>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace {

void glfwErrorCallback(int error, const char* description) {
    LOG_ERROR(std::string("GLFW error ") + std::to_string(error) + ": " + description);
}

}  // namespace

namespace engine {

Window::Window(int width, int height, const std::string& title) : width_(width), height_(height) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    // Visible (not hidden) so an X server (e.g. Xvfb) actually maps a window
    // that can be screenshotted from the outside for headless verification.
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window (no GL context available?)");
    }

    glfwMakeContextCurrent(window_);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        throw std::runtime_error("Failed to load OpenGL functions via glad");
    }

    LOG_INFO(std::string("OpenGL vendor:   ") + reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    LOG_INFO(std::string("OpenGL renderer: ") + reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    LOG_INFO(std::string("OpenGL version:  ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    GL_CHECK(glViewport(0, 0, width_, height_));

    LOG_INFO("Window created (" + std::to_string(width_) + "x" + std::to_string(height_) + ")");
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    // Safe even if glfwInit() never succeeded / window_ was never set: this
    // destructor only runs after a successful construction (a throwing
    // constructor's partially-built object has no destructor invoked), and
    // construction always leaves glfwInit() successfully called by the time
    // it returns normally.
    glfwTerminate();
    LOG_INFO("Window destroyed, GLFW terminated");
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_) != 0;
}

void Window::swapBuffers() {
    glfwSwapBuffers(window_);
}

void Window::pollEvents() {
    glfwPollEvents();
}

std::pair<int, int> Window::getSize() const {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    return {w, h};
}

bool Window::isKeyPressed(int key) const {
    return glfwGetKey(window_, key) == GLFW_PRESS;
}

}  // namespace engine
