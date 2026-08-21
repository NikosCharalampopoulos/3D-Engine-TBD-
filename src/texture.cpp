#include "engine/texture.hpp"

#include <glad/glad.h>

// This is the one translation unit in the whole project that defines
// STB_IMAGE_IMPLEMENTATION -- stb_image.h is a single-header library where
// exactly one .cpp must enable the implementation before including it (every
// other includer just gets the declarations). Grepping the tree for this
// macro before adding it here confirmed no other file already defines it.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>
#include <string>

#include "engine/gl_debug.hpp"
#include "engine/log.hpp"

namespace engine {

namespace {

// Maps stb_image's returned channel count to the matching GL format enum.
// stb_image reports exactly what was in the source file when asked for
// `desired_channels = 0` (as Texture does below) -- 1 (grayscale), 2
// (grayscale+alpha), 3 (RGB), or 4 (RGBA). Hardcoding GL_RGB regardless of
// this is a classic bug: uploading an RGBA source as if it were 3
// bytes/pixel reads one byte short per pixel, which shears/shifts every
// row's colors (and reads past the buffer's end reconstructing the last
// one) -- corrupted output at best, a crash at worst.
GLenum glFormatForChannels(int channels, const std::string& path) {
    switch (channels) {
        case 1:
            return GL_RED;
        case 2:
            return GL_RG;
        case 3:
            return GL_RGB;
        case 4:
            return GL_RGBA;
        default:
            throw std::runtime_error("Texture: unsupported channel count (" + std::to_string(channels) +
                                      ") in " + path);
    }
}

}  // namespace

Texture::Texture(const std::string& path, bool generateMipmaps) {
    // GL samples texture (0,0) at the bottom-left, but image file formats
    // conventionally store rows top-to-bottom; flipping on load (stb_image's
    // own knob for this, applied globally before the load call) is the
    // standard fix so a mesh's UV origin lines up with the image the way
    // it's viewed, rather than rendering upside down.
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    // desired_channels = 0 means "give me whatever the file actually has" --
    // channels is populated with the real source channel count so the
    // upload format below always matches it instead of assuming RGB.
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 0);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        LOG_ERROR("Texture: failed to load \"" + path +
                   "\": " + (reason != nullptr ? std::string(reason) : std::string("unknown error")));
        throw std::runtime_error("Texture: failed to load image: " + path);
    }

    width_ = width;
    height_ = height;
    channels_ = channels;

    GLenum format = GL_RGB;
    try {
        format = glFormatForChannels(channels_, path);
    } catch (...) {
        stbi_image_free(pixels);
        throw;
    }

    GL_CHECK(glGenTextures(1, &textureId_));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureId_));

    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                              generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR));

    // GL_UNPACK_ALIGNMENT defaults to 4: glTexImage2D assumes each row of the
    // source buffer starts on a 4-byte boundary and pads accordingly when
    // reading it. stbi_load returns pixels tightly packed with no such
    // padding, so whenever a row isn't already a multiple of 4 bytes (e.g.
    // any odd-width GL_RED/GL_RGB image -- checker.png's 256*3=768 happens to
    // be a multiple of 4, which is exactly why this bug class is easy to
    // miss on this asset) GL misreads row boundaries and every row after the
    // first is shifted, corrupting the whole image on upload. Setting it to
    // 1 tells GL the buffer is tightly packed, which is always correct
    // regardless of width/channel count. Restored to the GL default
    // afterwards so this call doesn't leak altered global pixel-store state
    // into unrelated GL calls made elsewhere (e.g. a future glReadPixels).
    GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), width_, height_, 0, format,
                           GL_UNSIGNED_BYTE, pixels));
    GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 4));
    if (generateMipmaps) {
        GL_CHECK(glGenerateMipmap(GL_TEXTURE_2D));
    }

    GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
    stbi_image_free(pixels);

    LOG_INFO("Texture loaded: " + path + " (" + std::to_string(width_) + "x" + std::to_string(height_) +
              ", " + std::to_string(channels_) + " channel(s))");
}

Texture::~Texture() {
    if (textureId_ != 0) {
        glDeleteTextures(1, &textureId_);
    }
}

Texture::Texture(Texture&& other) noexcept
    : textureId_(other.textureId_), width_(other.width_), height_(other.height_), channels_(other.channels_) {
    other.textureId_ = 0;
    other.width_ = 0;
    other.height_ = 0;
    other.channels_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        if (textureId_ != 0) {
            glDeleteTextures(1, &textureId_);
        }

        textureId_ = other.textureId_;
        width_ = other.width_;
        height_ = other.height_;
        channels_ = other.channels_;

        other.textureId_ = 0;
        other.width_ = 0;
        other.height_ = 0;
        other.channels_ = 0;
    }
    return *this;
}

void Texture::bind(unsigned int unit) const {
    GL_CHECK(glActiveTexture(GL_TEXTURE0 + unit));
    GL_CHECK(glBindTexture(GL_TEXTURE_2D, textureId_));
}

}  // namespace engine
