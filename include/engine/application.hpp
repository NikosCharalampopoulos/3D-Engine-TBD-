#ifndef ENGINE_APPLICATION_HPP
#define ENGINE_APPLICATION_HPP

// Owns a Window and drives the main loop: poll events -> update -> render
// -> swap, once per frame, until the window should close or ESC is
// pressed. Computes delta-time and a frame count each iteration -- nothing
// consumes them yet, but later phases (animation, physics, camera
// movement, FPS display) will need exactly this, so the pattern is
// established here rather than bolted on later. Deliberately does not grow
// into a scene graph / ECS / callback system -- that's later phases' job.
//
// Phase 2 adds the first real rendered content: render() draws a shaded
// cube via a Shader + Mesh instead of just clearing the framebuffer. There's
// no Camera yet (that's Phase 3), so the view/projection matrices are built
// by hand each frame from a hardcoded eye position + GLM's perspective() --
// see render()'s definition for the "replace this with Camera" note.

#include <cstdint>
#include <string>

#include "engine/mesh.hpp"
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
    void update(double deltaTime);
    void render();

    // Declaration order matters here: window_ must construct (and create
    // the GL context) before shader_/cube_ since both do GL calls in their
    // constructors.
    Window window_;
    Shader shader_;
    Mesh cube_;
    std::uint64_t maxFrames_;
    std::uint64_t frameCount_ = 0;
    double totalTime_ = 0.0;
};

}  // namespace engine

#endif  // ENGINE_APPLICATION_HPP
