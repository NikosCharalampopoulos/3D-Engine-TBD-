#ifndef ENGINE_TEXTURE_HPP
#define ENGINE_TEXTURE_HPP

// RAII wrapper around a single GL 2D texture object, loaded from an image
// file on disk via stb_image (external/stb/stb_image.h -- src/texture.cpp
// is the one translation unit in the whole project that
// #defines STB_IMAGE_IMPLEMENTATION, per stb's single-header convention;
// every other file that wants stbi_* declarations just includes the header
// normally).
//
// Move-only, same rationale as Shader/Mesh: a GL texture name is a scarce
// handle owned by exactly one Texture. Copying would let two destructors
// glDeleteTextures the same name (double free / use-after-delete of the
// name once one instance dies), so copy is disabled and move transfers
// ownership, zeroing the moved-from Texture's id so its destructor is a
// no-op.

#include <string>

namespace engine {

class Texture {
public:
    // path is a file path (e.g. under assets/textures/) read relative to
    // the process's current working directory -- same convention as
    // Shader's vertexPath/fragmentPath. Throws std::runtime_error if the
    // file can't be found or decoded (stbi_load returning null) rather than
    // uploading garbage/uninitialized data to the GPU.
    //
    // generateMipmaps controls whether a full mipmap chain is built (and
    // trilinear-filtered minification used) or only the base level is
    // uploaded with plain bilinear minification -- true is the sane default
    // for a texture that will be viewed at varying distances.
    explicit Texture(const std::string& path, bool generateMipmaps = true);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Makes this texture the active one on the given texture unit (0-based,
    // i.e. GL_TEXTURE0 + unit) and binds it as a GL_TEXTURE_2D there.
    // Callers are responsible for pointing the shader's sampler uniform at
    // the same unit (Shader::setInt(name, unit)) -- this mirrors Shader's
    // "no hidden uniform state" style rather than baking a unit choice in
    // here.
    void bind(unsigned int unit = 0) const;

    int width() const { return width_; }
    int height() const { return height_; }
    // Number of color channels stb_image actually found in the source file
    // (1 = grayscale, 2 = grayscale+alpha, 3 = RGB, 4 = RGBA) -- exposed
    // mainly so callers/logging can confirm the loader picked the right GL
    // upload format rather than silently assuming RGB.
    int channels() const { return channels_; }
    unsigned int id() const { return textureId_; }

private:
    unsigned int textureId_ = 0;
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
};

}  // namespace engine

#endif  // ENGINE_TEXTURE_HPP
