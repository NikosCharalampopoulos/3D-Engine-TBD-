#include "engine/application.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace engine {

namespace {

// Cornflower blue, carried over from Phase 0 as the "yes, the context and
// loop actually work" clear color: a screenshot averaging to this (rather
// than black) proves clear+swap ran, and running it every frame (rather
// than once) proves the loop is actually looping. It also needs to stay
// visually distinct from every cube face color below so a screenshot can
// tell background from geometry at a glance.
constexpr float kClearR = 0.3921f;
constexpr float kClearG = 0.5843f;
constexpr float kClearB = 0.9294f;
constexpr float kClearA = 1.0f;

// There's no real display refresh to synchronize with under Xvfb/llvmpipe,
// so cap the frame rate by hand instead of busy-spinning the CPU as fast as
// the software rasterizer allows.
constexpr auto kFrameThrottle = std::chrono::milliseconds(16);

// One flat color per cube face (see makeCube()'s indexing: face i occupies
// indices [i*6, i*6+6)), set as a uniform before that face's draw call so
// the six faces are visually distinguishable from each other and from the
// background -- this phase's proof that the shader+buffer pipeline actually
// renders real geometry, not a placeholder for real lighting (Phase 4+).
constexpr std::array<glm::vec3, 6> kFaceColors = {
    glm::vec3{0.90f, 0.15f, 0.15f},  // +Z front:  red
    glm::vec3{0.15f, 0.75f, 0.20f},  // -Z back:   green
    glm::vec3{0.15f, 0.35f, 0.90f},  // +X right:  blue
    glm::vec3{0.95f, 0.85f, 0.15f},  // -X left:   yellow
    glm::vec3{0.85f, 0.20f, 0.85f},  // +Y top:    magenta
    glm::vec3{0.15f, 0.85f, 0.85f},  // -Y bottom: cyan
};

// Shader source paths, read relative to the process's working directory --
// see README.md, headless runs (and normal ones) are expected to be
// launched from the repo root so "assets/shaders/..." resolves.
constexpr const char* kVertexShaderPath = "assets/shaders/basic.vert";
constexpr const char* kFragmentShaderPath = "assets/shaders/basic.frag";

}  // namespace

Application::Application(int width, int height, const std::string& title, std::uint64_t maxFrames)
    : window_(width, height, title),
      shader_(kVertexShaderPath, kFragmentShaderPath),
      cube_(makeCube()),
      maxFrames_(maxFrames) {
    // No depth buffer testing existed in Phase 1 (nothing but a flat clear
    // needed it); real 3D geometry does, so faces occlude each other
    // correctly instead of painting in draw-call order.
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    LOG_INFO("Application initialized");
}

void Application::update(double deltaTime) {
    // Nothing consumes delta-time yet -- just accumulate it so the pattern
    // (and a visible use of the value) is in place for later phases.
    totalTime_ += deltaTime;
}

void Application::render() {
    GL_CHECK(glClearColor(kClearR, kClearG, kClearB, kClearA));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    // No Camera yet (Phase 3 owns that) -- a fixed hand-built view +
    // perspective projection is enough to prove real 3D depth/occlusion
    // works. Model is a fixed (not time-animated) two-axis rotation so the
    // cube reliably shows 3+ faces in every frame, including whichever one
    // a headless screenshot happens to land on, rather than depending on
    // catching a good moment in an animated rotation. Replace this whole
    // block with Camera-provided view/projection matrices in Phase 3.
    const auto [fbWidth, fbHeight] = window_.getSize();
    const float aspect = fbHeight != 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;

    const glm::mat4 model = glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(35.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                                         glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    const glm::mat4 mvp = projection * view * model;

    shader_.use();
    shader_.setMat4("uMVP", mvp);
    cube_.bind();
    for (std::size_t face = 0; face < kFaceColors.size(); ++face) {
        shader_.setVec3("uColor", kFaceColors[face]);
        cube_.drawRange(face * 6, 6);
    }
}

void Application::run() {
    std::string startMsg = "Entering main loop";
    if (maxFrames_ != 0) {
        startMsg += " (capped at " + std::to_string(maxFrames_) + " frame(s), headless mode)";
    }
    LOG_INFO(startMsg);

    double lastTime = glfwGetTime();

    while (!window_.shouldClose()) {
        if (window_.isKeyPressed(GLFW_KEY_ESCAPE)) {
            LOG_INFO("ESC pressed, exiting main loop");
            break;
        }
        if (maxFrames_ != 0 && frameCount_ >= maxFrames_) {
            LOG_INFO("Reached max frame count, exiting main loop");
            break;
        }

        window_.pollEvents();

        const double currentTime = glfwGetTime();
        const double deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        update(deltaTime);
        render();
        window_.swapBuffers();

        ++frameCount_;
        std::this_thread::sleep_for(kFrameThrottle);
    }

    LOG_INFO("Exited main loop after " + std::to_string(frameCount_) + " frame(s), " +
              std::to_string(totalTime_) + "s total");
}

}  // namespace engine
