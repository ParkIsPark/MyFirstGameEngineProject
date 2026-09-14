#pragma once
#include <GL/glew.h>

// CPU pointers (including nullptr storage allocation) must never be interpreted
// as offsets into a caller PBO. Restore all baseline 3.3 unpack layout state.
class FPixelUnpackGuard
{
public:
    FPixelUnpackGuard()
    {
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer_);
        for (int i = 0; i < 6; ++i) glGetIntegerv(fields_[i], &values_[i]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        for (int i = 0; i < 6; ++i) glPixelStorei(fields_[i], i == 0 ? 1 : 0);
    }
    ~FPixelUnpackGuard()
    {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer_));
        for (int i = 0; i < 6; ++i) glPixelStorei(fields_[i], values_[i]);
    }
    FPixelUnpackGuard(const FPixelUnpackGuard&) = delete;
    FPixelUnpackGuard& operator=(const FPixelUnpackGuard&) = delete;
private:
    const GLenum fields_[6] = {GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH,
        GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_IMAGES};
    GLint buffer_ = 0, values_[6] = {};
};
