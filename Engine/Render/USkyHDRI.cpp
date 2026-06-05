#include "USkyHDRI.h"

#include <GL/glew.h>
#include <cstdio>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION          // single definition for the whole engine
#include "stb_image.h"

unsigned int USkyHDRI::GetOrLoad(const std::string& path)
{
    if (path == loadedPath_) return tex_;     // already current (incl. both empty)

    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    cpu_.clear(); w_ = h_ = 0;
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

    cpu_.assign(data, data + (size_t)w * h * 3);   // keep a CPU copy for the rasterizer
    w_ = w; h_ = h;
    stbi_image_free(data);
    std::printf("[Sky] loaded HDRI '%s' (%dx%d)\n", path.c_str(), w, h);
    return tex_;
}

glm::vec3 USkyHDRI::SampleDir(const glm::vec3& dir) const
{
    if (cpu_.empty()) return glm::vec3(0.0f);
    glm::vec3 d = glm::normalize(dir);
    float u = atan2f(d.z, d.x) * 0.15915494f + 0.5f;            // 1/(2*pi)
    float v = asinf(glm::clamp(d.y, -1.0f, 1.0f)) * 0.31830989f + 0.5f; // 1/pi
    // GL texture was uploaded flipped (top->v=1); match that here.
    v = 1.0f - v;
    int x = glm::clamp((int)(u * w_), 0, w_ - 1);
    int y = glm::clamp((int)(v * h_), 0, h_ - 1);
    const size_t i = ((size_t)y * w_ + x) * 3;
    return glm::vec3(cpu_[i], cpu_[i + 1], cpu_[i + 2]);
}

void USkyHDRI::Cleanup()
{
    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    cpu_.clear(); w_ = h_ = 0;
    loadedPath_.clear();
}
