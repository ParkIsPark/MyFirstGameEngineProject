#pragma once

#include <glm/glm.hpp>

struct URay
{
    glm::vec3 origin;
    glm::vec3 direction;

    URay() {
        origin = glm::vec3(0.0f, 0.0f, 0.0f);
        direction = glm::vec3(0.0f, 0.0f, -1.0f);
    }

    URay(const glm::vec3& inOrigin, const glm::vec3& inDirection) {
        origin = inOrigin;
        direction = inDirection; 
    }
};