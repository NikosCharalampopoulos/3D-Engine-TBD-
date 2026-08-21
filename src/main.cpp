// Phase 0 placeholder entry point.
//
// Purpose: prove the whole toolchain works end to end -- CMake FetchContent
// pulling GLFW + GLM, a GL 3.3 core context created via GLFW, function
// pointers resolved through the vendored loader, a single frame cleared to
// a distinct color and presented, then a clean exit. Real windowing, input,
// and a persistent render loop are Phase 1's job, not this one's.
//
// Runs headlessly under Xvfb + Mesa llvmpipe software rasterization; see
// tools/run_headless.sh and README.md for the exact invocation.

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}  // namespace

int main() {
    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    // Visible (not hidden) so an X server (e.g. Xvfb) actually maps a window
    // we can screenshot from the outside for the headless proof-of-life.
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    const int width = 800;
    const int height = 600;
    GLFWwindow* window =
        glfwCreateWindow(width, height, "3D Engine - Phase 0", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window (no GL context available?)\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to load OpenGL functions via glad\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::printf("OpenGL vendor:   %s\n", glGetString(GL_VENDOR));
    std::printf("OpenGL renderer: %s\n", glGetString(GL_RENDERER));
    std::printf("OpenGL version:  %s\n", glGetString(GL_VERSION));

    glViewport(0, 0, width, height);

    // Cornflower blue -- the traditional "yes, the GL context actually
    // works" clear color, in [0,1] float form. GLM is included/used here
    // just to prove it links; real math usage starts in later phases.
    const glm::vec4 clear_color(0.3921f, 0.5843f, 0.9294f, 1.0f);

    // A handful of frames (not an unbounded loop) so this proves the
    // clear/swap/poll pipeline without ever needing an external kill signal
    // during headless verification. A small per-frame delay keeps the
    // window on screen long enough for an external screenshot tool to
    // capture it (there's no vsync to throttle us under Xvfb/llvmpipe).
    const int frame_count = 5;
    for (int frame = 0; frame < frame_count && !glfwWindowShouldClose(window); ++frame) {
        glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glfwSwapBuffers(window);
        glfwPollEvents();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::printf("Rendered %d frame(s) successfully. Exiting.\n", frame_count);

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
