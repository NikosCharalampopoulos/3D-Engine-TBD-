#include "engine/application.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <thread>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"
#include "engine/texture.hpp"

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

// Shader/texture asset paths, read relative to the process's working
// directory -- see README.md, headless runs (and normal ones) are expected
// to be launched from the repo root so these resolve.
constexpr const char* kVertexShaderPath = "assets/shaders/basic.vert";
constexpr const char* kFragmentShaderPath = "assets/shaders/basic.frag";
constexpr const char* kCubeTexturePath = "assets/textures/checker.png";

// Phase 4's directional light: a fixed "sun" direction/color, not yet
// animated or configurable -- proving the Phong math works is this phase's
// goal, not building a full light-management system. uLightDirection points
// *from* the light *toward* the scene (see basic.frag), coming down and
// across so every visible cube face gets a different N.L term instead of
// one face being lit edge-on.
constexpr glm::vec3 kLightDirection{-0.5f, -1.0f, -0.3f};
constexpr glm::vec3 kLightColor{1.0f, 0.95f, 0.85f};
constexpr glm::vec3 kAmbientColor{0.15f, 0.15f, 0.18f};

// Phase 2's fixed eye position, kept here only as a comment for context: it
// was glm::vec3(0, 0, 3) looking at the origin with up (0, 1, 0). Phase 3's
// default camera (see the Application constructor below) is deliberately
// placed somewhere else entirely -- off to the side, higher up, and farther
// back -- so a headless screenshot visibly proves the view now comes from a
// live Camera rather than that old hardcoded matrix.
constexpr glm::vec3 kDefaultCameraPosition{2.6f, 1.9f, 3.4f};
constexpr glm::vec3 kCubeCenter{0.0f, 0.0f, 0.0f};

bool cameraDemoModeFromEnv() {
    const char* value = std::getenv("ENGINE_CAMERA_DEMO");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

}  // namespace

Application::Application(int width, int height, const std::string& title, std::uint64_t maxFrames)
    : window_(width, height, title),
      shader_(kVertexShaderPath, kFragmentShaderPath),
      cube_(makeCube()),
      material_(shader_, Texture(kCubeTexturePath), /*tint=*/glm::vec3(1.0f), /*shininess=*/32.0f),
      camera_(kDefaultCameraPosition),
      maxFrames_(maxFrames),
      cameraDemoMode_(cameraDemoModeFromEnv()) {
    // No depth buffer testing existed in Phase 1 (nothing but a flat clear
    // needed it); real 3D geometry does, so faces occlude each other
    // correctly instead of painting in draw-call order.
    GL_CHECK(glEnable(GL_DEPTH_TEST));

    camera_.setPositionLookingAt(kDefaultCameraPosition, kCubeCenter);

    // Same fixed two-axis rotation Phase 2 hardcoded directly into its model
    // matrix (Rx(35) then Ry(45), i.e. model = Rx * Ry -- Ry applied to the
    // vertex first, then Rx), just expressed as one composed quaternion on a
    // Transform now instead of two chained glm::rotate() calls. Fixed (not
    // time-animated) for the same reason Phase 2 fixed it: the cube reliably
    // shows several faces in every frame regardless of which frame a
    // headless screenshot lands on.
    cubeTransform_.setRotation(glm::angleAxis(glm::radians(35.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));

    if (cameraDemoMode_) {
        LOG_INFO("ENGINE_CAMERA_DEMO set: driving the camera through a scripted orbit instead of live input");
    }
    LOG_INFO("Application initialized");
}

void Application::update(double deltaTime) {
    totalTime_ += deltaTime;

    if (cameraDemoMode_) {
        // Headless-safe stand-in for real input: Xvfb has no real keyboard/
        // mouse, so there's nothing for processKeyboard()/processMouseInput()
        // to read under the verification harness. Instead, step through a
        // small fixed set of known camera positions (all looking at the
        // cube), advancing one step every kFramesPerStep frames. Keyed off
        // frameCount_ (an exact integer, incremented once per loop iteration)
        // rather than totalTime_, so which waypoint is showing at any given
        // frame is fully deterministic and doesn't depend on how long each
        // frame actually took to render on this machine.
        constexpr std::uint64_t kFramesPerStep = 20;
        constexpr std::array<glm::vec3, 4> kWaypoints = {{
            {3.2f, 0.6f, 0.0f},    // orbit: right side, low
            {0.05f, 3.2f, 0.05f},  // orbit: near-overhead (exercises the pitch clamp/near-vertical case)
            {-3.2f, 0.6f, 0.0f},   // orbit: left side
            {0.0f, 0.6f, -3.2f},   // orbit: behind the cube
        }};
        const std::size_t waypoint = (frameCount_ / kFramesPerStep) % kWaypoints.size();
        camera_.setPositionLookingAt(kWaypoints[waypoint], kCubeCenter);
    } else {
        // Real free-fly input: WASD + Space/Shift (or E/Q) move the camera,
        // scaled by deltaTime so speed is frame-rate independent; mouse-look
        // reads the absolute cursor position each frame and lets Camera
        // derive its own delta. Under Xvfb there's no real input device
        // driving either of these -- isKeyPressed() just reports "not
        // pressed" and the cursor position never changes -- so this simply
        // leaves the camera at its constructor-set default pose during
        // headless verification, which is expected and fine.
        camera_.processKeyboard(window_, static_cast<float>(deltaTime));
        const auto [mouseX, mouseY] = window_.getCursorPos();
        camera_.processMouseInput(mouseX, mouseY);
    }
}

void Application::render() {
    GL_CHECK(glClearColor(kClearR, kClearG, kClearB, kClearA));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    const auto [fbWidth, fbHeight] = window_.getSize();
    const float aspect = fbHeight != 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;

    const glm::mat4 model = cubeTransform_.getModelMatrix();
    const glm::mat4 view = camera_.getViewMatrix();
    const glm::mat4 projection = camera_.getProjectionMatrix(aspect);
    // transpose(inverse(mat3(model))), NOT just mat3(model) -- the latter is
    // a common subtly-wrong shortcut that only happens to preserve normal
    // directions under uniform scale/rotation/translation and silently
    // skews them under non-uniform scale. Computed fresh each frame (cheap:
    // one 3x3 inverse) rather than assuming the cube's transform never
    // scales non-uniformly, since Transform allows exactly that.
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

    // material_.bind() sets the shader program, diffuse texture, and
    // tint/shininess -- everything fixed per-material. Model/view/
    // projection/normal-matrix and the light are scene/per-draw state, set
    // here right after.
    material_.bind();
    shader_.setMat4("uModel", model);
    shader_.setMat4("uView", view);
    shader_.setMat4("uProjection", projection);
    shader_.setMat3("uNormalMatrix", normalMatrix);
    shader_.setVec3("uLightDirection", kLightDirection);
    shader_.setVec3("uLightColor", kLightColor);
    shader_.setVec3("uAmbientColor", kAmbientColor);
    shader_.setVec3("uViewPos", camera_.position());

    cube_.bind();
    cube_.draw();
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
