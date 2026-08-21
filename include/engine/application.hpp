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
//
// Phase 7a extends the lighting/rendering pipeline without touching the
// main-loop shape above:
//   - Phase 4's single hardcoded directional light is joined by a small
//     fixed set of point lights and a spot light (kPointLights/kSpotLights
//     in application.cpp), uploaded to the shader as fixed-size uniform
//     arrays + a live count each frame -- see basic.frag.
//   - A second render target, shadowMap_ (see shadow_map.hpp), is rendered
//     into once per frame *before* the main pass: the whole scene, drawn
//     depth-only from the directional light's point of view via a second,
//     minimal shader (shadowShader_). The main pass then samples that depth
//     texture per-fragment to reduce the directional light's own
//     contribution for shadowed fragments (see renderShadowPass()/render()
//     and basic.frag's shadowFactor()).
//   - A hand-built ground plane (groundMesh_/groundMaterial_, not part of
//     scene.obj/Model) is drawn alongside entities_ specifically so this
//     phase has one demo surface with a bound normal map (see mesh.hpp's
//     makeGroundPlane()) independent of scene.obj's own (still
//     normal-map-less) materials.
//
// Phase 7b adds a skybox background and an HDR + tonemapping pipeline,
// without changing the shape of the loop above:
//   - render() no longer draws the lit scene straight to the default
//     framebuffer: it renders into hdrFramebuffer_ (see framebuffer.hpp), a
//     floating-point (GL_RGBA16F) off-screen target, then resolves that to
//     the window with one fullscreen post-process pass
//     (postProcessShader_ + postProcessQuad_, assets/shaders/
//     postprocess.vert/.frag) that Reinhard-tonemaps and gamma-corrects.
//     This is what lets kPointLights[0] (application.cpp) carry an
//     intensity above 1.0 and still roll off smoothly near the light
//     instead of hard-clipping to a flat white disc the way writing
//     straight to the (8-bit, clamped) default framebuffer would.
//   - skybox_ (see skybox.hpp) is drawn as the scene's background: a
//     6-face procedural-sky cubemap, rendered LAST into hdrFramebuffer_
//     (after entities_/the ground plane) via a GL_LEQUAL depth trick so it
//     only shows through pixels nothing else was drawn to, replacing the
//     old flat kClearR/G/B glClearColor as what's actually visible behind
//     the scene (that glClearColor call still runs first, as a safety
//     fallback in case the skybox draw is ever skipped/fails).

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/camera.hpp"
#include "engine/entity.hpp"
#include "engine/framebuffer.hpp"
#include "engine/input.hpp"
#include "engine/material.hpp"
#include "engine/mesh.hpp"
#include "engine/resource_manager.hpp"
#include "engine/shader.hpp"
#include "engine/shadow_map.hpp"
#include "engine/skybox.hpp"
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

    // Phase 7a: renders the whole scene depth-only from the directional
    // light's point of view into shadowMap_ (see shadow_map.hpp), using
    // shadowShader_. Called once per frame from render(), before the main
    // pass, and restores the window's own viewport (and default framebuffer
    // binding) before returning, since ShadowMap::bindForWriting() points
    // the viewport at the shadow map's own (typically much smaller)
    // resolution.
    void renderShadowPass(const glm::mat4& lightSpaceMatrix);

    // Declaration order matters here: window_ must construct (and create
    // the GL context) before resources_ is used to load anything (Shader/
    // Model/ShadowMap/Mesh construction all do GL calls) -- resources_
    // itself does no GL work at construction (its caches start empty), but
    // shader_ is initialized from it in the constructor's member-
    // initializer list, so resources_ must still be declared (and thus
    // constructed) before shader_. shadowShader_/skyboxShader_/
    // postProcessShader_ likewise need resources_ constructed first.
    // shader_ must in turn come before entities_ and groundMaterial_:
    // building the scene's Entity calls resources_.getModel(path,
    // *shader_), and each resulting Model's per-mesh Materials (plus
    // groundMaterial_, built directly against shader_ rather than through
    // Model) hold a pointer into that same shader_, so shader_ must
    // outlive all of them. hdrFramebuffer_ is sized from window_.getSize()
    // in its own initializer, so it too must come after window_ (anywhere
    // after is fine -- it has no other dependency). camera_ does no GL
    // work so its position relative to the above is unconstrained.
    Window window_;
    ResourceManager resources_;
    std::shared_ptr<Shader> shader_;
    // Phase 7a: a second, minimal program (assets/shaders/shadow.vert/
    // shadow.frag) used only to render shadowMap_'s depth pass -- see
    // renderShadowPass(). Routed through resources_ like shader_, even
    // though nothing else currently requests the same (vertex, fragment)
    // pair, for the same reason every other asset load goes through the
    // cache: one consistent loading path, not because sharing is expected
    // here.
    std::shared_ptr<Shader> shadowShader_;
    // Phase 7b: the skybox's own program (assets/shaders/skybox.vert/
    // skybox.frag) and the HDR-resolve fullscreen pass's program
    // (assets/shaders/postprocess.vert/postprocess.frag) -- both routed
    // through resources_ for the same "one consistent loading path" reason
    // as shadowShader_ above.
    std::shared_ptr<Shader> skyboxShader_;
    std::shared_ptr<Shader> postProcessShader_;
    // Phase 7a: the directional light's depth-only render target. Fixed
    // resolution (see application.cpp's kShadowMapWidth/Height), independent
    // of the window's own framebuffer size.
    ShadowMap shadowMap_;
    // Phase 7b: the off-screen floating-point target render() draws the
    // whole lit scene (+ skybox) into, before postProcessShader_ resolves
    // it to the window -- see framebuffer.hpp. Sized once, from the
    // window's real framebuffer size at construction time; Window has no
    // resize callback/event of its own for this to react to (see
    // window.hpp), so -- like every other fixed-at-construction GL
    // resource in this engine -- this only ever needs to be sized once.
    Framebuffer hdrFramebuffer_;
    // Phase 7b: the procedural-sky cubemap background -- see skybox.hpp.
    // Loads its own 6 face images directly (not through resources_/Texture,
    // see skybox.hpp's class comment on why it's a separate small class).
    Skybox skybox_;
    // Phase 7a: a hand-built ground plane (see mesh.hpp's makeGroundPlane())
    // with a bound normal map, drawn directly alongside entities_ rather
    // than through Model/scene.obj -- see this header's Phase 7a comment
    // above for why. Constructed directly as members (not wrapped in an
    // Entity) since there's exactly one and Entity only knows how to hold a
    // Model, not a raw Mesh + Material pair.
    Mesh groundMesh_;
    Material groundMaterial_;
    // Phase 7b: the fullscreen quad the HDR-resolve pass draws (see
    // mesh.hpp's makeFullscreenQuad()) -- built once here, like groundMesh_,
    // rather than re-built every frame.
    Mesh postProcessQuad_;
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
