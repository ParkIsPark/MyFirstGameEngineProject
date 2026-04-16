#include "USurface.h"
#include "AActor.h"

glm::vec3 USurface::getPosition() const
{
    return owner ? owner->position : glm::vec3(0.0f);
}
#include <GL/glew.h>
#include <iostream>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include "../../include/stb_image.h"

void USurface::SetTexture(const char* filePath)
{
    unsigned int id;
    glGenTextures(1, &id);

    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(filePath, &w, &h, &ch, 0);
    if (data)
    {
        material.texData.assign(data, data + w * h * ch);
        material.texWidth    = w;
        material.texHeight   = h;
        material.texChannels = ch;

        GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        material.texture = id;
    }
    else
    {
        std::cout << "Texture load failed: " << filePath << "\n";
    }
    stbi_image_free(data);
}

glm::vec3 USurface::getDiffuseColor(const glm::vec3& hitPoint) const
{
    if (material.texData.empty())
        return material.kd;

    glm::vec2 uv = getUV(hitPoint);

    float u = uv.x - std::floor(uv.x);
    float v = uv.y - std::floor(uv.y);

    int px = static_cast<int>(u * material.texWidth);
    int py = static_cast<int>(v * material.texHeight);
    px = glm::clamp(px, 0, material.texWidth  - 1);
    py = glm::clamp(py, 0, material.texHeight - 1);

    int idx = (py * material.texWidth + px) * material.texChannels;
    float r = material.texData[idx + 0] / 255.0f;
    float g = material.texData[idx + 1] / 255.0f;
    float b = material.texData[idx + 2] / 255.0f;
    return glm::vec3(r, g, b);
}
