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
#include <utility>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"
#include "engine/model.hpp"

namespace engine {

namespace {

// Cornflower blue, carried over from Phase 0 as the "yes, the context and
// loop actually work" clear color: a screenshot averaging to this (rather
// than black) proves clear+swap ran, and running it every frame (rather
// than once) proves the loop is actually looping. It also needs to stay
// visually distinct from the scene's own colors (see scene.mtl) so a
// screenshot can tell background from geometry at a glance.
constexpr float kClearR = 0.3921f;
constexpr float kClearG = 0.5843f;
constexpr float kClearB = 0.9294f;
constexpr float kClearA = 1.0f;

// There's no real display refresh to synchronize with under Xvfb/llvmpipe,
// so cap the frame rate by hand instead of busy-spinning the CPU as fast as
// the software rasterizer allows.
constexpr auto kFrameThrottle = std::chrono::milliseconds(16);

// Shader/model asset paths, read relative to the process's working
// directory -- see README.md, headless runs (and normal ones) are expected
// to be launched from the repo root so these resolve.
constexpr const char* kVertexShaderPath = "assets/shaders/basic.vert";
constexpr const char* kFragmentShaderPath = "assets/shaders/basic.frag";
// Phase 5's hand-authored test scene: three separate objects (a pyramid, a
// table, and a small box sitting on top of the table) at different
// positions, proving Model's node hierarchy + transform composition places
// more than one mesh correctly -- see assets/models/scene.obj and
// model.cpp.
constexpr const char* kScenePath = "assets/models/scene.obj";

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
// Phase 5's scene.obj lays its three objects out (deliberately) so their
// combined bounding box is still roughly centered near the origin -- see
// assets/models/scene.obj -- so this unchanged Phase 3/4 camera target
// still frames the whole scene, not just one object.
constexpr glm::vec3 kSceneCenter{0.0f, 0.0f, 0.0f};

bool cameraDemoModeFromEnv() {
    const char* value = std::getenv("ENGINE_CAMERA_DEMO");
    return value != nullptr && *value != '\0' && std::string(value) != "0";
}

}  // namespace

Application::Application(int width, int height, const std::string& title, std::uint64_t maxFrames)
    : window_(width, height, title),
      shader_(resources_.getShader(kVertexShaderPath, kFragmentShaderPath)),
      camera_(kDefaultCameraPosition),
      maxFrames_(maxFrames),
      cameraDemoMode_(cameraDemoModeFromEnv()) {
    // No depth buffer testing existed in Phase 1 (nothing but a flat clear
    // needed it); real 3D geometry does, so faces occlude each other
    // correctly instead of painting in draw-call order.
    GL_CHECK(glEnable(GL_DEPTH_TEST));

    camera_.setPositionLookingAt(kDefaultCameraPosition, kSceneCenter);

    // The scene is one Entity wrapping the same Phase 5 model
    // (assets/models/scene.obj), loaded through resources_ instead of
    // constructed directly. A small fixed rotation is applied to its
    // Transform (rather than identity), for the same reason Phase 2-4 fixed
    // cubeTransform_'s rotation: proving the composition (entity transform *
    // accumulated parent node transform * node's own local transform, see
    // Model::drawNode()) is actually being applied, not just compiling,
    // regardless of which frame a headless screenshot lands on. 12 degrees
    // is small enough that scene.obj's three objects (deliberately laid out
    // to fit within Phase 3/4's unchanged camera framing) stay comfortably
    // in frame after the rotation.
    Entity sceneEntity("scene", resources_.getModel(kScenePath, *shader_));
    sceneEntity.transform.setRotation(glm::angleAxis(glm::radians(12.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    entities_.push_back(std::move(sceneEntity));

    if (cameraDemoMode_) {
        LOG_INFO("ENGINE_CAMERA_DEMO set: driving the camera through a scripted orbit instead of live input");
    }
    LOG_INFO("Application initialized");
}

void Application::update(double deltaTime, const InputState& input) {
    totalTime_ += deltaTime;

    if (cameraDemoMode_) {
        // Headless-safe stand-in for real input: Xvfb has no real keyboard/
        // mouse, so there's nothing for processMovement()/processMouseInput()
        // to read under the verification harness. Instead, step through a
        // small fixed set of known camera positions (all looking at the
        // scene), advancing one step every kFramesPerStep frames. Keyed off
        // frameCount_ (an exact integer, incremented once per loop iteration)
        // rather than totalTime_, so which waypoint is showing at any given
        // frame is fully deterministic and doesn't depend on how long each
        // frame actually took to render on this machine.
        constexpr std::uint64_t kFramesPerStep = 20;
        constexpr std::array<glm::vec3, 4> kWaypoints = {{
            {3.2f, 0.6f, 0.0f},    // orbit: right side, low
            {0.05f, 3.2f, 0.05f},  // orbit: near-overhead (exercises the pitch clamp/near-vertical case)
            {-3.2f, 0.6f, 0.0f},   // orbit: left side
            {0.0f, 0.6f, -3.2f},   // orbit: behind the scene
        }};
        const std::size_t waypoint = (frameCount_ / kFramesPerStep) % kWaypoints.size();
        camera_.setPositionLookingAt(kWaypoints[waypoint], kSceneCenter);
    } else {
        // Real free-fly input: WASD + Space/Shift (or E/Q) move the camera,
        // scaled by deltaTime so speed is frame-rate independent; mouse-look
        // reads the absolute cursor position each frame and lets Camera
        // derive its own delta. `input` is the InputState run() already
        // polled from window_ once this frame (see input.hpp) -- Camera
        // itself no longer touches window_ directly. Under Xvfb there's no
        // real input device driving any of this -- every InputState flag is
        // false and the cursor position never changes -- so this simply
        // leaves the camera at its constructor-set default pose during
        // headless verification, which is expected and fine.
        camera_.processMovement(input, static_cast<float>(deltaTime));
        camera_.processMouseInput(input.cursorX, input.cursorY);
    }
}

void Application::render() {
    GL_CHECK(glClearColor(kClearR, kClearG, kClearB, kClearA));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    const auto [fbWidth, fbHeight] = window_.getSize();
    const float aspect = fbHeight != 0 ? static_cast<float>(fbWidth) / static_cast<float>(fbHeight) : 1.0f;

    const glm::mat4 view = camera_.getViewMatrix();
    const glm::mat4 projection = camera_.getProjectionMatrix(aspect);

    // View/projection and lighting are scene-level state, constant across
    // every node/mesh Model::draw() below is about to issue -- set once per
    // frame on the (one, shared) shader program rather than re-set inside
    // the per-node/per-mesh loop. GL uniform values live on the program
    // object itself and aren't disturbed by the repeated glUseProgram calls
    // each Material::bind() makes as Model::draw() walks the scene, so this
    // is safe to set just once here.
    shader_->use();
    shader_->setMat4("uView", view);
    shader_->setMat4("uProjection", projection);
    shader_->setVec3("uLightDirection", kLightDirection);
    shader_->setVec3("uLightColor", kLightColor);
    shader_->setVec3("uAmbientColor", kAmbientColor);
    shader_->setVec3("uViewPos", camera_.position());

    // Each entity's transform matrix is the "rootTransform" Model::draw()
    // composes above the file's own node hierarchy: draw() recurses through
    // the model's node tree, uploading uModel/uNormalMatrix per node as
    // entity.transform * (accumulated parent node transform) * (node's own
    // local transform), and binding + drawing each node's mesh(es) with
    // their own Material. This phase's scene is exactly one Entity, but
    // iterating entities_ (rather than drawing one hardcoded model_)
    // establishes the pattern for however many later phases add.
    for (const Entity& entity : entities_) {
        if (entity.model()) {
            entity.model()->draw(*shader_, entity.transform.getModelMatrix());
        }
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
        if (maxFrames_ != 0 && frameCount_ >= maxFrames_) {
            LOG_INFO("Reached max frame count, exiting main loop");
            break;
        }

        window_.pollEvents();

        const double currentTime = glfwGetTime();
        const double deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        // Polled once per frame, right after pollEvents() (same timing
        // real keyboard/mouse reads always had) and threaded down through
        // update() to whatever needs it (currently just Camera) -- see
        // input.hpp. ESC is read from this same snapshot (input.escapePressed)
        // rather than Application calling window_.isKeyPressed() directly, so
        // Application -- like Camera since Phase 6 -- never reaches into
        // Window/GLFW key constants itself; InputState is the one place that
        // does.
        const InputState input = pollInputState(window_);
        if (input.escapePressed) {
            LOG_INFO("ESC pressed, exiting main loop");
            break;
        }

        update(deltaTime, input);
        render();
        window_.swapBuffers();

        ++frameCount_;
        std::this_thread::sleep_for(kFrameThrottle);
    }

    LOG_INFO("Exited main loop after " + std::to_string(frameCount_) + " frame(s), " +
              std::to_string(totalTime_) + "s total");
}

}  // namespace engine
