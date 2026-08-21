#ifndef ENGINE_APPLICATION_HPP
#define ENGINE_APPLICATION_HPP

// Owns a Window and drives the main loop: poll events -> update -> render
// -> swap, once per frame, until the window should close or ESC is
// pressed. Computes delta-time and a frame count each iteration -- nothing
// consumes them yet, but later phases (animation, physics, camera
// movement, FPS display) will need exactly this, so the pattern is
// established here rather than bolted on later. Phase 6 adds the smallest
// possible "things in the scene are enumerable" structure (entities_, see
// entity.hpp) but deliberately does not grow into a full scene graph / real
// ECS / callback system -- that's later phases' job if/when it's needed.
//
// Phase 2 added the first real rendered content: render() draws a shaded
// cube via a Shader + Mesh instead of just clearing the framebuffer.
//
// Phase 3 replaces Phase 2's hardcoded eye position + hand-rolled view/
// projection matrices with a real Camera (view/projection) and Transform
// (the cube's model matrix). update() now feeds delta-time and input into
// camera_ every frame -- either real WASD/mouse input via Window, or (when
// ENGINE_CAMERA_DEMO is set) a small deterministic scripted path, since
// there's no real keyboard/mouse under the headless Xvfb verification
// harness. See update()'s definition for both paths.
//
// Phase 4 replaces Phase 2-3's flat per-face uColor rendering with a real
// Material (Shader + Texture + tint/shininess) and Blinn-Phong directional
// lighting: render() now uploads separate model/view/projection + normal
// matrices (instead of one combined MVP) plus light uniforms, and issues a
// single whole-mesh draw() instead of drawRange() per face, since the
// cube's six flat uColor faces are gone in favor of one textured, lit
// surface.
//
// Phase 5 replaces the Phase 2-4 hardcoded single cube with a real Model
// (Assimp-loaded scene graph, see model.hpp): render() now calls
// model_.draw(), which recursively composes each node's local transform
// with its parent's and uploads uModel/uNormalMatrix per node instead of
// once per frame. sceneTransform_ is an outer Transform placed above the
// whole model's own node hierarchy (a small fixed rotation, the same "prove
// the composition order is right, not just that it compiles" role
// cubeTransform_'s fixed rotation played in Phase 3-4) so the render path
// exercises at least two composed levels: sceneTransform_ * (accumulated
// parent node transform) * (node's own local transform). Camera and
// lighting are otherwise unchanged from Phase 3/4.
//
// Phase 6 restructures how all of the above is owned, without changing what
// gets rendered:
//   - shader_/model_'s previous ad-hoc construction (each loaded directly
//     in this class's constructor) is replaced by resources_ (a
//     ResourceManager): both are now requested via resources_.getShader()/
//     getModel(), which load on first request and hand back the same
//     cached instance on any later request for the same asset -- see
//     resource_manager.hpp.
//   - Phase 5's single model_ + sceneTransform_ pair (two loose members
//     that only worked for exactly one object) becomes entities_, a
//     std::vector<Entity>: each Entity bundles a Transform with a
//     (possibly shared, possibly null) Model -- see entity.hpp. This phase
//     only ever puts one Entity in it, but render() iterates entities_
//     rather than assuming exactly one, establishing the "a scene is an
//     enumerable list of things" pattern a later phase's real scene
//     graph/ECS can build on.
//   - Camera no longer reads Window directly for keyboard/mouse state:
//     Application::run() polls an InputState once per frame (see
//     input.hpp) and passes it down to update()/Camera, rather than Camera
//     calling Window::isKeyPressed() itself the way Phase 3-5's
//     processKeyboard(const Window&, float) did.
// Camera movement, lighting, and the rendered scene itself are otherwise
// unchanged from Phase 5.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/camera.hpp"
#include "engine/entity.hpp"
#include "engine/input.hpp"
#include "engine/resource_manager.hpp"
#include "engine/shader.hpp"
#include "engine/window.hpp"

namespace engine {

class Application {
public:
    // width/height/title are passed straight through to the Window.
    //
    // maxFrames == 0 (the default) means run until the window should close
    // or ESC is pressed -- normal interactive behavior. A nonzero value
    // makes run() return on its own after that many frames; this exists
    // solely so the headless verification path (Xvfb has no real window
    // manager or keyboard to close the window / send ESC) can terminate
    // deterministically instead of relying on an external timeout/kill. See
    // main.cpp's ENGINE_MAX_FRAMES handling.
    Application(int width, int height, const std::string& title, std::uint64_t maxFrames = 0);

    void run();

private:
    void update(double deltaTime, const InputState& input);
    void render();

    // Declaration order matters here: window_ must construct (and create
    // the GL context) before resources_ is used to load anything (Shader/
    // Model construction both do GL calls) -- resources_ itself does no GL
    // work at construction (its caches start empty), but shader_ is
    // initialized from it in the constructor's member-initializer list, so
    // resources_ must still be declared (and thus constructed) before
    // shader_. shader_ must in turn come before entities_: building the
    // scene's Entity calls resources_.getModel(path, *shader_), and each
    // resulting Model's per-mesh Materials hold a pointer into that same
    // shader_, so shader_ must outlive every entity in entities_. camera_
    // does no GL work so its position relative to the above is
    // unconstrained.
    Window window_;
    ResourceManager resources_;
    std::shared_ptr<Shader> shader_;
    std::vector<Entity> entities_;
    Camera camera_;
    std::uint64_t maxFrames_;
    std::uint64_t frameCount_ = 0;
    double totalTime_ = 0.0;

    // Set from the ENGINE_CAMERA_DEMO env var (see application.cpp's demo
    // path in update()'s definition) -- true drives the camera through a
    // scripted, frame-count-based orbit instead of reading Window input,
    // purely so headless verification can prove the free-fly/lookAt math
    // actually moves the camera, not just that it compiles.
    bool cameraDemoMode_ = false;
};

}  // namespace engine

#endif  // ENGINE_APPLICATION_HPP
