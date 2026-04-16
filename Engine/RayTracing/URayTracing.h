#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

class UScene;
class ACamera;
class AActor;
class ALight;
struct URay;
class IntersectionPass;
class LightingPass;

class URayTracing
{
public:
    URayTracing();
    ~URayTracing();

    // ------------------------------------------------------------------
    // GPU renderer lifecycle
    //   Init()        — assemble shader from passes, compile, build quad
    //   RenderFrame() — upload uniforms via passes, draw
    //   Cleanup()     — delete GL objects
    // ------------------------------------------------------------------
    void Init(const UScene& scene);
    void RenderFrame(const UScene& scene, const ACamera& cam);
    void Cleanup();

    // ------------------------------------------------------------------
    // CPU ray tracer
    //   mode 0 = Q1 (white/black intersection test)
    //   mode 1 = Q2 (Phong shading + shadow rays)
    // ------------------------------------------------------------------
    void Render(UScene& scene, ACamera& camera, int mode) const;

    glm::vec3 TraceQ3(const URay& ray, const UScene& scene,
        const ACamera& camera, int depth = 0) const;

private:
    // ------------------------------------------------------------------
    // GPU state  (unsigned int == GLuint)
    // ------------------------------------------------------------------
    unsigned int prog_ = 0;
    unsigned int vao_  = 0;
    unsigned int vbo_  = 0;

    std::unique_ptr<IntersectionPass> intersectionPass_;
    std::unique_ptr<LightingPass>     lightingPass_;

    void uploadCameraUniforms(const ACamera& cam, const UScene& scene) const;

    // ------------------------------------------------------------------
    // CPU ray tracing internals
    // ------------------------------------------------------------------
    bool      FindClosestHit(const URay& ray, const UScene& scene,
                             float& outT, const AActor*& outActor) const;

    glm::vec3 Trace(const URay& ray, const UScene& scene,
                    const ACamera& camera, int mode) const;

    glm::vec3 TraceQ1(const URay& ray, const UScene& scene) const;
    glm::vec3 TraceQ2(const URay& ray, const UScene& scene,
                      const ACamera& camera) const;

    glm::vec3 ComputeLightingQ3(const glm::vec3& hitPoint, const glm::vec3& normal,
                                const AActor* actor, const URay& ray,
                                const UScene& scene, const ACamera& camera,
                                int depth) const;
};
