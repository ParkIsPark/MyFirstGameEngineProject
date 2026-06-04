#include "USkyHDRI.h"

#include <GL/glew.h>
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION          // single definition for the whole engine
#include "stb_image.h"

unsigned int USkyHDRI::GetOrLoad(const std::string& path)
{
    if (path == loadedPath_) return tex_;     // already current (incl. both empty)

    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    loadedPath_ = path;
    if (path.empty()) return 0;

    // .hdr -> linear float RGB. Flip so the image top maps to v=1 (sky up).
    stbi_set_flip_vertically_on_load(1);
    int w = 0, h = 0, n = 0;
    float* data = stbi_loadf(path.c_str(), &w, &h, &n, 3);
    if (!data)
    {
        std::printf("[Sky] failed to load HDRI '%s'\n", path.c_str());
        loadedPath_.clear();
        return 0;
    }

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);          // equirect wraps in u
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    std::printf("[Sky] loaded HDRI '%s' (%dx%d)\n", path.c_str(), w, h);
    return tex_;
}

void USkyHDRI::Cleanup()
{
    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    loadedPath_.clear();
}
