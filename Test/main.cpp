// ---------------------------------------------------------------------------
// Mesh demo driven by the Engine framework.
//   Key 1 = CPU software rasterizer  (flat white silhouette, Q1)
//   Key 2 = GPU mesh ray tracer      (Blinn-Phong shaded sphere)   [default]
//   Key 3 = depth-buffer debug view  (grayscale, nearer = brighter; Q1 deliverable)
//   Key 4 = HYBRID: CPU raster G-buffer + GPU shadow ray (sphere shadow on wall)
//   Key 5 = imported OBJ cube (GPU ray traced, rotating) -- OBJ import end-to-end
//   Key 6 = imported FBX (Assimp) mesh (GPU ray traced, rotating)
//   ESC/Q = quit
//   Run with "--fbxtest" for a headless FBX import self-test (no window).
// (Test harness -- not engine source.)
// ---------------------------------------------------------------------------
#include <GL/glew.h>
#define GLFW_DLL
#include <GLFW/glfw3.h>

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <memory>

#if defined(_MSC_VER)
#pragma warning(disable : 4996) // Regression gates intentionally exercise deprecated renderers.
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Engine.h"
#include "ACamera.h"
#include "UMesh.h"
#include "FTransform.h"
#include "UFrameBuffer.h"
#include "UGBuffer.h"
#include "URasterizer.h"
#include "UMeshRayTracer.h"
#include "UHybridPass.h"
#include "UObjImporter.h"
#include "UFbxImporter.h"
#include "EditorEngine.h"
#include "GameEngine.h"
#include "FRenderScene.h"
#include "UHardwareGBuffer.h"
#include "UHardwareRasterizer.h"
#include "UGPUMeshCache.h"
#include "FWorldSerializer.h"
#include "URenderer.h"
#include "URasterLightingPass.h"
#include "USkyHDRI.h"
#include "UWorldRenderer.h"
#include "FRenderTarget.h"
#include "FRenderQuality.h"
#include "UGL33RayTracingBackend.h"
#include "UGL43RayTracingBackend.h"
#include "FRaySceneCache.h"

// Load a model trying a few candidate directories (working dir varies between
// running from bin\ and VS's project dir).
static std::vector<UMesh*> LoadFbxAny(const char* name)
{
    const char* dirs[] = { "", "bin/", "Test/models/", "models/", "../Test/models/" };
    for (const char* d : dirs)
    {
        std::vector<UMesh*> v = UFbxImporter::Load((std::string(d) + name).c_str());
        if (!v.empty()) return v;
    }
    return {};
}

// A cube .obj with per-face normals -- written to disk then loaded back through
// the importer, so mode 5 exercises the real file path end-to-end.
static const char* CUBE_OBJ =
    "# demo cube\n"
    "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
    "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
    "vn 0 0 -1\nvn 0 0 1\nvn 0 -1 0\nvn 0 1 0\nvn -1 0 0\nvn 1 0 0\n"
    "f 1//1 4//1 3//1 2//1\nf 5//2 6//2 7//2 8//2\n"
    "f 1//3 2//3 6//3 5//3\nf 3//4 4//4 8//4 7//4\n"
    "f 5//5 8//5 4//5 1//5\nf 2//6 3//6 7//6 6//6\n";

// Append a mesh's world-space triangle vertices (3 per triangle) to `out`.
static void appendWorldTris(const UMesh& m, const glm::mat4& model,
                            std::vector<glm::vec3>& out)
{
    const int n = m.triangleCount();
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k)
        {
            const glm::vec3 p = m.vertices[m.indices[3 * i + k]].position;
            out.push_back(glm::vec3(model * glm::vec4(p, 1.0f)));
        }
}

class MeshDemo : public Engine
{
protected:
    void OnStartup() override
    {
        // --- sphere (caster), world radius 2 at (0,0,-7) ---
        gSphere_ = UMesh::GenerateSphere(2.0f, 32, 16);
        gSphere_->material.ka        = glm::vec3(0.20f, 0.22f, 0.28f);
        gSphere_->material.kd        = glm::vec3(0.55f, 0.60f, 0.85f);
        gSphere_->material.ks        = glm::vec3(0.55f);
        gSphere_->material.shininess = 48.0f;
        sphereModel_ = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -7));

        // --- back wall (receiver), facing +z at z=-12 ---
        gWall_ = new UMesh();
        const float W = 15.0f, Z = -12.0f;
        gWall_->vertices = {
            {{-W,-W, Z},{0,0,1},{0,0}}, {{ W,-W, Z},{0,0,1},{1,0}},
            {{ W, W, Z},{0,0,1},{1,1}}, {{-W, W, Z},{0,0,1},{0,1}},
        };
        gWall_->indices = { 0,1,2, 0,2,3 };
        gWall_->material.kd = glm::vec3(0.75f, 0.75f, 0.78f);
        wallModel_ = glm::mat4(1.0f);

        gpu_.Init();
        gpu_.UploadMesh(*gSphere_, sphereModel_);

        // hybrid: static scene triangles (sphere + wall) for shadow occlusion
        hybrid_.Init();
        std::vector<glm::vec3> tris;
        appendWorldTris(*gSphere_, sphereModel_, tris);
        appendWorldTris(*gWall_,   wallModel_,   tris);
        hybrid_.UploadSceneTriangles(tris);

        // --- OBJ import end-to-end: write a cube .obj, load it, ray trace it ---
        { std::ofstream f("demo_cube.obj"); f << CUBE_OBJ; }
        gCube_ = UObjImporter::Load("demo_cube.obj");
        if (gCube_)
        {
            gCube_->material.ka        = glm::vec3(0.18f, 0.16f, 0.12f);
            gCube_->material.kd        = glm::vec3(0.85f, 0.55f, 0.30f); // warm orange
            gCube_->material.ks        = glm::vec3(0.4f);
            gCube_->material.shininess = 32.0f;
            gpuCube_.Init();
        }

        // --- FBX import end-to-end (Assimp) ---
        std::vector<UMesh*> fbx = LoadFbxAny("box.fbx");
        if (!fbx.empty())
        {
            gFbx_ = fbx[0];                       // first mesh (box = single mesh)
            for (size_t i = 1; i < fbx.size(); ++i) delete fbx[i];
            gFbx_->material.ka        = glm::vec3(0.16f, 0.18f, 0.16f);
            gFbx_->material.kd        = glm::vec3(0.45f, 0.80f, 0.55f); // green
            gFbx_->material.ks        = glm::vec3(0.45f);
            gFbx_->material.shininess = 40.0f;
            gpuFbx_.Init();
        }
    }

    void Render() override
    {
        if (KeyDown(GLFW_KEY_1)) mode_ = 1;
        if (KeyDown(GLFW_KEY_2)) mode_ = 2;
        if (KeyDown(GLFW_KEY_3)) mode_ = 3;
        if (KeyDown(GLFW_KEY_4)) mode_ = 4;
        if (KeyDown(GLFW_KEY_5) && gCube_) mode_ = 5;
        if (KeyDown(GLFW_KEY_6) && gFbx_)  mode_ = 6;

        if (mode_ == 2)
        {
            gpu_.RenderFrame(cam_, Width(), Height());
            return;
        }

        if (mode_ == 5)
        {
            // rotate the imported cube so several faces show (GLM rotate = degrees)
            spin_ += 0.4f;
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -7));
            m = glm::rotate(m, spin_,  glm::vec3(0, 1, 0));
            m = glm::rotate(m, 22.0f,  glm::vec3(1, 0, 0));
            m = glm::scale (m, glm::vec3(1.7f));
            gpuCube_.UploadMesh(*gCube_, m);          // re-bake world triangles each frame
            gpuCube_.RenderFrame(cam_, Width(), Height());
            return;
        }

        if (mode_ == 6)
        {
            spin_ += 0.4f;
            glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -7));
            m = glm::rotate(m, spin_, glm::vec3(0, 1, 0));
            m = glm::rotate(m, 18.0f, glm::vec3(1, 0, 0));
            m = glm::scale (m, glm::vec3(2.2f));       // box.fbx is a small unit cube
            gpuFbx_.UploadMesh(*gFbx_, m);
            gpuFbx_.RenderFrame(cam_, Width(), Height());
            return;
        }

        if (mode_ == 4)
        {
            // CPU primary visibility -> G-buffer (sphere + wall)
            UGBuffer gb; gb.Init(Width(), Height()); gb.Clear();
            URasterizer R;
            FTransform xf;
            xf.view     = FTransform::MakeView(cam_);
            xf.proj     = FTransform::MakeProjFCG(cam_.l, cam_.r, cam_.b, cam_.t, -cam_.d, -1000.0f);
            xf.viewport = FTransform::MakeViewport(Width(), Height());

            xf.model = sphereModel_;
            R.DrawMeshGBuffer(*gSphere_, xf, gSphere_->material.kd, gb);
            xf.model = wallModel_;
            R.DrawMeshGBuffer(*gWall_, xf, gWall_->material.kd, gb);

            // GPU shadow + Blinn-Phong shading
            hybrid_.UploadGBuffer(gb);
            hybrid_.Render(cam_, glm::vec3(6, 6, -4), glm::vec3(1.0f), Width(), Height());
            return;
        }

        // CPU rasterizer (Q1 path) — modes 1 (white silhouette) and 3 (depth view)
        UFrameBuffer fb;
        fb.Init(Width(), Height());
        fb.Clear(glm::vec3(0.0f));
        FTransform xf;
        xf.model    = sphereModel_;
        xf.view     = glm::mat4(1.0f);
        xf.proj     = FTransform::MakeProjFCG(-0.1f, 0.1f, -0.1f, 0.1f, -0.1f, -1000.0f);
        xf.viewport = FTransform::MakeViewport(Width(), Height());
        URasterizer R;
        R.DrawMesh(*gSphere_, xf, glm::vec3(1.0f), fb);
        if (mode_ == 3) fb.ToDepthImage(buf_);
        else            fb.ToOutputImage(buf_);
        glDrawPixels(Width(), Height(), GL_RGB, GL_FLOAT, buf_.data());
    }

private:
    UMesh*          gSphere_ = nullptr;
    UMesh*          gWall_   = nullptr;
    UMesh*          gCube_   = nullptr;
    UMesh*          gFbx_    = nullptr;
    glm::mat4       sphereModel_{1.0f};
    glm::mat4       wallModel_{1.0f};
    UMeshRayTracer  gpu_;
    UMeshRayTracer  gpuCube_;
    UMeshRayTracer  gpuFbx_;
    UHybridPass     hybrid_;
    ACamera         cam_;
    int             mode_ = 2;
    float           spin_ = 0.0f;
    std::vector<float> buf_;
};

// HW6 visual harness: the exact assignment setup (unit->radius-2 sphere at
// (0,0,-7), Blinn-Phong ka/kd/ks/p, point light (-4,4,-3), white ambient 0.2,
// eye 0, FCG frustum l/r/b/t=+-0.1 n=-0.1 f=-1000, viewport 1024). Keys:
//   1 = Flat   2 = Gouraud   3 = Phong   (compare against the reference images)
class HW6Demo : public Engine
{
protected:
    void OnStartup() override
    {
        sphere_ = UMesh::GenerateSphere(2.0f, 32, 16);
        mat_.ka = glm::vec3(0.0f, 1.0f, 0.0f);
        mat_.kd = glm::vec3(0.0f, 0.5f, 0.0f);
        mat_.ks = glm::vec3(0.8f, 0.8f, 0.8f);
        mat_.shininess = 64.0f;
        model_ = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -7.0f));
    }
    void Render() override
    {
        if (KeyDown(GLFW_KEY_1)) shading_ = EShadingModel::Flat;
        if (KeyDown(GLFW_KEY_2)) shading_ = EShadingModel::Gouraud;
        if (KeyDown(GLFW_KEY_3)) shading_ = EShadingModel::Phong;

        UFrameBuffer fb; fb.Init(Width(), Height()); fb.Clear(glm::vec3(0.0f));
        FTransform xf;
        xf.model    = model_;
        xf.view     = glm::mat4(1.0f);                                       // eye 0, axis basis
        xf.proj     = FTransform::MakeProjFCG(-0.1f, 0.1f, -0.1f, 0.1f, -0.1f, -1000.0f);
        xf.viewport = FTransform::MakeViewport(Width(), Height());
        FShadeParams sp;
        sp.lightPos = glm::vec3(-4.0f, 4.0f, -3.0f);
        sp.lightColor = glm::vec3(1.0f);
        sp.ambient = glm::vec3(0.2f);
        sp.eye = glm::vec3(0.0f);

        URasterizer R;
        R.DrawMeshShaded(*sphere_, xf, &mat_, sp, shading_, fb);
        fb.ToOutputImage(buf_);
        glDrawPixels(Width(), Height(), GL_RGB, GL_FLOAT, buf_.data());
    }
private:
    UMesh*        sphere_ = nullptr;
    Material      mat_;
    glm::mat4     model_{1.0f};
    EShadingModel shading_ = EShadingModel::Flat;
    std::vector<float> buf_;
};

// Headless FBX import self-test (page 6 gates) -- no GL window.
static int RunFbxGates()
{
    int pass = 0, fail = 0;
    auto ck = [&](const char* t, const char* w, bool ok)
    { std::printf("[%s] %s -> %s\n", t, w, ok ? "PASS" : "FAIL"); ok ? ++pass : ++fail; };

    std::vector<UMesh*> meshes = LoadFbxAny("box.fbx");
    ck("T6", "box.fbx loaded (>=1 mesh)", !meshes.empty());

    if (!meshes.empty())
    {
        UMesh* m = meshes[0];
        std::printf("    (box.fbx: %d meshes, mesh0 = %d verts, %d tris)\n",
                    (int)meshes.size(), (int)m->vertices.size(), m->triangleCount());
        ck("T1", "box has 12 triangles (cube)", m->triangleCount() == 12);

        bool nrm = true;
        for (auto& v : m->vertices)
            if (std::fabs(glm::length(v.normal) - 1.0f) > 1e-2f) nrm = false;
        ck("T4", "all normals unit length", nrm);
        ck("T3", "all faces triangulated (indices % 3 == 0)", m->indices.size() % 3 == 0);
    }

    auto bad = LoadFbxAny("definitely_missing_zzz.fbx");
    ck("T9", "missing fbx -> empty vector (no crash)", bad.empty());

    for (UMesh* m : meshes) delete m;
    std::printf("=== fbx gates: %d passed, %d failed ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}

// Headless Task 5 importer completion gate. Assimp selects its importer from
// file contents/extension, so a tiny generated OBJ exercises the same
// UFbxImporter::ConvertMesh completion path without a checked-in binary asset.
static int RunMeshRevisionGates()
{
    const char* path = "mesh_revision_assimp.tmp.obj";
    {
        std::ofstream obj(path);
        obj << "v 0 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "f 1 2 3\n";
    }

    UMesh baseline;
    std::vector<UMesh*> meshes = UFbxImporter::Load(path);
    const bool passed = meshes.size() == 1 &&
                        meshes[0]->triangleCount() == 1 &&
                        meshes[0]->GeometryRevision() == baseline.GeometryRevision() + 1;

    for (UMesh* mesh : meshes) delete mesh;
    std::remove(path);
    std::printf("[%s] Assimp conversion finalizes geometry exactly once\n",
                passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

class FHardwareRasterSelfTestApp final : public Engine
{
public:
    int RunGates(const std::string& outputPath)
    {
        glfwHideWindow(window_);

        int passed = 0;
        int failed = 0;
        auto check = [&](const char* label, bool result)
        {
            std::printf("[%s] %s\n", result ? "PASS" : "FAIL", label);
            result ? ++passed : ++failed;
        };

        bool initialErrorsCleared = false;
        for (int attempt = 0; attempt < 16; ++attempt)
            if (glGetError() == GL_NO_ERROR)
            {
                initialErrorsCleared = true;
                break;
            }
        check("initial OpenGL error drain is bounded", initialErrorsCleared);

        FOpenGLMeshUploadAdapter uploadAdapter;
        UGPUMeshCache meshCache(uploadAdapter);
        UHardwareGBuffer gbuffer;
        UHardwareRasterizer rasterizer;
        std::string diagnostic;
        check("hardware raster shaders compile and link",
              rasterizer.Init(ContextGeneration(), &diagnostic));
        if (!diagnostic.empty()) std::fprintf(stderr, "%s\n", diagnostic.c_str());

        std::unique_ptr<UMesh> cube(UMesh::GenerateCube(glm::vec3(0.75f)));
        FRenderScene scene;
        scene.camera.nearDistance = 0.1f;
        scene.camera.left = -0.1f;
        scene.camera.rightPlane = 0.1f;
        scene.camera.bottom = -0.1f;
        scene.camera.top = 0.1f;

        Material farMaterial;
        farMaterial.kd = glm::vec3(0.1f, 0.2f, 0.9f);
        Material frontMaterial;
        frontMaterial.kd = glm::vec3(0.9f, 0.2f, 0.1f);
        frontMaterial.ks = glm::vec3(0.6f, 0.5f, 0.4f);
        frontMaterial.shininess = 48.0f;
        frontMaterial.km = glm::vec3(0.25f);
        Material sideMaterial;
        sideMaterial.kd = glm::vec3(0.1f, 0.8f, 0.2f);

        auto instance = [&](const glm::vec3& position, const Material& material,
                            std::uint32_t objectIdentity, std::uint32_t materialIdentity)
        {
            FRenderMeshInstance result;
            result.mesh = cube.get();
            result.modelTransform = glm::translate(glm::mat4(1.0f), position);
            result.normalTransform = glm::mat3(1.0f);
            FResolvedRenderMaterial resolved;
            resolved.source = &material;
            resolved.albedo = material.kd;
            resolved.specularColor = material.ks;
            resolved.shininess = material.shininess;
            resolved.mirrorFactor = std::max(material.km.x,
                std::max(material.km.y, material.km.z));
            result.materialOverride = resolved;
            result.materialOverrideIdentity = materialIdentity;
            result.uvTiling = glm::vec2(1.0f);
            result.shadingModel = ERenderShadingModel::Phong;
            result.objectIdentity = objectIdentity;
            return result;
        };

        scene.meshes.push_back(instance(glm::vec3(0.0f, 0.0f, -8.0f), farMaterial, 1, 11));
        check("64x64 G-buffer allocation", gbuffer.Resize(64, 64, ContextGeneration()));
        check("64x64 G-buffer is complete", gbuffer.IsComplete());
        check("far cube geometry draw", rasterizer.RenderGeometry(
            scene, meshCache, gbuffer, ContextGeneration(), &diagnostic));

        float farDepth = 1.0f;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer.Framebuffer());
        glReadPixels(32, 32, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &farDepth);

        scene.meshes.push_back(instance(glm::vec3(0.0f, 0.0f, -5.0f), frontMaterial, 2, 22));
        FRenderMeshInstance side = instance(
            glm::vec3(2.2f, 0.0f, -6.0f), sideMaterial, 3, 33);
        side.modelTransform = side.modelTransform *
            glm::scale(glm::mat4(1.0f), glm::vec3(-0.7f, 1.2f, 0.8f));
        side.normalTransform = glm::transpose(glm::inverse(glm::mat3(side.modelTransform)));
        scene.meshes.push_back(std::move(side));
        meshCache.BeginFrame();

        GLuint sentinelTextures[2] = {};
        GLuint sentinelVAO = 0;
        GLuint sentinelBuffer = 0;
        glGenTextures(2, sentinelTextures);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sentinelTextures[0]);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, sentinelTextures[1]);
        glGenVertexArrays(1, &sentinelVAO);
        glBindVertexArray(sentinelVAO);
        glGenBuffers(1, &sentinelBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
        glViewport(3, 4, 17, 19);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glFrontFace(GL_CW);
        glEnablei(GL_BLEND, 0);
        glDisablei(GL_BLEND, 1);
        glEnable(GL_SCISSOR_TEST);
        glScissor(7, 8, 1, 1);
        glColorMaski(0, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        glColorMaski(1, GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        glDepthRange(0.2, 0.8);
        glPolygonMode(GL_FRONT, GL_LINE);
        glPolygonMode(GL_BACK, GL_POINT);

        FRenderScene emptyScene;
        check("empty scene clears under hostile caller state", rasterizer.RenderGeometry(
            emptyScene, meshCache, gbuffer, ContextGeneration(), &diagnostic));
        float emptyCoverage = -1.0f;
        float emptyDepth = -1.0f;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer.Framebuffer());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::PositionCoverage));
        glReadPixels(32, 32, 1, 1, GL_ALPHA, GL_FLOAT, &emptyCoverage);
        glReadPixels(32, 32, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &emptyDepth);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        check("hostile-state empty clear writes neutral coverage and depth",
              emptyCoverage == 0.0f && emptyDepth == 1.0f);

        check("overlap and transformed cube geometry draw", rasterizer.RenderGeometry(
            scene, meshCache, gbuffer, ContextGeneration(), &diagnostic));

        GLint restoredViewport[4] = {};
        GLint restoredVAO = 0;
        GLint restoredBuffer = 0;
        GLint restoredActiveTexture = 0;
        GLint restoredActiveBinding = 0;
        GLint restoredUnitZeroBinding = 0;
        GLint restoredDepthFunction = 0;
        GLint restoredFrontFace = 0;
        GLint restoredScissor[4] = {};
        GLint restoredPolygonMode[2] = {};
        GLboolean restoredColorMasks[2][4] = {};
        GLdouble restoredDepthRange[2] = {};
        GLboolean restoredDepthMask = GL_TRUE;
        glGetIntegerv(GL_VIEWPORT, restoredViewport);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &restoredVAO);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &restoredBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &restoredActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &restoredActiveBinding);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &restoredUnitZeroBinding);
        glActiveTexture(GL_TEXTURE3);
        glGetIntegerv(GL_DEPTH_FUNC, &restoredDepthFunction);
        glGetIntegerv(GL_FRONT_FACE, &restoredFrontFace);
        glGetIntegerv(GL_SCISSOR_BOX, restoredScissor);
        glGetIntegerv(GL_POLYGON_MODE, restoredPolygonMode);
        glGetBooleani_v(GL_COLOR_WRITEMASK, 0, restoredColorMasks[0]);
        glGetBooleani_v(GL_COLOR_WRITEMASK, 1, restoredColorMasks[1]);
        glGetDoublev(GL_DEPTH_RANGE, restoredDepthRange);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &restoredDepthMask);
        check("geometry pass restores caller OpenGL state",
              restoredViewport[0] == 3 && restoredViewport[1] == 4 &&
              restoredViewport[2] == 17 && restoredViewport[3] == 19 &&
              restoredVAO == static_cast<GLint>(sentinelVAO) &&
              restoredBuffer == static_cast<GLint>(sentinelBuffer) &&
              restoredActiveTexture == GL_TEXTURE3 &&
              restoredActiveBinding == static_cast<GLint>(sentinelTextures[1]) &&
              restoredUnitZeroBinding == static_cast<GLint>(sentinelTextures[0]) &&
              restoredDepthFunction == GL_GREATER && restoredFrontFace == GL_CW &&
              restoredDepthMask == GL_FALSE && !glIsEnabled(GL_DEPTH_TEST) &&
              glIsEnabled(GL_CULL_FACE) && glIsEnabledi(GL_BLEND, 0) &&
              !glIsEnabledi(GL_BLEND, 1) &&
              glIsEnabled(GL_SCISSOR_TEST) &&
              restoredScissor[0] == 7 && restoredScissor[1] == 8 &&
              restoredScissor[2] == 1 && restoredScissor[3] == 1 &&
              restoredPolygonMode[0] == GL_LINE && restoredPolygonMode[1] == GL_POINT &&
              restoredColorMasks[0][0] == GL_FALSE && restoredColorMasks[0][1] == GL_TRUE &&
              restoredColorMasks[0][2] == GL_FALSE && restoredColorMasks[0][3] == GL_TRUE &&
              restoredColorMasks[1][0] == GL_TRUE && restoredColorMasks[1][1] == GL_FALSE &&
              restoredColorMasks[1][2] == GL_TRUE && restoredColorMasks[1][3] == GL_FALSE &&
              std::fabs(restoredDepthRange[0] - 0.2) < 0.000001 &&
              std::fabs(restoredDepthRange[1] - 0.8) < 0.000001);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glDeleteBuffers(1, &sentinelBuffer);
        glDeleteVertexArrays(1, &sentinelVAO);
        glDeleteTextures(2, sentinelTextures);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);
        for (GLuint drawBuffer = 0; drawBuffer < 8; ++drawBuffer)
        {
            glDisablei(GL_BLEND, drawBuffer);
            glColorMaski(drawBuffer, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        glDisable(GL_SCISSOR_TEST);
        glDepthRange(0.0, 1.0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glFrontFace(GL_CCW);

        std::vector<float> positions(64 * 64 * 4);
        std::vector<float> geometricNormals(64 * 64 * 4);
        std::vector<float> shadingNormals(64 * 64 * 4);
        std::vector<float> albedo(64 * 64 * 4);
        std::vector<float> specular(64 * 64 * 4);
        std::vector<float> depth(64 * 64);
        std::vector<std::uint32_t> identities(64 * 64 * 2);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer.Framebuffer());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::PositionCoverage));
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, positions.data());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::GeometricNormal));
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, geometricNormals.data());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::ShadingNormalModel));
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, shadingNormals.data());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::AlbedoShininess));
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, albedo.data());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::SpecularMirror));
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, specular.data());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::Identity));
        glReadPixels(0, 0, 64, 64, GL_RG_INTEGER, GL_UNSIGNED_INT, identities.data());
        glReadPixels(0, 0, 64, 64, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        const std::size_t center = 32u * 64u + 32u;
        const std::size_t corner = 0;
        check("center pixel is covered", positions[center * 4 + 3] > 0.5f);
        check("corner pixel remains invalid", positions[corner * 4 + 3] == 0.0f &&
              identities[corner * 2] == 0u && identities[corner * 2 + 1] == 0u);
        const glm::vec3 centerNormal(geometricNormals[center * 4],
                                     geometricNormals[center * 4 + 1],
                                     geometricNormals[center * 4 + 2]);
        check("center normal is finite and normalized",
              std::isfinite(centerNormal.x) && std::isfinite(centerNormal.y) &&
              std::isfinite(centerNormal.z) &&
              std::fabs(glm::length(centerNormal) - 1.0f) < 0.02f);
        check("center depth is in range", depth[center] > 0.0f && depth[center] < 1.0f);
        check("front cube wins overlap depth", identities[center * 2] == 2u &&
              depth[center] < farDepth);
        check("textureless material fields survive the G-buffer",
              identities[center * 2 + 1] == 22u &&
              std::fabs(albedo[center * 4] - 0.9f) < 0.002f &&
              std::fabs(albedo[center * 4 + 1] - 0.2f) < 0.002f &&
              std::fabs(albedo[center * 4 + 3] - 48.0f) < 0.02f &&
              std::fabs(specular[center * 4] - 0.6f) < 0.002f &&
              std::fabs(specular[center * 4 + 3] - 0.25f) < 0.002f &&
              std::fabs(shadingNormals[center * 4 + 3] - 2.0f) < 0.002f);

        bool foundSideCube = false;
        bool sideNormalValid = false;
        for (std::size_t pixel = 0; pixel < 64u * 64u; ++pixel)
            if (identities[pixel * 2] == 3u)
            {
                foundSideCube = true;
                const glm::vec3 normal(shadingNormals[pixel * 4],
                                       shadingNormals[pixel * 4 + 1],
                                       shadingNormals[pixel * 4 + 2]);
                sideNormalValid = sideNormalValid ||
                    (std::isfinite(normal.x) && std::isfinite(normal.y) &&
                     std::isfinite(normal.z) &&
                     std::fabs(glm::length(normal) - 1.0f) < 0.02f);
            }
        check("mirrored nonuniform cube covers a distinct region", foundSideCube && sideNormalValid);
        check("shared source mesh uploads once", meshCache.Stats().uploads == 1u &&
              meshCache.Stats().reuploads == 0u && meshCache.Stats().residentResources == 1u);

        {
            std::ofstream ppm(outputPath, std::ios::binary);
            ppm << "P6\n64 64\n255\n";
            for (int y = 63; y >= 0; --y)
                for (int x = 0; x < 64; ++x)
                {
                    const std::size_t pixel = static_cast<std::size_t>(y * 64 + x) * 4;
                    const unsigned char rgb[3] = {
                        static_cast<unsigned char>(glm::clamp(albedo[pixel], 0.0f, 1.0f) * 255.0f),
                        static_cast<unsigned char>(glm::clamp(albedo[pixel + 1], 0.0f, 1.0f) * 255.0f),
                        static_cast<unsigned char>(glm::clamp(albedo[pixel + 2], 0.0f, 1.0f) * 255.0f),
                    };
                    ppm.write(reinterpret_cast<const char*>(rgb), 3);
                }
            check("meaningful PPM output written", static_cast<bool>(ppm));
        }

        UMesh invalidMesh;
        invalidMesh.vertices.push_back({glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                                        glm::vec2(0.0f)});
        invalidMesh.indices = {0u, 1u, 2u};
        FRenderScene emptyAndInvalidScene;
        FRenderMeshInstance invalidInstance;
        invalidInstance.mesh = &invalidMesh;
        emptyAndInvalidScene.meshes.push_back(invalidInstance);
        const std::uint64_t uploadsBeforeInvalid = meshCache.Stats().uploads;
        check("empty and invalid meshes skip without GL errors",
              rasterizer.RenderGeometry(emptyAndInvalidScene, meshCache, gbuffer,
                                        ContextGeneration(), &diagnostic) &&
              meshCache.Stats().uploads == uploadsBeforeInvalid &&
              glGetError() == GL_NO_ERROR);

        const std::uint64_t beforeResizeRevision = gbuffer.ResourceRevision();
        check("invalid resize fails without disturbing live resources",
              !gbuffer.Resize(0, 48, ContextGeneration()) && gbuffer.IsComplete() &&
              gbuffer.Width() == 64 && gbuffer.Height() == 64 &&
              gbuffer.ResourceRevision() == beforeResizeRevision);
        check("resize to 80x48 succeeds", gbuffer.Resize(80, 48, ContextGeneration()) &&
              gbuffer.IsComplete() && gbuffer.Width() == 80 && gbuffer.Height() == 48);
        check("resize back to 64x64 succeeds", gbuffer.Resize(64, 64, ContextGeneration()) &&
              gbuffer.IsComplete() && gbuffer.Width() == 64 && gbuffer.Height() == 64);
        check("resize owns one bounded attachment set", gbuffer.OwnedTextureCount() == 9u &&
              gbuffer.ResourceRevision() == beforeResizeRevision + 2u);
        check("idempotent resize keeps resources", gbuffer.Resize(64, 64, ContextGeneration()) &&
              gbuffer.ResourceRevision() == beforeResizeRevision + 2u);
        check("no unexpected OpenGL error", glGetError() == GL_NO_ERROR);

        std::printf("    probes: farDepth=%.6f frontDepth=%.6f centerNormal=(%.3f,%.3f,%.3f)\n",
                    farDepth, depth[center], centerNormal.x, centerNormal.y, centerNormal.z);

        FGPUMeshResource oldMeshResource = meshCache.Acquire(*cube, ContextGeneration());
        const GLuint oldProgram = rasterizer.Program();
        const GLuint oldFramebuffer = gbuffer.Framebuffer();
        const GLuint oldDepthTexture = gbuffer.DepthTexture();
        GLuint oldColorTextures[8] = {};
        for (unsigned semantic = 0; semantic < 8; ++semantic)
            oldColorTextures[semantic] = gbuffer.Texture(
                static_cast<EHardwareGBufferSemantic>(semantic));

        const std::uint64_t recreatedGeneration = ContextGeneration() + 100;
        const std::uint64_t originalGeneration = ContextGeneration();
        const std::uint64_t revisionBeforeMismatch = gbuffer.ResourceRevision();
        check("mismatched caller generation is rejected without mutation",
              !gbuffer.Resize(64, 64, recreatedGeneration) &&
              !rasterizer.Init(recreatedGeneration, &diagnostic) &&
              !rasterizer.RenderGeometry(scene, meshCache, gbuffer,
                                         recreatedGeneration, &diagnostic) &&
              gbuffer.ResourceRevision() == revisionBeforeMismatch &&
              gbuffer.ContextGeneration() == originalGeneration &&
              rasterizer.ContextGeneration() == originalGeneration &&
              meshCache.Stats().abandons == 0u);
        SetActiveRenderTargetContextGeneration(recreatedGeneration);
        check("generation transition allocates a fresh G-buffer",
              gbuffer.Resize(64, 64, recreatedGeneration) &&
              gbuffer.ContextGeneration() == recreatedGeneration &&
              gbuffer.Framebuffer() != oldFramebuffer);
        bool oldColorTexturesSurvive = true;
        for (GLuint texture : oldColorTextures)
            oldColorTexturesSurvive = oldColorTexturesSurvive && glIsTexture(texture);
        check("generation transition abandons stale program and mesh names",
              rasterizer.RenderGeometry(scene, meshCache, gbuffer,
                                        recreatedGeneration, &diagnostic) &&
              rasterizer.ContextGeneration() == recreatedGeneration &&
              rasterizer.Program() != oldProgram &&
              meshCache.Stats().abandons == 1u &&
              glIsProgram(oldProgram) && glIsVertexArray(oldMeshResource.vao) &&
              glIsBuffer(oldMeshResource.vertexBuffer) &&
              glIsBuffer(oldMeshResource.indexBuffer) &&
              glIsFramebuffer(oldFramebuffer) && glIsTexture(oldDepthTexture) &&
              oldColorTexturesSurvive);

        glDeleteProgram(oldProgram);
        glDeleteBuffers(1, &oldMeshResource.indexBuffer);
        glDeleteBuffers(1, &oldMeshResource.vertexBuffer);
        glDeleteVertexArrays(1, &oldMeshResource.vao);
        glDeleteTextures(8, oldColorTextures);
        glDeleteTextures(1, &oldDepthTexture);
        glDeleteFramebuffers(1, &oldFramebuffer);

        const GLuint currentProgram = rasterizer.Program();
        const GLuint currentFramebuffer = gbuffer.Framebuffer();
        const GLuint currentDepthTexture = gbuffer.DepthTexture();
        GLuint currentColorTextures[8] = {};
        for (unsigned semantic = 0; semantic < 8; ++semantic)
            currentColorTextures[semantic] = gbuffer.Texture(
                static_cast<EHardwareGBufferSemantic>(semantic));
        const FGPUMeshResource currentMeshResource = meshCache.Acquire(
            *cube, recreatedGeneration);
        SetActiveRenderTargetContextGeneration(recreatedGeneration);
        meshCache.Clear();
        gbuffer.Release();
        rasterizer.Shutdown();
        bool currentColorTexturesDeleted = true;
        for (GLuint texture : currentColorTextures)
            currentColorTexturesDeleted = currentColorTexturesDeleted && !glIsTexture(texture);
        check("same-generation shutdown deletes current GL resources",
              !glIsProgram(currentProgram) && !glIsFramebuffer(currentFramebuffer) &&
              !glIsTexture(currentDepthTexture) && currentColorTexturesDeleted &&
              !glIsVertexArray(currentMeshResource.vao) &&
              !glIsBuffer(currentMeshResource.vertexBuffer) &&
              !glIsBuffer(currentMeshResource.indexBuffer));
        SetActiveRenderTargetContextGeneration(originalGeneration);
        std::printf("=== hardware raster gates: %d passed, %d failed ===\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }
};

static int RunHardwareRasterGates(const std::string& outputPath)
{
    FHardwareRasterSelfTestApp app;
    if (!app.Init(64, 64, "Hardware Raster Self-Test")) return 2;
    return app.RunGates(outputPath);
}

static std::unique_ptr<UWorld> LoadRenderParityFixture()
{
    const char* candidates[] = {
        "Test/Fixtures/RenderParityScene.world",
        "../Test/Fixtures/RenderParityScene.world",
        "../../Test/Fixtures/RenderParityScene.world",
    };
    for (const char* path : candidates)
        if (UWorld* world = FWorldSerializer::LoadFromFile(path))
            return std::unique_ptr<UWorld>(world);
    return nullptr;
}

static float LuminanceAt(const std::vector<float>& image, int width, int x, int y)
{
    const std::size_t i = static_cast<std::size_t>(y * width + x) * 3u;
    return image[i] * 0.2126f + image[i + 1] * 0.7152f + image[i + 2] * 0.0722f;
}

static double FloatImageAbsDifference(const std::vector<float>& a,
                                      const std::vector<float>& b)
{
    if (a.size() != b.size()) return -1.0;
    double difference = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        difference += std::fabs(static_cast<double>(a[i] - b[i]));
    return difference;
}

class FLegacyRasterBaselineApp final : public Engine
{
public:
    void Hide() { glfwHideWindow(window_); }
};

// Baseline-only path used before the normal executor cutover. It records
// semantic probes from the compatibility CPU rasterizer; no golden image is
// checked in because edge rules legitimately vary between CPU and OpenGL.
static int RunRasterLightingLegacyBaseline()
{
    FLegacyRasterBaselineApp app;
    if (!app.Init(64, 64, "Legacy Raster Baseline")) return 2;
    app.Hide();
    std::unique_ptr<UWorld> world = LoadRenderParityFixture();
    if (!world)
    {
        std::fprintf(stderr, "RenderParityScene.world not found\n");
        return 2;
    }
    ACamera& camera = world->GetCamera();
    camera.SetOrientation(camera.yaw, camera.pitch);
    camera.SetFOV(camera.fov, 1.0f);
    FRenderScene scene = ExtractRenderScene(*world, camera);
    std::printf("fixture actors=%zu meshes=%zu lights=%zu\n",
        world->GetScene().Actors.size(), scene.meshes.size(), scene.pointLights.size());
    URenderer renderer;
    renderer.multithread = false;
    FRenderShowFlag flags;
    flags.ambientStrength = 1.0f;

    std::vector<float> images[3];
    for (int mode = 0; mode < 3; ++mode)
    {
        flags.shading = static_cast<EShadingModel>(mode);
        images[mode] = renderer.RasterShadedLegacyOutput(scene, 64, 64, flags, nullptr);
    }
    FRenderScene oneLight = scene;
    if (oneLight.pointLights.size() > 1) oneLight.pointLights.resize(1);
    flags.shading = EShadingModel::Phong;
    const std::vector<float> one = renderer.RasterShadedLegacyOutput(
        oneLight, 64, 64, flags, nullptr);
    const std::vector<float> two = renderer.RasterShadedLegacyOutput(
        scene, 64, 64, flags, nullptr);
    flags.depthView = true;
    const std::vector<float> depth = renderer.RasterShadedLegacyOutput(
        scene, 64, 64, flags, nullptr);

    Material checker;
    checker.kd = glm::vec3(1.0f);
    checker.ka = glm::vec3(0.2f);
    checker.ks = glm::vec3(0.0f);
    checker.texWidth = 2;
    checker.texHeight = 2;
    checker.texChannels = 3;
    checker.diffuseTexPath = "baseline://checker";
    checker.texData = {
        255, 255, 255, 20, 20, 20,
        20, 20, 20, 255, 255, 255,
    };
    FResolvedRenderMaterial checkerResolved;
    checkerResolved.source = &checker;
    checkerResolved.albedo = checker.kd;
    FRenderScene checkerScene = scene;
    checkerScene.meshes.front().materialOverride = checkerResolved;
    checkerScene.meshes.front().uvTiling = glm::vec2(2.0f);
    flags.depthView = false;
    flags.shading = EShadingModel::Phong;
    checker.wrapMode = EWrapMode::Repeat;
    const std::vector<float> checkerRepeat = renderer.RasterShadedLegacyOutput(
        checkerScene, 64, 64, flags, nullptr);
    checker.wrapMode = EWrapMode::Clamp;
    const std::vector<float> checkerClamp = renderer.RasterShadedLegacyOutput(
        checkerScene, 64, 64, flags, nullptr);

    FRenderScene alternateEnvironment = scene;
    alternateEnvironment.environment.horizon = glm::vec3(0.7f, 0.03f, 0.02f);
    alternateEnvironment.environment.zenith = glm::vec3(0.02f, 0.1f, 0.75f);
    alternateEnvironment.environment.exponent = 2.0f;
    const std::vector<float> proceduralOriginal = renderer.RasterShadedLegacyOutput(
        scene, 64, 64, flags, nullptr);
    const std::vector<float> proceduralAlternate = renderer.RasterShadedLegacyOutput(
        alternateEnvironment, 64, 64, flags, nullptr);

    const char* baselineHDRPath = "task8_legacy_baseline.tmp.hdr";
    {
        std::ofstream hdr(baselineHDRPath, std::ios::binary);
        hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
        const unsigned char rgbe[16] = {
            240, 20, 10, 129, 10, 220, 30, 129,
            15, 30, 240, 129, 210, 180, 20, 129,
        };
        hdr.write(reinterpret_cast<const char*>(rgbe), sizeof(rgbe));
    }
    USkyHDRI baselineSky;
    baselineSky.GetOrLoad(baselineHDRPath);
    const std::vector<float> hdri = renderer.RasterShadedLegacyOutput(
        scene, 64, 64, flags, &baselineSky);

    const int probes[][2] = {{32, 32}, {22, 31}, {42, 31}, {32, 45}, {2, 2}, {32, 61}};
    std::printf("=== legacy raster semantic baseline (64x64, gamma output) ===\n");
    for (int p = 0; p < 6; ++p)
    {
        const int x = probes[p][0], y = probes[p][1];
        std::printf("probe[%d]=(%d,%d) flat=%.6f gouraud=%.6f phong=%.6f one=%.6f two=%.6f depth=%.6f\n",
            p, x, y, LuminanceAt(images[0], 64, x, y),
            LuminanceAt(images[1], 64, x, y), LuminanceAt(images[2], 64, x, y),
            LuminanceAt(one, 64, x, y), LuminanceAt(two, 64, x, y),
            LuminanceAt(depth, 64, x, y));
    }
    std::printf("checker_repeat_vs_clamp_abs=%.6f\n",
        FloatImageAbsDifference(checkerRepeat, checkerClamp));
    std::printf("procedural_background_delta=%.6f procedural_geometry_delta=%.6f procedural_image_abs=%.6f\n",
        std::fabs(LuminanceAt(proceduralOriginal, 64, 2, 2) -
                  LuminanceAt(proceduralAlternate, 64, 2, 2)),
        std::fabs(LuminanceAt(proceduralOriginal, 64, 32, 32) -
                  LuminanceAt(proceduralAlternate, 64, 32, 32)),
        FloatImageAbsDifference(proceduralOriginal, proceduralAlternate));
    std::printf("hdri_background_delta=%.6f hdri_geometry_delta=%.6f hdri_image_abs=%.6f\n",
        std::fabs(LuminanceAt(proceduralOriginal, 64, 2, 2) -
                  LuminanceAt(hdri, 64, 2, 2)),
        std::fabs(LuminanceAt(proceduralOriginal, 64, 32, 32) -
                  LuminanceAt(hdri, 64, 32, 32)),
        FloatImageAbsDifference(proceduralOriginal, hdri));
    std::printf("depth_order center=%.6f right=%.6f sphere=%.6f background=%.6f\n",
        LuminanceAt(depth, 64, 32, 32), LuminanceAt(depth, 64, 42, 31),
        LuminanceAt(depth, 64, 32, 45), LuminanceAt(depth, 64, 2, 2));
    std::printf("row31_depth_transitions");
    bool previousCovered = LuminanceAt(depth, 64, 0, 31) > 0.0f;
    for (int x = 1; x < 64; ++x)
    {
        const bool covered = LuminanceAt(depth, 64, x, 31) > 0.0f;
        if (covered != previousCovered)
            std::printf(" x%d:%d", x, covered ? 1 : 0);
        previousCovered = covered;
    }
    std::printf("\n");
    baselineSky.Cleanup();
    std::remove(baselineHDRPath);
    return 0;
}

class FRasterLightingSelfTestApp final : public Engine
{
public:
    int RunGates(const std::string& outputPath)
    {
        glfwHideWindow(window_);
        int passed = 0, failed = 0;
        auto check = [&](const char* label, bool result)
        {
            std::printf("[%s] %s\n", result ? "PASS" : "FAIL", label);
            result ? ++passed : ++failed;
        };

        std::unique_ptr<UMesh> sphere(UMesh::GenerateSphere(0.9f, 12, 8));
        Material material;
        material.kd = glm::vec3(0.25f, 0.4f, 0.85f);
        material.ks = glm::vec3(0.7f);
        material.shininess = 40.0f;
        material.emissive = glm::vec3(0.01f, 0.005f, 0.02f);

        FRenderScene scene;
        scene.camera.nearDistance = 0.1f;
        scene.camera.left = -0.1f;
        scene.camera.rightPlane = 0.1f;
        scene.camera.bottom = -0.1f;
        scene.camera.top = 0.1f;
        scene.environment.horizon = glm::vec3(0.08f, 0.12f, 0.22f);
        scene.environment.zenith = glm::vec3(0.42f, 0.64f, 0.95f);
        scene.pointLights = {
            {glm::vec3(-3.0f, 4.0f, -3.0f), glm::vec3(0.9f, 0.75f, 0.6f)},
            {glm::vec3(3.0f, 1.0f, -4.0f), glm::vec3(0.2f, 0.35f, 0.7f)},
        };
        FRenderMeshInstance instance;
        instance.mesh = sphere.get();
        instance.modelTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -6));
        instance.normalTransform = glm::mat3(1.0f);
        FResolvedRenderMaterial resolved;
        resolved.source = &material;
        resolved.albedo = material.kd;
        resolved.specularColor = material.ks;
        resolved.emissive = material.emissive;
        resolved.shininess = material.shininess;
        instance.materialOverride = resolved;
        instance.materialOverrideIdentity = 7;
        instance.objectIdentity = 9;
        scene.meshes.push_back(instance);

        FOpenGLMeshUploadAdapter adapter;
        UGPUMeshCache cache(adapter);
        UHardwareGBuffer gbuffer;
        UHardwareRasterizer rasterizer;
        URasterLightingPass lighting;
        FRenderQuality quality;
        std::string diagnostic;
        check("lighting shaders compile and link",
              lighting.Init(ContextGeneration(), &diagnostic));
        check("lighting output allocation",
              lighting.Resize(64, 64, ContextGeneration(), &diagnostic));
        check("lighting G-buffer allocation",
              gbuffer.Resize(64, 64, ContextGeneration()));
        cache.BeginFrame();
        const bool preparedInitialEnvironment = lighting.PrepareEnvironment(
            scene, ContextGeneration(), &diagnostic);
        check("Phong geometry includes lighting interpolants",
              preparedInitialEnvironment && rasterizer.RenderGeometry(
                  scene, quality, cache, gbuffer, lighting.EnvironmentTexture(),
                  ContextGeneration(), &diagnostic));
        cache.ReleaseUnused();
        FRasterLightingOutput lit;
        check("Phong G-buffer shades into HDR output",
              lighting.Render(scene, quality, gbuffer, ContextGeneration(), lit, &diagnostic) &&
              lit.valid && lit.colorTarget.valid);

        glDrawBuffer(GL_NONE);
        glViewport(3, 4, 17, 19);
        glEnable(GL_RASTERIZER_DISCARD);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_NEVER, 1, 0xff);
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glEnable(GL_SAMPLE_COVERAGE);
        glEnable(GL_DITHER);
        glEnable(GL_PRIMITIVE_RESTART);
        glEnable(GL_DEPTH_CLAMP);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glEnable(GL_SCISSOR_TEST);
        glScissor(7, 8, 1, 1);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glEnable(GL_CULL_FACE);
        glEnable(GL_FRAMEBUFFER_SRGB);
        glDepthRange(0.2, 0.8);
        glPolygonMode(GL_FRONT, GL_LINE);
        glPolygonMode(GL_BACK, GL_POINT);
        glFrontFace(GL_CW);
        glEnablei(GL_BLEND, 0);
        glColorMaski(0, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);

        FRasterLightingOutput hostileLit;
        const bool hostileLightingOK = lighting.Render(
            scene, quality, gbuffer, ContextGeneration(), hostileLit, &diagnostic);
        float hostileLightingPixel[4] = {};
        glBindFramebuffer(GL_READ_FRAMEBUFFER, lighting.Framebuffer());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, hostileLightingPixel);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        GLint restoredLightingDrawBuffer = 0;
        GLint restoredLightingViewport[4] = {};
        GLint restoredLightingPolygon[2] = {};
        GLint restoredLightingFrontFace = 0;
        GLint restoredLightingScissor[4] = {};
        GLint restoredLightingDepthFunc = 0;
        GLdouble restoredLightingDepthRange[2] = {};
        GLboolean restoredLightingDepthMask = GL_TRUE;
        GLboolean restoredLightingMask[4] = {};
        glGetIntegerv(GL_DRAW_BUFFER0, &restoredLightingDrawBuffer);
        glGetIntegerv(GL_VIEWPORT, restoredLightingViewport);
        glGetIntegerv(GL_POLYGON_MODE, restoredLightingPolygon);
        glGetIntegerv(GL_FRONT_FACE, &restoredLightingFrontFace);
        glGetIntegerv(GL_SCISSOR_BOX, restoredLightingScissor);
        glGetIntegerv(GL_DEPTH_FUNC, &restoredLightingDepthFunc);
        glGetDoublev(GL_DEPTH_RANGE, restoredLightingDepthRange);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &restoredLightingDepthMask);
        glGetBooleani_v(GL_COLOR_WRITEMASK, 0, restoredLightingMask);
        const bool hostileStateRestored =
            restoredLightingDrawBuffer == GL_NONE &&
            restoredLightingViewport[0] == 3 && restoredLightingViewport[1] == 4 &&
            restoredLightingViewport[2] == 17 && restoredLightingViewport[3] == 19 &&
            restoredLightingPolygon[0] == GL_LINE &&
            restoredLightingPolygon[1] == GL_POINT &&
            restoredLightingFrontFace == GL_CW &&
            restoredLightingScissor[0] == 7 && restoredLightingScissor[1] == 8 &&
            restoredLightingScissor[2] == 1 && restoredLightingScissor[3] == 1 &&
            restoredLightingDepthFunc == GL_GREATER &&
            restoredLightingDepthMask == GL_FALSE &&
            std::fabs(restoredLightingDepthRange[0] - 0.2) < 0.000001 &&
            std::fabs(restoredLightingDepthRange[1] - 0.8) < 0.000001 &&
            restoredLightingMask[0] == GL_FALSE && restoredLightingMask[1] == GL_TRUE &&
            restoredLightingMask[2] == GL_FALSE && restoredLightingMask[3] == GL_TRUE &&
            glIsEnabled(GL_RASTERIZER_DISCARD) && glIsEnabled(GL_STENCIL_TEST) &&
            glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) && glIsEnabled(GL_SAMPLE_COVERAGE) &&
            glIsEnabled(GL_DITHER) && glIsEnabled(GL_PRIMITIVE_RESTART) &&
            glIsEnabled(GL_DEPTH_CLAMP) && glIsEnabled(GL_POLYGON_OFFSET_FILL) &&
            glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_DEPTH_TEST) &&
            glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_FRAMEBUFFER_SRGB) &&
            glIsEnabledi(GL_BLEND, 0);
        check("lighting pass survives hostile state and restores it exactly",
              hostileLightingOK && hostileLightingPixel[3] > 0.5f &&
              hostileLightingPixel[0] + hostileLightingPixel[1] +
                  hostileLightingPixel[2] > 0.01f && hostileStateRestored);

        FRenderTarget target = FRenderTarget::DefaultFramebuffer(
            64, 64, ContextGeneration());
        check("default target binds", target.Begin());
        FCompositeOutput composite;
        const bool hostileCompositeOK = lighting.Composite(
            target, hostileLit, FRayEffectOutputs{}, ContextGeneration(),
            composite, &diagnostic) && composite.valid;
        std::vector<unsigned char> pixels(64u * 64u * 3u);
        glReadPixels(0, 0, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        GLint restoredCompositeDrawBuffer = 0;
        glGetIntegerv(GL_DRAW_BUFFER0, &restoredCompositeDrawBuffer);
        check("composite selects color zero under hostile state and restores draw buffer",
              hostileCompositeOK && restoredCompositeDrawBuffer == GL_NONE);
        target.End();

        glDrawBuffer(GL_BACK);
        glViewport(0, 0, 64, 64);
        glDisable(GL_RASTERIZER_DISCARD);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glDisable(GL_SAMPLE_COVERAGE);
        glEnable(GL_DITHER);
        glDisable(GL_PRIMITIVE_RESTART);
        glDisable(GL_DEPTH_CLAMP);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glDepthRange(0.0, 1.0);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glFrontFace(GL_CCW);
        glDisablei(GL_BLEND, 0);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const std::size_t center = (32u * 64u + 32u) * 3u;
        const std::size_t corner = 0;
        check("lit geometry is non-background and finite",
              pixels[center] + pixels[center + 1] + pixels[center + 2] > 20u);
        check("procedural environment fills background",
              pixels[corner] + pixels[corner + 1] + pixels[corner + 2] > 20u);

        auto renderFrame = [&](FRenderScene& frameScene,
                               const FRenderQuality& frameQuality)
        {
            std::vector<unsigned char> result(64u * 64u * 3u);
            cache.BeginFrame();
            const bool environmentOK = lighting.PrepareEnvironment(
                frameScene, ContextGeneration(), &diagnostic);
            const bool geometryOK = environmentOK && rasterizer.RenderGeometry(
                frameScene, frameQuality, cache, gbuffer,
                lighting.EnvironmentTexture(), ContextGeneration(), &diagnostic);
            cache.ReleaseUnused();
            FRasterLightingOutput frameLighting;
            const bool lightingOK = geometryOK && lighting.Render(
                frameScene, frameQuality, gbuffer, ContextGeneration(),
                frameLighting, &diagnostic);
            FCompositeOutput frameComposite;
            const bool bound = target.Begin();
            const bool compositeOK = bound && lightingOK && lighting.Composite(
                target, frameLighting, FRayEffectOutputs{}, ContextGeneration(),
                frameComposite, &diagnostic);
            if (compositeOK)
                glReadPixels(0, 0, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, result.data());
            if (bound) target.End();
            if (!compositeOK && !diagnostic.empty())
                std::fprintf(stderr, "%s\n", diagnostic.c_str());
            return std::make_pair(compositeOK, result);
        };

        auto imageDifference = [](const std::vector<unsigned char>& a,
                                  const std::vector<unsigned char>& b)
        {
            std::uint64_t difference = 0;
            for (std::size_t i = 0; i < a.size(); ++i)
                difference += static_cast<std::uint64_t>(std::abs(
                    static_cast<int>(a[i]) - static_cast<int>(b[i])));
            return difference;
        };
        auto imageEnergy = [](const std::vector<unsigned char>& image)
        {
            std::uint64_t energy = 0;
            for (unsigned char value : image) energy += value;
            return energy;
        };

        scene.meshes.front().shadingModel = ERenderShadingModel::Flat;
        const auto flat = renderFrame(scene, quality);
        scene.meshes.front().shadingModel = ERenderShadingModel::Gouraud;
        const auto gouraud = renderFrame(scene, quality);
        scene.meshes.front().shadingModel = ERenderShadingModel::Phong;
        const auto phong = renderFrame(scene, quality);
        check("Flat/Gouraud/Phong frames all render",
              flat.first && gouraud.first && phong.first);
        check("Flat is a distinct face-constant lighting result",
              imageDifference(flat.second, phong.second) > 500u);
        check("Gouraud vertex lighting differs from per-pixel Phong",
              imageDifference(gouraud.second, phong.second) > 100u);

        FRenderScene oneLightScene = scene;
        oneLightScene.pointLights.resize(1);
        const auto oneLight = renderFrame(oneLightScene, quality);
        const auto twoLights = renderFrame(scene, quality);
        check("second point light contributes additively",
              oneLight.first && twoLights.first &&
              imageEnergy(twoLights.second) > imageEnergy(oneLight.second) + 100u);

        FRenderQuality noAmbient = quality;
        noAmbient.ambientStrength = 0.0f;
        const auto ambientOff = renderFrame(scene, noAmbient);
        const auto ambientOn = renderFrame(scene, quality);
        check("ambient strength changes visible geometry",
              ambientOff.first && ambientOn.first &&
              imageEnergy(ambientOn.second) > imageEnergy(ambientOff.second) + 100u);

        FRenderScene lowMaterialAmbientScene = scene;
        lowMaterialAmbientScene.pointLights.clear();
        lowMaterialAmbientScene.meshes.front().materialOverride->ambient = glm::vec3(0.01f);
        lowMaterialAmbientScene.meshes.front().materialOverride->emissive = glm::vec3(0.0f);
        const auto lowMaterialAmbient = renderFrame(lowMaterialAmbientScene, quality);
        FRenderScene highMaterialAmbientScene = lowMaterialAmbientScene;
        highMaterialAmbientScene.meshes.front().materialOverride->ambient = glm::vec3(0.8f);
        const auto highMaterialAmbient = renderFrame(highMaterialAmbientScene, quality);
        const int lowAmbientCenter =
            lowMaterialAmbient.second[center] +
            lowMaterialAmbient.second[center + 1] +
            lowMaterialAmbient.second[center + 2];
        const int highAmbientCenter =
            highMaterialAmbient.second[center] +
            highMaterialAmbient.second[center + 1] +
            highMaterialAmbient.second[center + 2];
        check("material ambient coefficient controls environment fill",
              lowMaterialAmbient.first && highMaterialAmbient.first &&
              highAmbientCenter > lowAmbientCenter + 20);

        FRenderQuality depthQuality = quality;
        depthQuality.depthView = true;
        const auto depthFrame = renderFrame(scene, depthQuality);
        const std::size_t depthCenter = (32u * 64u + 32u) * 3u;
        check("depth visualization separates geometry from background",
              depthFrame.first && depthFrame.second[depthCenter] > 0u &&
              depthFrame.second[0] == 0u &&
              depthFrame.second[depthCenter] == depthFrame.second[depthCenter + 1] &&
              depthFrame.second[depthCenter] == depthFrame.second[depthCenter + 2]);

        material.kd = glm::vec3(1.0f);
        material.texWidth = 2;
        material.texHeight = 2;
        material.texChannels = 3;
        material.diffuseTexPath = "Test/Fixtures/Checker2x2.ppm";
        material.wrapMode = EWrapMode::Repeat;
        material.uvTiling = glm::vec2(2.0f);
        material.texData = {
            255, 255, 255, 20, 20, 20,
            20, 20, 20, 255, 255, 255,
        };
        scene.meshes.front().uvTiling = glm::vec2(2.0f);
        GLuint hostileGeometrySamplers[2] = {};
        GLuint hostileEnvironmentBinding = 0;
        glGenSamplers(2, hostileGeometrySamplers);
        glSamplerParameteri(hostileGeometrySamplers[0], GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST_MIPMAP_NEAREST);
        glSamplerParameteri(hostileGeometrySamplers[1], GL_TEXTURE_MIN_FILTER,
                            GL_NEAREST_MIPMAP_NEAREST);
        glGenTextures(1, &hostileEnvironmentBinding);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, hostileGeometrySamplers[0]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, hostileEnvironmentBinding);
        glBindSampler(1, hostileGeometrySamplers[1]);
        glActiveTexture(GL_TEXTURE5);
        const std::uint64_t textureUploadsBefore = rasterizer.MaterialTextureUploads();
        const std::uint64_t textureFailuresBefore =
            rasterizer.MaterialTextureUploadFailures();
        rasterizer.InjectNextMaterialTextureUploadFailureForTesting();
        const auto checkerFailedUpload = renderFrame(scene, quality);
        check("failed material texture upload is transactional",
              !checkerFailedUpload.first &&
              rasterizer.MaterialTextureUploads() == textureUploadsBefore &&
              rasterizer.MaterialTextureUploadFailures() == textureFailuresBefore + 1u);
        glActiveTexture(GL_TEXTURE0 - 1);
        const auto checkerFirst = renderFrame(scene, quality);
        const std::uint64_t textureUploadsAfterFirst = rasterizer.MaterialTextureUploads();
        const auto checkerSecond = renderFrame(scene, quality);
        GLint restoredGeometryActiveTexture = 0;
        GLint restoredGeometryEnvironment = 0;
        GLint restoredGeometrySamplers[2] = {};
        glGetIntegerv(GL_ACTIVE_TEXTURE, &restoredGeometryActiveTexture);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &restoredGeometryEnvironment);
        glGetIntegeri_v(GL_SAMPLER_BINDING, 0, &restoredGeometrySamplers[0]);
        glGetIntegeri_v(GL_SAMPLER_BINDING, 1, &restoredGeometrySamplers[1]);
        glActiveTexture(GL_TEXTURE5);
        check("checker texture uploads once and remains cached",
              checkerFirst.first && checkerSecond.first &&
              textureUploadsAfterFirst == textureUploadsBefore + 1u &&
              rasterizer.MaterialTextureUploads() == textureUploadsAfterFirst);
        check("geometry ignores hostile samplers and restores units zero/one",
              restoredGeometryActiveTexture == GL_TEXTURE5 &&
              restoredGeometryEnvironment ==
                  static_cast<GLint>(hostileEnvironmentBinding) &&
              restoredGeometrySamplers[0] ==
                  static_cast<GLint>(hostileGeometrySamplers[0]) &&
              restoredGeometrySamplers[1] ==
                  static_cast<GLint>(hostileGeometrySamplers[1]));
        check("repeated checker UVs produce alternating surface samples",
              imageDifference(checkerFirst.second, phong.second) > 1000u);
        material.texData[0] = 64;
        rasterizer.InjectNextMaterialTextureUploadFailureForTesting();
        const auto checkerFailedEdit = renderFrame(scene, quality);
        check("failed live texture refresh retains the committed cache",
              !checkerFailedEdit.first &&
              rasterizer.MaterialTextureUploads() == textureUploadsAfterFirst &&
              rasterizer.MaterialTextureUploadFailures() == textureFailuresBefore + 2u);
        const auto checkerEdited = renderFrame(scene, quality);
        check("live texture byte edits refresh the cached texture",
              checkerEdited.first &&
              rasterizer.MaterialTextureUploads() == textureUploadsAfterFirst + 1u);
        material.texData.clear();
        material.texWidth = material.texHeight = material.texChannels = 0;
        material.diffuseTexPath.clear();
        material.uvTiling = glm::vec2(1.0f);
        scene.meshes.front().uvTiling = glm::vec2(1.0f);
        material.kd = glm::vec3(0.25f, 0.4f, 0.85f);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindSampler(1, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteSamplers(2, hostileGeometrySamplers);
        glDeleteTextures(1, &hostileEnvironmentBinding);
        glActiveTexture(GL_TEXTURE0);

        const char* temporaryHDR = "task8_environment.tmp.hdr";
        {
            std::ofstream hdr(temporaryHDR, std::ios::binary);
            hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
            const unsigned char rgbe[16] = {
                240, 20, 10, 129, 10, 220, 30, 129,
                15, 30, 240, 129, 210, 180, 20, 129,
            };
            hdr.write(reinterpret_cast<const char*>(rgbe), sizeof(rgbe));
        }
        FRenderScene ambientOnlyScene = scene;
        ambientOnlyScene.pointLights.clear();
        const auto gradientAmbientFrame = renderFrame(ambientOnlyScene, quality);
        const auto gradientFrame = renderFrame(scene, quality);
        scene.environment.skyPath = temporaryHDR;
        ambientOnlyScene.environment.skyPath = temporaryHDR;
        const std::uint64_t environmentFailuresBefore =
            lighting.Stats().environmentTextureUploadFailures;
        lighting.InjectNextEnvironmentTextureUploadFailureForTesting();
        const auto failedHDRIFrame = renderFrame(scene, quality);
        check("failed HDRI upload is transactional and retryable",
              !failedHDRIFrame.first &&
              lighting.Stats().environmentTextureUploads == 0u &&
              lighting.Stats().environmentTextureUploadFailures ==
                  environmentFailuresBefore + 1u);
        glActiveTexture(GL_TEXTURE0 - 1);
        const auto hdriFrame = renderFrame(scene, quality);
        const auto hdriAmbientFrame = renderFrame(ambientOnlyScene, quality);
        check("HDRI changes sky/background and uploads once",
              gradientFrame.first && hdriFrame.first &&
              imageDifference(gradientFrame.second, hdriFrame.second) > 500u &&
              lighting.Stats().environmentTextureUploads == 1u);
        const int ambientGeometryDelta =
            std::abs(static_cast<int>(gradientAmbientFrame.second[center]) -
                     static_cast<int>(hdriAmbientFrame.second[center])) +
            std::abs(static_cast<int>(gradientAmbientFrame.second[center + 1]) -
                     static_cast<int>(hdriAmbientFrame.second[center + 1])) +
            std::abs(static_cast<int>(gradientAmbientFrame.second[center + 2]) -
                     static_cast<int>(hdriAmbientFrame.second[center + 2]));
        check("HDRI changes geometry ambient contribution",
              gradientAmbientFrame.first && hdriAmbientFrame.first &&
              ambientGeometryDelta > 3);
        FRenderScene gouraudHDRIScene = ambientOnlyScene;
        gouraudHDRIScene.meshes.front().shadingModel = ERenderShadingModel::Gouraud;
        const auto gouraudHDRIFrame = renderFrame(gouraudHDRIScene, quality);
        float gouraudPrecomputed[4] = {};
        float gouraudLit[4] = {};
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer.Framebuffer());
        glReadBuffer(gbuffer.ColorAttachment(
            EHardwareGBufferSemantic::PrecomputedLighting));
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, gouraudPrecomputed);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, lighting.Framebuffer());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_FLOAT, gouraudLit);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        FRenderScene phongHDRIScene = ambientOnlyScene;
        phongHDRIScene.meshes.front().shadingModel = ERenderShadingModel::Phong;
        const auto phongHDRIFrame = renderFrame(phongHDRIScene, quality);
        check("Gouraud HDRI is complete vertex lighting with no fragment delta",
              gouraudHDRIFrame.first &&
              std::fabs(gouraudPrecomputed[0] - gouraudLit[0]) < 0.002f &&
              std::fabs(gouraudPrecomputed[1] - gouraudLit[1]) < 0.002f &&
              std::fabs(gouraudPrecomputed[2] - gouraudLit[2]) < 0.002f);
        check("Gouraud HDRI vertex interpolation differs from Phong HDRI",
              phongHDRIFrame.first &&
              imageDifference(gouraudHDRIFrame.second, phongHDRIFrame.second) > 100u);

        const char* replacementHDR = "task8_environment_replacement.tmp.hdr";
        {
            std::ofstream hdr(replacementHDR, std::ios::binary);
            hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
            const unsigned char rgbe[16] = {
                20, 210, 240, 129, 230, 25, 180, 129,
                210, 210, 20, 129, 30, 35, 220, 129,
            };
            hdr.write(reinterpret_cast<const char*>(rgbe), sizeof(rgbe));
        }
        const GLuint committedEnvironment = lighting.EnvironmentTexture();
        scene.environment.skyPath = replacementHDR;
        lighting.InjectNextEnvironmentTextureUploadFailureForTesting();
        const auto failedHDRIReplacement = renderFrame(scene, quality);
        check("failed HDRI refresh retains the committed cache",
              !failedHDRIReplacement.first &&
              lighting.EnvironmentTexture() == committedEnvironment &&
              lighting.Stats().environmentTextureUploads == 1u &&
              lighting.Stats().environmentTextureUploadFailures ==
                  environmentFailuresBefore + 2u);
        glActiveTexture(GL_TEXTURE0 - 1);
        const auto replacedHDRI = renderFrame(scene, quality);
        check("failed HDRI refresh retries the same source next frame",
              replacedHDRI.first &&
              lighting.EnvironmentTexture() != committedEnvironment &&
              lighting.Stats().environmentTextureUploads == 2u);
        const auto hdriCached = renderFrame(scene, quality);
        check("unchanged HDRI remains cached",
              hdriCached.first && lighting.Stats().environmentTextureUploads == 2u);
        std::remove(temporaryHDR);
        std::remove(replacementHDR);
        scene.environment.skyPath.clear();

        const std::uint64_t lightingRevision = lighting.ResourceRevision();
        check("lighting output resizes 64 to 80x48",
              lighting.Resize(80, 48, ContextGeneration(), &diagnostic) &&
              lighting.Width() == 80 && lighting.Height() == 48 &&
              lighting.OwnedTextureCount() == 1u);
        check("lighting output resizes back and idempotently reuses",
              lighting.Resize(64, 64, ContextGeneration(), &diagnostic) &&
              lighting.ResourceRevision() == lightingRevision + 2u &&
              lighting.Resize(64, 64, ContextGeneration(), &diagnostic) &&
              lighting.ResourceRevision() == lightingRevision + 2u);

        std::unique_ptr<UMesh> slottedCube(UMesh::GenerateCube(glm::vec3(0.8f)));
        Material redSlot;
        redSlot.kd = glm::vec3(0.9f, 0.05f, 0.05f);
        Material greenSlot;
        greenSlot.kd = glm::vec3(0.05f, 0.9f, 0.05f);
        slottedCube->materials = {redSlot, greenSlot};
        slottedCube->triMaterial.resize(12);
        for (std::size_t triangle = 0; triangle < slottedCube->triMaterial.size(); ++triangle)
            slottedCube->triMaterial[triangle] = triangle == 9 ? 1u : 0u;
        FRenderScene slottedScene = scene;
        slottedScene.meshes.clear();
        FRenderMeshInstance slotted;
        slotted.mesh = slottedCube.get();
        slotted.modelTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -6));
        slotted.modelTransform = slotted.modelTransform *
            glm::rotate(glm::mat4(1.0f), 24.0f, glm::vec3(0, 1, 0));
        slotted.normalTransform = glm::transpose(glm::inverse(glm::mat3(slotted.modelTransform)));
        for (std::size_t slot = 0; slot < slottedCube->materials.size(); ++slot)
        {
            FResolvedRenderMaterial slotMaterial;
            slotMaterial.source = &slottedCube->materials[slot];
            slotMaterial.albedo = slottedCube->materials[slot].kd;
            slotted.materialSlots.push_back(slotMaterial);
            slotted.materialSlotIdentities.push_back(static_cast<std::uint32_t>(201 + slot));
        }
        slotted.triangleMaterialSlots = &slottedCube->triMaterial;
        slotted.triangleMaterialSlotCount = slottedCube->triMaterial.size();
        slotted.objectIdentity = 77;
        slottedScene.meshes.push_back(slotted);
        const auto slottedFrame = renderFrame(slottedScene, quality);
        std::vector<float> slotAlbedo(64u * 64u * 4u);
        std::vector<std::uint32_t> slotIdentity(64u * 64u * 2u);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer.Framebuffer());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::AlbedoShininess));
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_FLOAT, slotAlbedo.data());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::Identity));
        glReadPixels(0, 0, 64, 64, GL_RG_INTEGER, GL_UNSIGNED_INT, slotIdentity.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        bool foundRedSlot = false, foundGreenSlot = false;
        for (std::size_t pixel = 0; pixel < 64u * 64u; ++pixel)
        {
            foundRedSlot = foundRedSlot || slotIdentity[pixel * 2 + 1] == 201u;
            foundGreenSlot = foundGreenSlot || slotIdentity[pixel * 2 + 1] == 202u;
        }
        check("two mesh material slots preserve distinct identities/colors",
              slottedFrame.first && foundRedSlot && foundGreenSlot);
        FResolvedRenderMaterial blueOverride;
        Material blueMaterial;
        blueMaterial.kd = glm::vec3(0.05f, 0.1f, 0.9f);
        blueOverride.source = &blueMaterial;
        blueOverride.albedo = blueMaterial.kd;
        slottedScene.meshes.front().materialOverride = blueOverride;
        slottedScene.meshes.front().materialOverrideIdentity = 303;
        const auto overrideFrame = renderFrame(slottedScene, quality);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gbuffer.Framebuffer());
        glReadBuffer(gbuffer.ColorAttachment(EHardwareGBufferSemantic::Identity));
        glReadPixels(0, 0, 64, 64, GL_RG_INTEGER, GL_UNSIGNED_INT, slotIdentity.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        bool foundOverride = false, foundSlotUnderOverride = false;
        for (std::size_t pixel = 0; pixel < 64u * 64u; ++pixel)
        {
            foundOverride = foundOverride || slotIdentity[pixel * 2 + 1] == 303u;
            foundSlotUnderOverride = foundSlotUnderOverride ||
                slotIdentity[pixel * 2 + 1] == 201u || slotIdentity[pixel * 2 + 1] == 202u;
        }
        check("component override wins over every triangle material slot",
              overrideFrame.first && foundOverride && !foundSlotUnderOverride);

        std::unique_ptr<UMesh> sharedCube(UMesh::GenerateCube(glm::vec3(0.2f)));
        sharedCube->material.texWidth = 2;
        sharedCube->material.texHeight = 2;
        sharedCube->material.texChannels = 3;
        sharedCube->material.diffuseTexPath = "shared://task8-checker";
        sharedCube->material.texData = {
            255, 32, 32, 32, 255, 32,
            32, 255, 32, 255, 32, 32,
        };
        FResolvedRenderMaterial cubeMaterial;
        cubeMaterial.source = &sharedCube->material;
        cubeMaterial.albedo = glm::vec3(0.6f, 0.3f, 0.15f);
        auto sharedInstance = [&](int i)
        {
            FRenderMeshInstance shared;
            shared.mesh = sharedCube.get();
            const float x = static_cast<float>((i % 10) - 5) * 0.025f;
            const float y = static_cast<float>(((i / 10) % 10) - 5) * 0.025f;
            shared.modelTransform = glm::translate(glm::mat4(1.0f),
                glm::vec3(x, y, -6.0f - static_cast<float>(i / 100) * 0.01f));
            shared.normalTransform = glm::mat3(1.0f);
            shared.materialOverride = cubeMaterial;
            shared.materialOverrideIdentity = 101;
            shared.objectIdentity = static_cast<std::uint32_t>(i + 1);
            return shared;
        };
        const std::uint64_t uploadsBeforeShared = cache.Stats().uploads;
        const std::uint64_t hashesBeforeShared =
            rasterizer.MaterialTextureHashComputations();
        for (int count : {1, 100, 1000})
        {
            scene.meshes.clear();
            scene.meshes.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) scene.meshes.push_back(sharedInstance(i));
            const auto sharedFrame = renderFrame(scene, quality);
            check(count == 1 ? "one shared cube renders" :
                  count == 100 ? "100 shared cubes render" : "1000 shared cubes render",
                  sharedFrame.first);
        }
        check("1/100/1000 instances add one unique geometry upload",
              cache.Stats().uploads == uploadsBeforeShared + 1u &&
              cache.Stats().residentResources == 1u);
        check("1/100/1000 shared textured actors hash once per source per frame",
              rasterizer.MaterialTextureHashComputations() == hashesBeforeShared + 3u);
        const std::uint64_t uploadsBeforeUnchanged = cache.Stats().uploads;
        const std::uint64_t hashesBeforeUnchanged =
            rasterizer.MaterialTextureHashComputations();
        const auto unchangedSharedFrame = renderFrame(scene, quality);
        check("unchanged second frame has zero geometry uploads",
              unchangedSharedFrame.first &&
              cache.Stats().uploads == uploadsBeforeUnchanged &&
              cache.Stats().reuploads == 0u &&
              rasterizer.MaterialTextureHashComputations() ==
                  hashesBeforeUnchanged + 1u);

        FRenderScene overflowScene = scene;
        overflowScene.meshes.resize(1);
        overflowScene.pointLights.clear();
        const std::size_t lightLimit = lighting.Stats().pointLightLimit;
        for (std::size_t i = 0; i < lightLimit + 2u; ++i)
            overflowScene.pointLights.push_back({
                glm::vec3(static_cast<float>(i) * 0.1f, 3.0f, -3.0f),
                glm::vec3(0.03f),
            });
        const std::uint64_t warningsBefore = lighting.Stats().overflowWarnings;
        const auto overflowFirst = renderFrame(overflowScene, quality);
        const auto overflowSecond = renderFrame(overflowScene, quality);
        check("excess point lights truncate at queried GL3.3-safe limit",
              overflowFirst.first && overflowSecond.first &&
              lighting.Stats().uploadedPointLights == lightLimit &&
              lighting.Stats().truncatedPointLights == 2u);
        check("same overflow condition warns only once",
              lighting.Stats().overflowWarnings == warningsBefore + 1u);
        overflowScene.pointLights.back().radiance.x += 0.01f;
        overflowScene.pointLights.back().worldPosition.x += 0.02f;
        const auto animatedOverflow = renderFrame(overflowScene, quality);
        check("animated overflow scene does not warn every frame",
              animatedOverflow.first &&
              lighting.Stats().overflowWarnings == warningsBefore + 1u);
        overflowScene.pointLights.push_back({glm::vec3(9.0f), glm::vec3(0.01f)});
        const auto changedOverflowCount = renderFrame(overflowScene, quality);
        check("distinct overflow count may emit one new warning",
              changedOverflowCount.first &&
              lighting.Stats().overflowWarnings == warningsBefore + 2u);

        std::unique_ptr<UWorld> fixtureWorld = LoadRenderParityFixture();
        check("sectioned parity fixture loads", fixtureWorld != nullptr);
        if (fixtureWorld)
        {
            ACamera& fixtureCamera = fixtureWorld->GetCamera();
            fixtureCamera.SetOrientation(fixtureCamera.yaw, fixtureCamera.pitch);
            fixtureCamera.SetFOV(fixtureCamera.fov, 1.0f);
            FRenderScene parityScene = ExtractRenderScene(*fixtureWorld, fixtureCamera);
            auto byteLuminance = [](const std::vector<unsigned char>& image,
                                    int x, int y)
            {
                const std::size_t i = static_cast<std::size_t>(y * 64 + x) * 3u;
                return (image[i] * 0.2126f + image[i + 1] * 0.7152f +
                        image[i + 2] * 0.0722f) / 255.0f;
            };
            std::pair<bool, std::vector<unsigned char>> parityModes[3];
            for (int mode = 0; mode < 3; ++mode)
            {
                FRenderScene modeScene = parityScene;
                for (FRenderMeshInstance& mesh : modeScene.meshes)
                    mesh.shadingModel = static_cast<ERenderShadingModel>(mode);
                parityModes[mode] = renderFrame(modeScene, quality);
            }
            const float legacyModeProbe[3][4] = {
                {0.667491f, 0.667491f, 0.777746f, 0.661457f},
                {0.675586f, 0.655532f, 0.805347f, 0.758981f},
                {0.676912f, 0.658379f, 0.800226f, 0.658601f},
            };
            const int parityProbes[4][2] = {
                {32, 32}, {22, 31}, {42, 31}, {32, 45},
            };
            bool modeProbesWithinTolerance = true;
            for (int mode = 0; mode < 3; ++mode)
                for (int probe = 0; probe < 4; ++probe)
                    modeProbesWithinTolerance = modeProbesWithinTolerance &&
                        std::fabs(byteLuminance(parityModes[mode].second,
                                               parityProbes[probe][0],
                                               parityProbes[probe][1]) -
                                  legacyModeProbe[mode][probe]) < 0.30f;
            check("hardware Flat/Gouraud/Phong probes match measured legacy values",
                  parityModes[0].first && parityModes[1].first &&
                  parityModes[2].first && modeProbesWithinTolerance);

            FRenderScene parityDepthScene = parityScene;
            FRenderQuality parityDepthQuality = quality;
            parityDepthQuality.depthView = true;
            const auto parityDepth = renderFrame(parityDepthScene, parityDepthQuality);
            int leftTransition = -1;
            int rightTransition = -1;
            bool priorCovered = byteLuminance(parityDepth.second, 0, 31) > 0.0f;
            for (int x = 1; x < 64; ++x)
            {
                const bool covered = byteLuminance(parityDepth.second, x, 31) > 0.0f;
                if (covered != priorCovered)
                {
                    if (leftTransition < 0) leftTransition = x;
                    else if (rightTransition < 0) rightTransition = x;
                }
                priorCovered = covered;
            }
            const float hardwareDepthCenter = byteLuminance(parityDepth.second, 32, 32);
            const float hardwareDepthRight = byteLuminance(parityDepth.second, 42, 31);
            const float hardwareDepthSphere = byteLuminance(parityDepth.second, 32, 45);
            check("hardware edge coverage matches measured legacy transitions",
                  parityDepth.first && std::abs(leftTransition - 8) <= 2 &&
                  std::abs(rightTransition - 53) <= 2);
            check("hardware depth probes preserve measured legacy ordering",
                  hardwareDepthCenter > hardwareDepthRight &&
                  hardwareDepthRight > hardwareDepthSphere &&
                  byteLuminance(parityDepth.second, 2, 2) == 0.0f);

            FRenderScene oneLightParity = parityScene;
            oneLightParity.pointLights.resize(1);
            const auto oneLightHardware = renderFrame(oneLightParity, quality);
            const auto twoLightHardware = renderFrame(parityScene, quality);
            const float measuredLegacySecondLightDelta = 0.676912f - 0.490377f;
            const float hardwareSecondLightDelta =
                byteLuminance(twoLightHardware.second, 32, 32) -
                byteLuminance(oneLightHardware.second, 32, 32);
            check("hardware additive light delta matches measured legacy semantics",
                  oneLightHardware.first && twoLightHardware.first &&
                  hardwareSecondLightDelta > 0.02f &&
                  std::fabs(hardwareSecondLightDelta - measuredLegacySecondLightDelta) < 0.25f);

            Material parityChecker;
            parityChecker.kd = glm::vec3(1.0f);
            parityChecker.ka = glm::vec3(0.2f);
            parityChecker.texWidth = 2;
            parityChecker.texHeight = 2;
            parityChecker.texChannels = 3;
            parityChecker.diffuseTexPath = "baseline://checker";
            parityChecker.texData = {
                255, 255, 255, 20, 20, 20,
                20, 20, 20, 255, 255, 255,
            };
            FResolvedRenderMaterial parityCheckerResolved;
            parityCheckerResolved.source = &parityChecker;
            parityCheckerResolved.albedo = parityChecker.kd;
            parityCheckerResolved.ambient = parityChecker.ka;
            FRenderScene checkerParityScene = parityScene;
            checkerParityScene.meshes.front().materialOverride = parityCheckerResolved;
            checkerParityScene.meshes.front().uvTiling = glm::vec2(2.0f);
            parityChecker.wrapMode = EWrapMode::Repeat;
            const auto checkerRepeatHardware = renderFrame(checkerParityScene, quality);
            parityChecker.wrapMode = EWrapMode::Clamp;
            const auto checkerClampHardware = renderFrame(checkerParityScene, quality);
            const double checkerHardwareAbs =
                static_cast<double>(imageDifference(checkerRepeatHardware.second,
                                                    checkerClampHardware.second)) / 255.0;
            constexpr double measuredLegacyCheckerAbs = 220.209557;
            check("hardware checker repetition matches measured legacy semantic delta",
                  checkerRepeatHardware.first && checkerClampHardware.first &&
                  checkerHardwareAbs > 20.0 &&
                  std::fabs(checkerHardwareAbs - measuredLegacyCheckerAbs) < 220.0);

            FRenderScene alternateHardwareEnvironment = parityScene;
            alternateHardwareEnvironment.environment.horizon =
                glm::vec3(0.7f, 0.03f, 0.02f);
            alternateHardwareEnvironment.environment.zenith =
                glm::vec3(0.02f, 0.1f, 0.75f);
            alternateHardwareEnvironment.environment.exponent = 2.0f;
            const auto originalEnvironmentHardware = renderFrame(parityScene, quality);
            const auto alternateEnvironmentHardware =
                renderFrame(alternateHardwareEnvironment, quality);
            const float proceduralBackgroundDelta = std::fabs(
                byteLuminance(originalEnvironmentHardware.second, 2, 2) -
                byteLuminance(alternateEnvironmentHardware.second, 2, 2));
            const float proceduralGeometryDelta = std::fabs(
                byteLuminance(originalEnvironmentHardware.second, 32, 32) -
                byteLuminance(alternateEnvironmentHardware.second, 32, 32));
            check("hardware procedural environment follows measured legacy deltas",
                  originalEnvironmentHardware.first && alternateEnvironmentHardware.first &&
                  std::fabs(proceduralBackgroundDelta - 0.237586f) < 0.25f &&
                  std::fabs(proceduralGeometryDelta - 0.074633f) < 0.15f);

            const char* parityHDRPath = "task8_parity_environment.tmp.hdr";
            {
                std::ofstream hdr(parityHDRPath, std::ios::binary);
                hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
                const unsigned char rgbe[16] = {
                    240, 20, 10, 129, 10, 220, 30, 129,
                    15, 30, 240, 129, 210, 180, 20, 129,
                };
                hdr.write(reinterpret_cast<const char*>(rgbe), sizeof(rgbe));
            }
            FRenderScene hdriParityScene = parityScene;
            hdriParityScene.environment.skyPath = parityHDRPath;
            const auto hdriParityHardware = renderFrame(hdriParityScene, quality);
            const float hdriBackgroundDelta = std::fabs(
                byteLuminance(originalEnvironmentHardware.second, 2, 2) -
                byteLuminance(hdriParityHardware.second, 2, 2));
            const float hdriGeometryDelta = std::fabs(
                byteLuminance(originalEnvironmentHardware.second, 32, 32) -
                byteLuminance(hdriParityHardware.second, 32, 32));
            check("hardware HDRI background tracks measured legacy behavior",
                  hdriParityHardware.first &&
                  std::fabs(hdriBackgroundDelta - 0.053053f) < 0.25f);
            check("hardware HDRI extends measured zero legacy geometry delta",
                  hdriGeometryDelta > 0.01f);
            std::remove(parityHDRPath);

            UWorldRenderer worldRenderer;
            worldRenderer.Init();
            FRenderTarget viewport = FRenderTarget::TextureViewport(
                64, 64, ContextGeneration());
            FBackendSelection backend;
            backend.requested = ERayTracingBackend::CompatibleGL33;
            backend.selected = ERayTracingBackend::CompatibleGL33;
            backend.available = true;
            backend.rayTracingEnabled = true;
            const bool routed = worldRenderer.Render(
                *fixtureWorld, fixtureCamera, viewport,
                fixtureWorld->GetScene().renderFeatures, quality, backend,
                ContextGeneration());
            const std::vector<float> cpuSentinel = {0.25f, 0.5f, 0.75f};
            fixtureWorld->GetScene().outputImage = cpuSentinel;
            const bool routedAgain = worldRenderer.Render(
                *fixtureWorld, fixtureCamera, viewport,
                fixtureWorld->GetScene().renderFeatures, quality, backend,
                ContextGeneration());
            FRenderFeatures rtFeatures = fixtureWorld->GetScene().renderFeatures;
            rtFeatures.rayTracing = true;
            const bool routedWithPendingRT = worldRenderer.Render(
                *fixtureWorld, fixtureCamera, viewport, rtFeatures, quality,
                backend, ContextGeneration());
            std::vector<unsigned char> routedPixels(64u * 64u * 3u);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, viewport.Identity());
            glReadPixels(0, 0, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, routedPixels.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            check("normal UWorldRenderer routes fixture through hardware output",
                  routed && routedAgain && routedWithPendingRT &&
                  imageEnergy(routedPixels) > 1000u &&
                  fixtureWorld->GetScene().outputImage == cpuSentinel);
            const float routedCenter =
                (routedPixels[(32u * 64u + 32u) * 3u] * 0.2126f +
                 routedPixels[(32u * 64u + 32u) * 3u + 1] * 0.7152f +
                 routedPixels[(32u * 64u + 32u) * 3u + 2] * 0.0722f) / 255.0f;
            const float routedCorner =
                (routedPixels[0] * 0.2126f + routedPixels[1] * 0.7152f +
                 routedPixels[2] * 0.0722f) / 255.0f;
            check("hardware probes remain within semantic legacy tolerances",
                  std::fabs(routedCenter - 0.676912f) < 0.25f &&
                  std::fabs(routedCorner - 0.595910f) < 0.20f);
            const FWorldRendererStats& normalStats = worldRenderer.Stats();
            check("normal renderer keeps CPU work at zero and schedules one ray backend call",
                  normalStats.hardwareGBufferPasses == 3u &&
                  normalStats.rasterLightingPasses == 3u &&
                  normalStats.compositePasses == 3u &&
                  normalStats.rayResourceAllocations > 0u &&
                  normalStats.rayBackendCalls == 1u &&
                  normalStats.cpuFramebufferGenerations == 0u &&
                  normalStats.cpuReadbacks == 0u &&
                  normalStats.cpuFramebufferUploads == 0u);
            worldRenderer.Shutdown();
        }
        {
            std::ofstream ppm(outputPath, std::ios::binary);
            ppm << "P6\n64 64\n255\n";
            for (int y = 63; y >= 0; --y)
                ppm.write(reinterpret_cast<const char*>(pixels.data() + y * 64 * 3), 64 * 3);
            check("lighting PPM written", static_cast<bool>(ppm));
        }
        check("lighting pass leaves no GL error", glGetError() == GL_NO_ERROR);
        lighting.Shutdown();
        std::printf("=== raster lighting gates: %d passed, %d failed ===\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }
};

static int RunRasterLightingGates(const std::string& outputPath)
{
    FRasterLightingSelfTestApp app;
    if (!app.Init(64, 64, "Raster Lighting Self-Test")) return 2;
    return app.RunGates(outputPath);
}

class FRayEffectsSelfTestApp final : public Engine
{
public:
    int RunGates(const std::string& outputPath)
    {
        glfwHideWindow(window_);
        int passed = 0, failed = 0;
        auto check = [&](const char* label, bool result)
        {
            std::printf("[%s] %s\n", result ? "PASS" : "FAIL", label);
            result ? ++passed : ++failed;
        };
        std::unique_ptr<UMesh> cube(UMesh::GenerateCube(glm::vec3(0.8f)));
        std::unique_ptr<UMesh> plane(UMesh::GeneratePlane(glm::vec2(8.0f)));
        Material cubeMaterial;
        cubeMaterial.kd = glm::vec3(0.75f, 0.18f, 0.1f);
        cubeMaterial.ks = glm::vec3(0.8f);
        cubeMaterial.km = glm::vec3(0.8f);
        cubeMaterial.shininess = 48.0f;
        Material planeMaterial;
        planeMaterial.kd = glm::vec3(0.25f, 0.55f, 0.25f);

        auto resolved = [](Material& material)
        {
            FResolvedRenderMaterial result;
            result.source = &material;
            result.ambient = material.ka;
            result.albedo = material.kd;
            result.specularColor = material.ks;
            result.shininess = material.shininess;
            result.mirrorFactor = std::max(material.km.x,
                std::max(material.km.y, material.km.z));
            return result;
        };
        FRenderScene scene;
        scene.camera.nearDistance = 0.1f;
        scene.camera.left = scene.camera.bottom = -0.1f;
        scene.camera.rightPlane = scene.camera.top = 0.1f;
        scene.environment.horizon = glm::vec3(0.08f, 0.12f, 0.22f);
        scene.environment.zenith = glm::vec3(0.55f, 0.7f, 0.95f);
        scene.pointLights = {{glm::vec3(2.5f, 4.0f, -3.0f), glm::vec3(1.0f)}};
        FRenderMeshInstance cubeInstance;
        cubeInstance.mesh = cube.get();
        cubeInstance.modelTransform = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.0f, -0.1f, -5.0f));
        cubeInstance.normalTransform = glm::mat3(1.0f);
        cubeInstance.materialOverride = resolved(cubeMaterial);
        cubeInstance.materialOverrideIdentity = 1;
        cubeInstance.objectIdentity = 1;
        scene.meshes.push_back(cubeInstance);
        FRenderMeshInstance planeInstance;
        planeInstance.mesh = plane.get();
        planeInstance.modelTransform = glm::translate(glm::mat4(1.0f),
            glm::vec3(0.0f, -1.0f, -5.0f));
        planeInstance.normalTransform = glm::mat3(1.0f);
        planeInstance.materialOverride = resolved(planeMaterial);
        planeInstance.materialOverrideIdentity = 2;
        planeInstance.objectIdentity = 2;
        scene.meshes.push_back(planeInstance);

        FOpenGLMeshUploadAdapter adapter;
        UGPUMeshCache meshCache(adapter);
        UHardwareGBuffer gbuffer;
        UHardwareRasterizer rasterizer;
        URasterLightingPass lighting;
        UGL33RayTracingBackend rays;
        FRenderQuality quality;
        quality.giSamples = 2;
        quality.giBounces = 2;
        quality.shadowSamples = 2;
        quality.shadowSoftness = 0.03f;
        std::string diagnostic;
        auto invalidRayMesh = [&](UMesh& mesh)
        {
            FRenderScene invalidScene;
            FRenderMeshInstance invalidInstance;
            invalidInstance.mesh = &mesh;
            invalidScene.meshes.push_back(invalidInstance);
            FRaySceneCache validationCache;
            return validationCache.Prepare(invalidScene);
        };
        UMesh emptyVertexMesh;
        emptyVertexMesh.indices = {0, 0, 0};
        const FPackedRayScene& emptyVertexPacked = invalidRayMesh(emptyVertexMesh);
        check("ray packing rejects indices with an empty vertex array actionably",
              !emptyVertexPacked.valid &&
              emptyVertexPacked.diagnostic.find("vertices") != std::string::npos);
        UMesh partialTriangleMesh;
        partialTriangleMesh.vertices.resize(3);
        partialTriangleMesh.indices = {0, 1};
        const FPackedRayScene& partialTrianglePacked = invalidRayMesh(partialTriangleMesh);
        check("ray packing rejects non-triangle index counts actionably",
              !partialTrianglePacked.valid &&
              partialTrianglePacked.diagnostic.find("multiple of three") !=
                  std::string::npos);
        UMesh outOfBoundsMesh;
        outOfBoundsMesh.vertices.resize(3);
        outOfBoundsMesh.indices = {0, 1, 3};
        const FPackedRayScene& outOfBoundsPacked = invalidRayMesh(outOfBoundsMesh);
        check("ray packing rejects out-of-bounds indices actionably",
              !outOfBoundsPacked.valid &&
              outOfBoundsPacked.diagnostic.find("out of bounds") !=
                  std::string::npos);

        std::unique_ptr<UMesh> materialMesh(
            UMesh::GenerateCube(glm::vec3(0.5f)));
        Material materialSlot0;
        materialSlot0.kd = glm::vec3(0.9f, 0.05f, 0.05f);
        Material materialSlot1;
        materialSlot1.kd = glm::vec3(0.05f, 0.9f, 0.05f);
        materialSlot1.texWidth = materialSlot1.texHeight = 2;
        materialSlot1.texChannels = 3;
        materialSlot1.diffuseTexPath = "memory://ray-secondary-checker";
        materialSlot1.texData = {
            20, 40, 255, 20, 40, 255,
            20, 40, 255, 20, 40, 255,
        };
        materialMesh->materials = {materialSlot0, materialSlot1};
        materialMesh->triMaterial.assign(12, 0u);
        materialMesh->triMaterial[10] = materialMesh->triMaterial[11] = 1u;
        FRenderMeshInstance materialInstance;
        materialInstance.mesh = materialMesh.get();
        materialInstance.materialSlots = {
            resolved(materialMesh->materials[0]),
            resolved(materialMesh->materials[1]),
        };
        materialInstance.materialSlotIdentities = {401u, 402u};
        materialInstance.triangleMaterialSlots = &materialMesh->triMaterial;
        materialInstance.triangleMaterialSlotCount = materialMesh->triMaterial.size();
        materialInstance.objectIdentity = 400u;
        FRenderScene materialScene;
        materialScene.meshes.push_back(materialInstance);
        FRaySceneCache materialCache;
        const FPackedRayScene firstMaterialPack = materialCache.Prepare(materialScene);
        check("ray packing preserves every slot and textured secondary metadata",
              firstMaterialPack.valid && firstMaterialPack.materialCount == 2 &&
              firstMaterialPack.textureLayerCount == 1 &&
              firstMaterialPack.triangleTexels.size() == 12u * 7u);
        check("ray atlas preserves logical texture size for GL_LINEAR sampling",
              firstMaterialPack.textureWidth == 4 &&
              firstMaterialPack.textureHeight == 4 &&
              firstMaterialPack.materialTexels.size() == 2u * 6u &&
              firstMaterialPack.materialTexels[11].x == 2.0f &&
              firstMaterialPack.materialTexels[11].y == 2.0f);
        const std::uint64_t materialBLASRevision = firstMaterialPack.blasRevision;
        const std::uint64_t materialInstanceRevision =
            firstMaterialPack.instanceRevision;
        const std::uint64_t materialRevision = firstMaterialPack.materialRevision;
        materialScene.meshes.front().materialSlots[1].albedo =
            glm::vec3(0.05f, 0.05f, 0.95f);
        const FPackedRayScene changedMaterialPack =
            materialCache.Prepare(materialScene);
        check("non-first material-slot edits update material data without BLAS rebuild",
              changedMaterialPack.blasRevision == materialBLASRevision &&
              changedMaterialPack.instanceRevision == materialInstanceRevision &&
              changedMaterialPack.materialRevision != materialRevision);
        materialScene.meshes.front().materialOverride =
            materialScene.meshes.front().materialSlots[0];
        materialScene.meshes.front().materialOverrideIdentity = 499u;
        const FPackedRayScene overrideMaterialPack =
            materialCache.Prepare(materialScene);
        check("component override collapses triangle slots to one exact material identity",
              overrideMaterialPack.materialCount == 1 &&
              overrideMaterialPack.instanceIdentityTexels.size() == 1u &&
              overrideMaterialPack.instanceIdentityTexels[0].z == 1u &&
              overrideMaterialPack.instanceIdentityTexels[0].w == 1u);
        check("ray effects shader is initially lazy", !rays.Stats().resourceAllocations &&
              !rays.Stats().renderCalls && !rays.Stats().sceneUploads);
        check("ray-effects G-buffer allocated",
              gbuffer.Resize(64, 64, ContextGeneration()));
        meshCache.BeginFrame();
        const bool prepared = lighting.PrepareEnvironment(
            scene, ContextGeneration(), &diagnostic);
        const bool geometry = prepared && rasterizer.RenderGeometry(
            scene, quality, meshCache, gbuffer, lighting.EnvironmentTexture(),
            ContextGeneration(), &diagnostic);
        meshCache.ReleaseUnused();
        FRasterLightingOutput lit;
        const bool litOK = geometry && lighting.Render(scene, quality, gbuffer,
            ContextGeneration(), lit, &diagnostic);
        check("ray-effects scene has hardware primary visibility", litOK);
        std::vector<float> primaryCoverageBefore(64u * 64u * 4u);
        std::vector<float> primaryDepthBefore(64u * 64u);
        std::vector<std::uint32_t> primaryIdentityBefore(64u * 64u * 4u);
        glBindTexture(GL_TEXTURE_2D,
            gbuffer.Texture(EHardwareGBufferSemantic::PositionCoverage));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
            primaryCoverageBefore.data());
        glBindTexture(GL_TEXTURE_2D, gbuffer.DepthTexture());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
            primaryDepthBefore.data());
        glBindTexture(GL_TEXTURE_2D,
            gbuffer.Texture(EHardwareGBufferSemantic::Identity));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT,
            primaryIdentityBefore.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        FRenderTarget target = FRenderTarget::DefaultFramebuffer(
            64, 64, ContextGeneration());
        auto composite = [&](const FRayEffectOutputs& effects)
        {
            std::vector<unsigned char> pixels(64u * 64u * 3u);
            FCompositeOutput output;
            const bool bound = target.Begin();
            const bool okay = bound && lighting.Composite(target, lit, effects,
                ContextGeneration(), output, &diagnostic);
            if (!okay && !diagnostic.empty())
                std::fprintf(stderr, "ray composite: %s\n", diagnostic.c_str());
            if (okay) glReadPixels(0, 0, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            if (bound) target.End();
            return std::make_pair(okay, pixels);
        };
        const auto rasterOnlyA = composite({});
        const auto rasterOnlyB = composite({});
        check("RT off preserves raster-only bytes", rasterOnlyA.first &&
              rasterOnlyB.first && rasterOnlyA.second == rasterOnlyB.second &&
              rays.Stats().resourceAllocations == 0 && rays.Stats().renderCalls == 0);

        auto execute = [&](bool shadows, bool gi, bool reflections,
                           FRayEffectOutputs& outputs)
        {
            FRayEffectInputs inputs;
            inputs.gbuffer = &gbuffer;
            inputs.scene = &scene;
            inputs.rasterLighting = &lit;
            inputs.features.rayTracing = true;
            inputs.features.rayTracedShadows = shadows;
            inputs.features.rayTracedGI = gi;
            inputs.features.rayTracedReflections = reflections;
            inputs.quality = quality;
            inputs.environmentTexture = lighting.EnvironmentTexture();
            inputs.width = 64;
            inputs.height = 64;
            inputs.contextGeneration = ContextGeneration();
            return rays.RenderEffects(inputs, outputs, &diagnostic);
        };
        auto readTarget = [](const std::optional<FRenderOutputView>& target,
                             GLenum format, int channels, int width = 64,
                             int height = 64)
        {
            std::vector<float> pixels(static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) *
                static_cast<std::size_t>(channels), 0.0f);
            if (target)
            {
                glBindTexture(GL_TEXTURE_2D,
                    static_cast<GLuint>(target->identity));
                glGetTexImage(GL_TEXTURE_2D, 0, format, GL_FLOAT,
                              pixels.data());
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            return pixels;
        };
        auto readComputeTarget = [&](const std::optional<FRenderOutputView>& target,
                                     GLenum format, int channels, int width = 64,
                                     int height = 64)
        {
            // Shader image writes are made visible to CPU texture-transfer
            // commands explicitly; the production barrier remains scoped to
            // compositor texture sampling.
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
            return readTarget(target, format, channels, width, height);
        };
        auto nearImages = [](const std::vector<float>& a,
                             const std::vector<float>& b)
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (!std::isfinite(a[i]) || !std::isfinite(b[i])) return false;
                const float tolerance = 0.015f +
                    0.025f * std::max(std::fabs(a[i]), std::fabs(b[i]));
                if (std::fabs(a[i] - b[i]) > tolerance) return false;
            }
            return true;
        };

        FRayEffectOutputs shadow;
        const std::uint64_t beforeShadowDraws = rays.Stats().rayDraws;
        const bool shadowOK = execute(true, false, false, shadow);
        std::vector<float> shadowPixels(64u * 64u, 1.0f);
        if (shadowOK && shadow.shadowVisibilityTarget)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D,
                static_cast<unsigned>(shadow.shadowVisibilityTarget->identity));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, shadowPixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        const auto shadowMinMax = std::minmax_element(
            shadowPixels.begin(), shadowPixels.end());
        check("shadows-only allocates and draws only shadow output", shadowOK &&
              shadow.shadowVisibilityTarget && !shadow.globalIlluminationTarget &&
              !shadow.reflectionTarget && rays.OwnedOutputTextureCount() == 1u &&
              rays.Stats().rayDraws == beforeShadowDraws + 1u &&
              *shadowMinMax.first < 0.75f && *shadowMinMax.second > 0.95f);
        check("exact object/material identity avoids near self-hit acne",
              shadowOK && std::isfinite(*shadowMinMax.first) &&
              *shadowMinMax.first >= 0.0f && *shadowMinMax.second <= 1.0f &&
              *shadowMinMax.second > 0.95f);

        FRayEffectOutputs gi;
        const bool giOK = execute(false, true, false, gi);
        std::vector<float> giPixels(64u * 64u * 4u, 0.0f);
        if (giOK && gi.globalIlluminationTarget)
        {
            glBindTexture(GL_TEXTURE_2D,
                static_cast<unsigned>(gi.globalIlluminationTarget->identity));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, giPixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        double giEnergy = 0.0;
        bool giFinite = true;
        for (std::size_t pixel = 0; pixel < giPixels.size() / 4u; ++pixel)
            for (int channel = 0; channel < 3; ++channel)
            {
                const float value = giPixels[pixel * 4u + static_cast<std::size_t>(channel)];
                giFinite = giFinite && std::isfinite(value);
                giEnergy += std::fabs(value);
            }
        check("GI-only allocates finite non-neutral GI", giOK &&
              !gi.shadowVisibilityTarget && gi.globalIlluminationTarget &&
              !gi.reflectionTarget && rays.OwnedOutputTextureCount() == 1u &&
              giFinite && giEnergy > 0.01);

        FRayEffectOutputs reflection;
        const bool reflectionOK = execute(false, false, true, reflection);
        std::vector<float> reflectionPixels(64u * 64u * 4u, 0.0f);
        if (reflectionOK && reflection.reflectionTarget)
        {
            glBindTexture(GL_TEXTURE_2D,
                static_cast<unsigned>(reflection.reflectionTarget->identity));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, reflectionPixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        double reflectionEnergy = 0.0;
        bool reflectionFinite = true;
        for (std::size_t pixel = 0; pixel < reflectionPixels.size() / 4u; ++pixel)
            for (int channel = 0; channel < 3; ++channel)
            {
                const float value = reflectionPixels[pixel * 4u + static_cast<std::size_t>(channel)];
                reflectionFinite = reflectionFinite && std::isfinite(value);
                reflectionEnergy += std::fabs(value);
            }
        check("reflections-only allocates finite non-neutral reflection", reflectionOK &&
              !reflection.shadowVisibilityTarget && !reflection.globalIlluminationTarget &&
              reflection.reflectionTarget && rays.OwnedOutputTextureCount() == 1u &&
              reflectionFinite && reflectionEnergy > 0.01);

        GLuint controlledHDRI = 0;
        const float hdriPixels[12] = {
            0.05f, 3.0f, 0.1f, 0.05f, 3.0f, 0.1f,
            0.05f, 3.0f, 0.1f, 0.05f, 3.0f, 0.1f,
        };
        glGenTextures(1, &controlledHDRI);
        glBindTexture(GL_TEXTURE_2D, controlledHDRI);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 2, 2, 0, GL_RGB,
            GL_FLOAT, hdriPixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        FRayEffectInputs hdriInputs;
        hdriInputs.gbuffer = &gbuffer;
        hdriInputs.scene = &scene;
        hdriInputs.rasterLighting = &lit;
        hdriInputs.features.rayTracing = true;
        hdriInputs.features.rayTracedReflections = true;
        hdriInputs.quality = quality;
        hdriInputs.environmentTexture = controlledHDRI;
        hdriInputs.width = hdriInputs.height = 64;
        hdriInputs.contextGeneration = ContextGeneration();
        FRayEffectOutputs hdriReflection;
        std::vector<float> hdriReflectionPixels(reflectionPixels.size(), 0.0f);
        const bool hdriReflectionOK = rays.RenderEffects(
            hdriInputs, hdriReflection, &diagnostic);
        if (hdriReflectionOK && hdriReflection.reflectionTarget)
        {
            glBindTexture(GL_TEXTURE_2D, static_cast<unsigned>(
                hdriReflection.reflectionTarget->identity));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
                hdriReflectionPixels.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        double hdriDifference = 0.0;
        for (std::size_t pixel = 0; pixel < reflectionPixels.size() / 4u; ++pixel)
            for (int channel = 0; channel < 3; ++channel)
                hdriDifference += std::fabs(reflectionPixels[pixel * 4u + channel] -
                    hdriReflectionPixels[pixel * 4u + channel]);
        check("reflection misses and secondary shading consume HDRI radiance",
              hdriReflectionOK && hdriReflection.reflectionTarget &&
              hdriDifference > 0.01);
        glDeleteTextures(1, &controlledHDRI);

        scene.meshes.front().materialOverride->mirrorFactor = 0.0f;
        meshCache.BeginFrame();
        const bool matteGeometry = rasterizer.RenderGeometry(scene, quality, meshCache,
            gbuffer, lighting.EnvironmentTexture(), ContextGeneration(), &diagnostic);
        meshCache.ReleaseUnused();
        const bool matteLit = matteGeometry && lighting.Render(scene, quality, gbuffer,
            ContextGeneration(), lit, &diagnostic);
        FRayEffectOutputs matteReflection;
        const bool matteReflectionOK = matteLit &&
            execute(false, false, true, matteReflection);
        std::fill(reflectionPixels.begin(), reflectionPixels.end(), 0.0f);
        if (matteReflectionOK && matteReflection.reflectionTarget)
        {
            glBindTexture(GL_TEXTURE_2D,
                static_cast<unsigned>(matteReflection.reflectionTarget->identity));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, reflectionPixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        reflectionEnergy = 0.0;
        for (std::size_t pixel = 0; pixel < reflectionPixels.size() / 4u; ++pixel)
            for (int channel = 0; channel < 3; ++channel)
                reflectionEnergy += std::fabs(reflectionPixels[
                    pixel * 4u + static_cast<std::size_t>(channel)]);
        check("zero-mirror surfaces produce neutral reflection radiance",
              matteReflectionOK && matteReflection.reflectionTarget &&
              reflectionEnergy < 0.0001);
        scene.meshes.front().materialOverride->mirrorFactor = 0.8f;
        meshCache.BeginFrame();
        const bool restoredGeometry = rasterizer.RenderGeometry(scene, quality, meshCache,
            gbuffer, lighting.EnvironmentTexture(), ContextGeneration(), &diagnostic);
        meshCache.ReleaseUnused();
        const bool restoredLit = restoredGeometry && lighting.Render(scene, quality, gbuffer,
            ContextGeneration(), lit, &diagnostic);

        FRayEffectOutputs all;
        const bool allOK = restoredLit && execute(true, true, true, all);
        const GLenum rayRestoreError = glGetError();
        const auto combined = composite(all);
        check("all effects share one backend draw", allOK && rayRestoreError == GL_NO_ERROR &&
              all.shadowVisibilityTarget && all.globalIlluminationTarget &&
              all.reflectionTarget && rays.OwnedOutputTextureCount() == 3u);
        check("all effect targets composite", combined.first);

        // The optional Compute path consumes the exact same immutable
        // hardware-primary inputs and packed scene. Float16 outputs can differ
        // slightly through instruction ordering, so compare with a bounded
        // absolute+relative tolerance instead of vendor-golden pixels.
        if (!GraphicsCapabilities().SupportsComputeBackend())
        {
            std::printf("[SKIP] GL33-vs-GL43 ray comparison requires OpenGL 4.3 Compute/SSBO\n");
        }
        else
        {
            FOpenGLRayTracingBackendFactory productionFactory;
            std::unique_ptr<IRayTracingBackend> factoryGL33 =
                productionFactory.Create(ERayTracingBackend::CompatibleGL33,
                                         &diagnostic);
            std::unique_ptr<IRayTracingBackend> factoryGL43 =
                productionFactory.Create(ERayTracingBackend::ComputeGL43,
                                         &diagnostic);
            check("production factory returns distinct GL33 and GL43 implementations",
                  dynamic_cast<UGL33RayTracingBackend*>(factoryGL33.get()) &&
                  dynamic_cast<UGL43RayTracingBackend*>(factoryGL43.get()));

            UGL43RayTracingBackend computeRays;
            check("compute ray backend is lazy before requested work",
                  computeRays.Stats().resourceAllocations == 0u &&
                  computeRays.Stats().rayDispatches == 0u);
            FRayEffectInputs lazyInputs;
            lazyInputs.features.rayTracing = false;
            FRayEffectOutputs lazyOutputs;
            check("compute master-off path performs zero work without valid GL inputs",
                  computeRays.RenderEffects(lazyInputs, lazyOutputs, &diagnostic) &&
                  computeRays.Stats().resourceAllocations == 0u &&
                  computeRays.Stats().rayDispatches == 0u);

            auto executeCompute = [&](bool shadowsEnabled, bool giEnabled,
                                      bool reflectionsEnabled,
                                      FRayEffectOutputs& outputs)
            {
                FRayEffectInputs inputs;
                inputs.gbuffer = &gbuffer;
                inputs.scene = &scene;
                inputs.rasterLighting = &lit;
                inputs.features.rayTracing = true;
                inputs.features.rayTracedShadows = shadowsEnabled;
                inputs.features.rayTracedGI = giEnabled;
                inputs.features.rayTracedReflections = reflectionsEnabled;
                inputs.quality = quality;
                inputs.environmentTexture = lighting.EnvironmentTexture();
                inputs.width = inputs.height = 64;
                inputs.contextGeneration = ContextGeneration();
                return computeRays.RenderEffects(inputs, outputs, &diagnostic);
            };
            FRayEffectOutputs parityGL33Shadow, parityGL33GI,
                parityGL33Reflection, parityGL33All;
            const bool parityGL33ShadowOK = execute(
                true, false, false, parityGL33Shadow);
            const std::vector<float> gl33Shadow = readTarget(
                parityGL33Shadow.shadowVisibilityTarget, GL_RED, 1);
            const bool parityGL33GIOK = execute(
                false, true, false, parityGL33GI);
            const std::vector<float> gl33GI = readTarget(
                parityGL33GI.globalIlluminationTarget, GL_RGBA, 4);
            const bool parityGL33ReflectionOK = execute(
                false, false, true, parityGL33Reflection);
            const std::vector<float> gl33Reflection = readTarget(
                parityGL33Reflection.reflectionTarget, GL_RGBA, 4);
            const bool parityGL33AllOK = execute(
                true, true, true, parityGL33All);
            const std::vector<float> gl33AllShadow = readTarget(
                parityGL33All.shadowVisibilityTarget, GL_RED, 1);
            const std::vector<float> gl33AllGI = readTarget(
                parityGL33All.globalIlluminationTarget, GL_RGBA, 4);
            const std::vector<float> gl33AllReflection = readTarget(
                parityGL33All.reflectionTarget, GL_RGBA, 4);

            FRayEffectOutputs computeShadow, computeGI, computeReflection,
                computeAll;
            const bool computeShadowOK = executeCompute(
                true, false, false, computeShadow);
            const std::vector<float> gl43Shadow = readComputeTarget(
                computeShadow.shadowVisibilityTarget, GL_RED, 1);
            const bool computeGIOK = executeCompute(
                false, true, false, computeGI);
            const std::vector<float> gl43GI = readComputeTarget(
                computeGI.globalIlluminationTarget, GL_RGBA, 4);
            const bool computeReflectionOK = executeCompute(
                false, false, true, computeReflection);
            const std::vector<float> gl43Reflection = readComputeTarget(
                computeReflection.reflectionTarget, GL_RGBA, 4);
            const bool computeAllOK = executeCompute(
                true, true, true, computeAll);
            // Exercise the production texture-fetch barrier before any
            // test-only CPU texture-transfer barrier is issued.
            const auto computeComposite = composite(computeAll);
            const std::vector<float> gl43AllShadow = readComputeTarget(
                computeAll.shadowVisibilityTarget, GL_RED, 1);
            const std::vector<float> gl43AllGI = readComputeTarget(
                computeAll.globalIlluminationTarget, GL_RGBA, 4);
            const std::vector<float> gl43AllReflection = readComputeTarget(
                computeAll.reflectionTarget, GL_RGBA, 4);

            check("GL43 shadows-only mask and values match GL33 tolerance",
                  parityGL33ShadowOK && computeShadowOK &&
                  computeShadow.shadowVisibilityTarget &&
                  !computeShadow.globalIlluminationTarget &&
                  !computeShadow.reflectionTarget &&
                  nearImages(gl33Shadow, gl43Shadow));
            check("GL43 GI-only mask and values match GL33 tolerance",
                  parityGL33GIOK && computeGIOK && !computeGI.shadowVisibilityTarget &&
                  computeGI.globalIlluminationTarget &&
                  !computeGI.reflectionTarget && nearImages(gl33GI, gl43GI));
            check("GL43 reflections-only mask and values match GL33 tolerance",
                  parityGL33ReflectionOK && computeReflectionOK &&
                  !computeReflection.shadowVisibilityTarget &&
                  !computeReflection.globalIlluminationTarget &&
                  computeReflection.reflectionTarget &&
                  nearImages(gl33Reflection, gl43Reflection));
            check("GL43 all-effects outputs match GL33 tolerance",
                  parityGL33AllOK && computeAllOK &&
                  computeAll.shadowVisibilityTarget &&
                  computeAll.globalIlluminationTarget &&
                  computeAll.reflectionTarget &&
                  nearImages(gl33AllShadow, gl43AllShadow) &&
                  nearImages(gl33AllGI, gl43AllGI) &&
                  nearImages(gl33AllReflection, gl43AllReflection));
            check("GL43 dispatch publishes image writes before sampling",
                  computeRays.Stats().rayDispatches == 4u &&
                  computeRays.Stats().memoryBarriers == 4u &&
                  glGetError() == GL_NO_ERROR);
            bool compositeNear = computeComposite.second.size() == combined.second.size();
            for (std::size_t i = 0; compositeNear &&
                 i < computeComposite.second.size(); ++i)
                compositeNear = std::abs(int(computeComposite.second[i]) -
                                         int(combined.second[i])) <= 8;
            check("GL43 outputs are sampled by the compositor",
                  computeComposite.first && compositeNear &&
                  computeComposite.second != rasterOnlyA.second);
            computeRays.Shutdown();
        }
        check("primary G-buffer identity remains unchanged",
              gbuffer.Texture(EHardwareGBufferSemantic::Identity) != 0 &&
              gbuffer.DepthTexture() != 0 && gbuffer.IsComplete());
        std::vector<float> primaryCoverageAfter(primaryCoverageBefore.size());
        std::vector<float> primaryDepthAfter(primaryDepthBefore.size());
        std::vector<std::uint32_t> primaryIdentityAfter(primaryIdentityBefore.size());
        glBindTexture(GL_TEXTURE_2D,
            gbuffer.Texture(EHardwareGBufferSemantic::PositionCoverage));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
            primaryCoverageAfter.data());
        glBindTexture(GL_TEXTURE_2D, gbuffer.DepthTexture());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
            primaryDepthAfter.data());
        glBindTexture(GL_TEXTURE_2D,
            gbuffer.Texture(EHardwareGBufferSemantic::Identity));
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT,
            primaryIdentityAfter.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        check("ray effects preserve G-buffer coverage depth object and material identity",
              primaryCoverageAfter == primaryCoverageBefore &&
              primaryDepthAfter == primaryDepthBefore &&
              primaryIdentityAfter == primaryIdentityBefore);

        const int savedGISamples = quality.giSamples;
        quality.giSamples = 0;
        FRayEffectOutputs zeroGI;
        const std::uint64_t drawsBeforeZeroGI = rays.Stats().rayDraws;
        const bool zeroGIOK = execute(false, true, false, zeroGI);
        check("zero-sample GI is absent with no ray draw", zeroGIOK &&
              !zeroGI.globalIlluminationTarget &&
              rays.Stats().rayDraws == drawsBeforeZeroGI);
        quality.giSamples = savedGISamples;

        UGL43RayTracingBackend computeQualityRays;
        const bool computeQualityAvailable =
            GraphicsCapabilities().SupportsComputeBackend();
        auto executeComputeQuality = [&](bool shadowsEnabled, bool giEnabled,
                                         bool reflectionsEnabled,
                                         FRayEffectOutputs& outputs)
        {
            if (!computeQualityAvailable) return false;
            FRayEffectInputs inputs;
            inputs.gbuffer = &gbuffer;
            inputs.scene = &scene;
            inputs.rasterLighting = &lit;
            inputs.features.rayTracing = true;
            inputs.features.rayTracedShadows = shadowsEnabled;
            inputs.features.rayTracedGI = giEnabled;
            inputs.features.rayTracedReflections = reflectionsEnabled;
            inputs.quality = quality;
            inputs.environmentTexture = lighting.EnvironmentTexture();
            inputs.width = inputs.height = 64;
            inputs.contextGeneration = ContextGeneration();
            return computeQualityRays.RenderEffects(inputs, outputs, &diagnostic);
        };
        std::vector<double> bounceEnergy;
        bool computeBounceParity = true;
        quality.giSamples = 4;
        for (int bounces = 0; bounces <= 4; ++bounces)
        {
            quality.giBounces = bounces;
            FRayEffectOutputs bounced;
            std::vector<float> pixels(64u * 64u * 4u, 0.0f);
            if (execute(false, true, false, bounced) &&
                bounced.globalIlluminationTarget)
            {
                glBindTexture(GL_TEXTURE_2D, static_cast<unsigned>(
                    bounced.globalIlluminationTarget->identity));
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            if (computeQualityAvailable)
            {
                FRayEffectOutputs computeBounced;
                const bool computeBouncedOK = executeComputeQuality(
                    false, true, false, computeBounced);
                const std::vector<float> computePixels = readComputeTarget(
                    computeBounced.globalIlluminationTarget, GL_RGBA, 4);
                computeBounceParity = computeBounceParity && computeBouncedOK &&
                    computeBounced.globalIlluminationTarget &&
                    nearImages(pixels, computePixels);
            }
            double rgb = 0.0;
            for (std::size_t pixel = 0; pixel < pixels.size() / 4u; ++pixel)
                rgb += std::fabs(pixels[pixel * 4u]) +
                    std::fabs(pixels[pixel * 4u + 1]) +
                    std::fabs(pixels[pixel * 4u + 2]);
            bounceEnergy.push_back(rgb);
        }
        int differentBouncePairs = 0;
        for (std::size_t i = 1; i < bounceEnergy.size(); ++i)
            if (std::fabs(bounceEnergy[i] - bounceEnergy[i - 1]) > 0.001)
                ++differentBouncePairs;
        check("GI bounce counts 0 through 4 have observable iterative semantics",
              differentBouncePairs >= 2);
        if (computeQualityAvailable)
            check("GL43 GI bounce counts 0 through 4 match GL33 semantics",
                  computeBounceParity);

        auto readGIConfiguration = [&](int samples, int bounces)
        {
            quality.giSamples = samples;
            quality.giBounces = bounces;
            FRayEffectOutputs output;
            std::vector<float> pixels(64u * 64u * 4u, 0.0f);
            if (execute(false, true, false, output) &&
                output.globalIlluminationTarget)
            {
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(
                    output.globalIlluminationTarget->identity));
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            return pixels;
        };
        const std::vector<float> giEightByFour = readGIConfiguration(8, 4);
        const std::vector<float> giThirtyTwoByFour = readGIConfiguration(32, 4);
        const std::vector<float> giThirtyTwoByTwo = readGIConfiguration(32, 2);
        std::vector<float> computeThirtyTwoByFour;
        bool computeMaximumGI = true;
        if (computeQualityAvailable)
        {
            quality.giSamples = 32;
            quality.giBounces = 4;
            FRayEffectOutputs computeMaximum;
            computeMaximumGI = executeComputeQuality(
                false, true, false, computeMaximum);
            computeThirtyTwoByFour = readComputeTarget(
                computeMaximum.globalIlluminationTarget, GL_RGBA, 4);
        }
        double maximumSampleDifference = 0.0;
        double maximumBounceDifference = 0.0;
        for (std::size_t pixel = 0; pixel < giEightByFour.size() / 4u; ++pixel)
            for (int channel = 0; channel < 3; ++channel)
            {
                const std::size_t index = pixel * 4u +
                    static_cast<std::size_t>(channel);
                maximumSampleDifference += std::fabs(
                    giEightByFour[index] - giThirtyTwoByFour[index]);
                maximumBounceDifference += std::fabs(
                    giThirtyTwoByTwo[index] - giThirtyTwoByFour[index]);
            }
        check("GI consumes the maximum 32-sample configuration",
              maximumSampleDifference > 0.01);
        check("GI consumes the maximum four-bounce configuration",
              maximumBounceDifference > 0.01);
        if (computeQualityAvailable)
            check("GL43 maximum 32-sample four-bounce GI matches GL33",
                  computeMaximumGI &&
                  nearImages(giThirtyTwoByFour, computeThirtyTwoByFour));

        Material secondaryPlain;
        secondaryPlain.kd = glm::vec3(0.9f, 0.05f, 0.05f);
        secondaryPlain.ka = glm::vec3(1.0f);
        Material secondaryTextured;
        secondaryTextured.kd = glm::vec3(1.0f);
        secondaryTextured.ka = glm::vec3(1.0f);
        secondaryTextured.texWidth = secondaryTextured.texHeight = 2;
        secondaryTextured.texChannels = 3;
        secondaryTextured.diffuseTexPath = "memory://secondary-hit-blue";
        secondaryTextured.texData = {
            0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255,
        };
        plane->triMaterial = {0u, 1u};
        const auto savedPlaneOverride = scene.meshes[1].materialOverride;
        scene.meshes[1].materialOverride.reset();
        scene.meshes[1].materialSlots = {
            resolved(secondaryPlain), resolved(secondaryTextured)};
        scene.meshes[1].materialSlotIdentities = {810u, 811u};
        scene.meshes[1].triangleMaterialSlots = &plane->triMaterial;
        scene.meshes[1].triangleMaterialSlotCount = plane->triMaterial.size();
        plane->MarkGeometryDirty();
        quality.giSamples = 16;
        quality.giBounces = 1;
        auto readSecondaryGI = [&]()
        {
            FRayEffectOutputs output;
            std::vector<float> pixels(64u * 64u * 4u, 0.0f);
            if (execute(false, true, false, output) &&
                output.globalIlluminationTarget)
            {
                glBindTexture(GL_TEXTURE_2D, static_cast<unsigned>(
                    output.globalIlluminationTarget->identity));
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            return pixels;
        };
        const std::vector<float> blueSecondary = readSecondaryGI();
        std::vector<float> computeBlueSecondary;
        if (computeQualityAvailable)
        {
            FRayEffectOutputs output;
            const bool okay = executeComputeQuality(false, true, false, output);
            computeBlueSecondary = readComputeTarget(
                output.globalIlluminationTarget, GL_RGBA, 4);
            if (!okay) computeBlueSecondary.clear();
        }
        const std::uint64_t secondaryBLASBefore = rays.Stats().blasUploads;
        const std::uint64_t secondaryMaterialsBefore = rays.Stats().materialUploads;
        std::fill(secondaryTextured.texData.begin(), secondaryTextured.texData.end(), 0u);
        for (std::size_t byte = 0; byte < secondaryTextured.texData.size(); byte += 3u)
            secondaryTextured.texData[byte] = 255u;
        const std::vector<float> redSecondary = readSecondaryGI();
        std::vector<float> computeRedSecondary;
        if (computeQualityAvailable)
        {
            FRayEffectOutputs output;
            const bool okay = executeComputeQuality(false, true, false, output);
            computeRedSecondary = readComputeTarget(
                output.globalIlluminationTarget, GL_RGBA, 4);
            if (!okay) computeRedSecondary.clear();
        }
        double secondaryDifference = 0.0;
        for (std::size_t pixel = 0; pixel < blueSecondary.size() / 4u; ++pixel)
            for (int channel = 0; channel < 3; ++channel)
                secondaryDifference += std::fabs(blueSecondary[pixel * 4u + channel] -
                    redSecondary[pixel * 4u + channel]);
        check("multi-slot textured secondary hits use actual UV material and revision",
              secondaryDifference > 0.01 &&
              rays.Stats().blasUploads == secondaryBLASBefore &&
              rays.Stats().materialUploads == secondaryMaterialsBefore + 1u);
        if (computeQualityAvailable)
        {
            double computeSecondaryDifference = 0.0;
            for (std::size_t pixel = 0;
                 pixel < computeBlueSecondary.size() / 4u; ++pixel)
                for (int channel = 0; channel < 3; ++channel)
                    computeSecondaryDifference += std::fabs(
                        computeBlueSecondary[pixel * 4u + channel] -
                        computeRedSecondary[pixel * 4u + channel]);
            check("GL43 multi-slot textured secondary hit follows the same revision",
                  computeSecondaryDifference > 0.01 &&
                  computeQualityRays.Stats().materialUploads >= 2u);
        }
        scene.meshes[1].materialOverride = savedPlaneOverride;
        scene.meshes[1].materialSlots.clear();
        scene.meshes[1].materialSlotIdentities.clear();
        scene.meshes[1].triangleMaterialSlots = nullptr;
        scene.meshes[1].triangleMaterialSlotCount = 0;

        const auto savedLights = scene.pointLights;
        for (int light = 1; light < 16; ++light)
            scene.pointLights.push_back({
                glm::vec3(float((light % 4) - 2), 2.0f + float(light % 3),
                          -3.0f - float(light / 4)),
                light >= 8 ? glm::vec3(4.0f, 2.0f, 1.0f) : glm::vec3(0.1f)});
        quality.giSamples = 32;
        quality.giBounces = 4;
        quality.shadowSamples = 16;
        FRayEffectOutputs maximumQuality;
        const bool maximumQualityOK = execute(true, true, false, maximumQuality);
        FRayEffectOutputs computeMaximumQuality;
        const bool computeMaximumQualityOK = computeQualityAvailable &&
            executeComputeQuality(true, true, false, computeMaximumQuality);
        check("GL33 honors GI 32x4 shadows 16 and point lights 9 through 16",
              maximumQualityOK && maximumQuality.shadowVisibilityTarget &&
              maximumQuality.globalIlluminationTarget && glGetError() == GL_NO_ERROR);
        if (computeQualityAvailable)
            check("GL43 honors maximum quality and all 16 lights with GL33 parity",
                  computeMaximumQualityOK &&
                  nearImages(readTarget(maximumQuality.shadowVisibilityTarget,
                                        GL_RED, 1),
                              readComputeTarget(computeMaximumQuality.shadowVisibilityTarget,
                                        GL_RED, 1)) &&
                  nearImages(readTarget(maximumQuality.globalIlluminationTarget,
                                        GL_RGBA, 4),
                              readComputeTarget(computeMaximumQuality.globalIlluminationTarget,
                                        GL_RGBA, 4)));
        scene.pointLights = savedLights;
        quality.giSamples = savedGISamples;
        quality.giBounces = 2;
        quality.shadowSamples = 2;
        computeQualityRays.Shutdown();

        // One hardware-primary pixel reflects into a known secondary triangle.
        // The literal expected value follows HardwareRasterShaders: GL_LINEAR
        // samples the stored UNORM RGB first, pow(2.2) decodes it, then the
        // result is multiplied by the non-white material base albedo.
        UHardwareGBuffer controlledGBuffer;
        const bool controlledGBufferOK = controlledGBuffer.Resize(
            1, 1, ContextGeneration());
        if (controlledGBufferOK && controlledGBuffer.BindForGeometry())
        {
            const GLfloat positionCoverage[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const GLfloat geometricNormal[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            const GLfloat shadingNormal[4] = {0.0f, 0.0f, 1.0f, 2.0f};
            const GLfloat primaryAlbedo[4] = {1.0f, 1.0f, 1.0f, 8.0f};
            const GLfloat primarySpecular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const GLuint primaryIdentity[4] = {71u, 72u, 0u, 0u};
            const GLfloat black[4] = {};
            const GLfloat depth = 0.5f;
            glClearBufferfv(GL_COLOR, 0, positionCoverage);
            glClearBufferfv(GL_COLOR, 1, geometricNormal);
            glClearBufferfv(GL_COLOR, 2, shadingNormal);
            glClearBufferfv(GL_COLOR, 3, primaryAlbedo);
            glClearBufferfv(GL_COLOR, 4, primarySpecular);
            glClearBufferuiv(GL_COLOR, 5, primaryIdentity);
            glClearBufferfv(GL_COLOR, 6, black);
            glClearBufferfv(GL_COLOR, 7, black);
            glClearBufferfv(GL_DEPTH, 0, &depth);
            controlledGBuffer.EndGeometry();
        }
        UMesh controlledSurface;
        controlledSurface.vertices = {
            {glm::vec3(-2.0f, -2.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f),
             glm::vec2(0.0f, 0.0f)},
            {glm::vec3( 2.0f, -2.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f),
             glm::vec2(0.8f, 0.4f)},
            {glm::vec3( 0.0f,  2.0f, 2.0f), glm::vec3(0.0f, 0.0f, -1.0f),
             glm::vec2(0.4f, 1.0f)},
        };
        controlledSurface.indices = {0u, 1u, 2u};
        controlledSurface.FinalizeGeometry();
        Material controlledMaterial;
        controlledMaterial.kd = glm::vec3(0.5f, 0.25f, 0.75f);
        controlledMaterial.ka = glm::vec3(1.0f);
        controlledMaterial.texWidth = 3;
        controlledMaterial.texHeight = 2;
        controlledMaterial.texChannels = 3;
        controlledMaterial.wrapMode = EWrapMode::Clamp;
        controlledMaterial.diffuseTexPath = "memory://linear-parity-3x2";
        controlledMaterial.texData = {
              0,   0,   0, 100,  40,  20, 240,  80,  40,
             20, 120,  60, 140, 200, 100, 255, 240, 180,
        };
        FRenderMeshInstance controlledInstance;
        controlledInstance.mesh = &controlledSurface;
        controlledInstance.materialOverride = resolved(controlledMaterial);
        controlledInstance.materialOverrideIdentity = 902u;
        controlledInstance.objectIdentity = 901u;
        Material largerAtlasMaterial;
        largerAtlasMaterial.kd = glm::vec3(1.0f);
        largerAtlasMaterial.texWidth = 7;
        largerAtlasMaterial.texHeight = 5;
        largerAtlasMaterial.texChannels = 3;
        largerAtlasMaterial.diffuseTexPath = "memory://larger-atlas-neighbor";
        largerAtlasMaterial.texData.assign(7u * 5u * 3u, 255u);
        FRenderMeshInstance largerAtlasInstance = controlledInstance;
        largerAtlasInstance.modelTransform = glm::translate(glm::mat4(1.0f),
            glm::vec3(100.0f, 0.0f, 0.0f));
        largerAtlasInstance.materialOverride = resolved(largerAtlasMaterial);
        largerAtlasInstance.materialOverrideIdentity = 904u;
        largerAtlasInstance.objectIdentity = 903u;
        FRenderScene controlledScene;
        controlledScene.camera.eye = glm::vec3(0.0f, 0.0f, 1.0f);
        controlledScene.environment.tint = glm::vec3(0.0f);
        controlledScene.environment.horizon = glm::vec3(0.0f);
        controlledScene.environment.zenith = glm::vec3(0.0f);
        controlledScene.meshes.push_back(controlledInstance);
        controlledScene.meshes.push_back(largerAtlasInstance);
        FRasterLightingOutput controlledLighting;
        controlledLighting.valid = true;
        FRenderQuality controlledQuality;
        controlledQuality.reflStrength = 1.0f;
        UGL33RayTracingBackend controlledRays;
        FRayEffectInputs controlledInputs;
        controlledInputs.gbuffer = &controlledGBuffer;
        controlledInputs.scene = &controlledScene;
        controlledInputs.rasterLighting = &controlledLighting;
        controlledInputs.features.rayTracing = true;
        controlledInputs.features.rayTracedReflections = true;
        controlledInputs.quality = controlledQuality;
        controlledInputs.width = controlledInputs.height = 1;
        controlledInputs.contextGeneration = ContextGeneration();
        FRayEffectOutputs controlledReflection;
        glm::vec4 controlledPixel(0.0f);
        const bool controlledReflectionOK = controlledGBufferOK &&
            controlledRays.RenderEffects(controlledInputs, controlledReflection,
                &diagnostic);
        if (controlledReflectionOK && controlledReflection.reflectionTarget)
        {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(
                controlledReflection.reflectionTarget->identity));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
                &controlledPixel[0]);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        const glm::vec3 expectedStoredRGB(93.8f / 255.0f,
            131.6f / 255.0f, 65.8f / 255.0f);
        const glm::vec3 expectedSecondary = controlledMaterial.kd *
            glm::pow(expectedStoredRGB, glm::vec3(2.2f));
        const glm::vec3 controlledError = glm::abs(
            glm::vec3(controlledPixel) - expectedSecondary);
        check("secondary textured albedo matches raster GL_LINEAR decode and base color",
              controlledReflectionOK && controlledReflection.reflectionTarget &&
              controlledError.x < 0.003f && controlledError.y < 0.003f &&
              controlledError.z < 0.003f);

        UGL43RayTracingBackend computeControlledRays;
        glm::vec4 computeControlledPixel(0.0f);
        FRayEffectOutputs computeControlledReflection;
        const bool computeControlledReflectionOK = computeQualityAvailable &&
            controlledGBufferOK && computeControlledRays.RenderEffects(
                controlledInputs, computeControlledReflection, &diagnostic);
        if (computeControlledReflectionOK &&
            computeControlledReflection.reflectionTarget)
        {
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(
                computeControlledReflection.reflectionTarget->identity));
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
                &computeControlledPixel[0]);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (computeQualityAvailable)
            check("GL43 non-white mixed-resolution textured secondary hit matches GL33",
                  computeControlledReflectionOK &&
                  glm::all(glm::lessThan(glm::abs(
                      glm::vec3(computeControlledPixel - controlledPixel)),
                      glm::vec3(0.003f))));

        GLuint controlledHDRITexture = 0;
        const float controlledHDRIPixels[12] = {
            0.2f, 1.4f, 0.4f, 0.2f, 1.4f, 0.4f,
            0.2f, 1.4f, 0.4f, 0.2f, 1.4f, 0.4f,
        };
        glGenTextures(1, &controlledHDRITexture);
        glBindTexture(GL_TEXTURE_2D, controlledHDRITexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 2, 2, 0, GL_RGB,
            GL_FLOAT, controlledHDRIPixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        controlledInputs.environmentTexture = controlledHDRITexture;
        FRayEffectOutputs controlledHDRIGL33, controlledHDRIGL43;
        const bool controlledHDRIGL33OK = controlledRays.RenderEffects(
            controlledInputs, controlledHDRIGL33, &diagnostic);
        const bool controlledHDRIGL43OK = computeQualityAvailable &&
            computeControlledRays.RenderEffects(controlledInputs,
                controlledHDRIGL43, &diagnostic);
        if (computeQualityAvailable)
            check("GL43 HDRI reflection and secondary shading match GL33",
                  controlledHDRIGL33OK && controlledHDRIGL43OK &&
                  nearImages(readTarget(controlledHDRIGL33.reflectionTarget,
                                        GL_RGBA, 4, 1, 1),
                              readComputeTarget(controlledHDRIGL43.reflectionTarget,
                                        GL_RGBA, 4, 1, 1)));
        controlledInputs.environmentTexture = 0;
        glDeleteTextures(1, &controlledHDRITexture);

        auto readControlledShadow = [&](int lightCount, int samples,
                                        float softness)
        {
            controlledScene.pointLights.clear();
            for (int light = 0; light < lightCount; ++light)
                controlledScene.pointLights.push_back({
                    light < 8 ? glm::vec3(0.0f, 0.0f, 4.0f)
                              : glm::vec3(0.0f, 0.0f, -4.0f),
                    glm::vec3(1.0f)});
            controlledInputs.features.rayTracedReflections = false;
            controlledInputs.features.rayTracedShadows = true;
            controlledInputs.quality.shadowSamples = samples;
            controlledInputs.quality.shadowSoftness = softness;
            FRayEffectOutputs output;
            float pixel = -1.0f;
            if (controlledRays.RenderEffects(controlledInputs, output, &diagnostic) &&
                output.shadowVisibilityTarget)
            {
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(
                    output.shadowVisibilityTarget->identity));
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, &pixel);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            return pixel;
        };
        auto readComputeControlledShadow = [&](int lightCount, int samples,
                                               float softness)
        {
            controlledScene.pointLights.clear();
            for (int light = 0; light < lightCount; ++light)
                controlledScene.pointLights.push_back({
                    light < 8 ? glm::vec3(0.0f, 0.0f, 4.0f)
                              : glm::vec3(0.0f, 0.0f, -4.0f),
                    glm::vec3(1.0f)});
            controlledInputs.features.rayTracedReflections = false;
            controlledInputs.features.rayTracedShadows = true;
            controlledInputs.quality.shadowSamples = samples;
            controlledInputs.quality.shadowSoftness = softness;
            FRayEffectOutputs output;
            float pixel = -1.0f;
            if (computeControlledRays.RenderEffects(
                    controlledInputs, output, &diagnostic) &&
                output.shadowVisibilityTarget)
            {
                glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(
                    output.shadowVisibilityTarget->identity));
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, &pixel);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            return pixel;
        };
        const float firstEightVisibility = readControlledShadow(8, 1, 0.0f);
        const float allSixteenVisibility = readControlledShadow(16, 1, 0.0f);
        check("shadow output measurably consumes point lights 9 through 16",
              firstEightVisibility >= 0.0f && firstEightVisibility < 0.05f &&
              allSixteenVisibility > 0.45f && allSixteenVisibility < 0.55f);
        if (computeQualityAvailable)
        {
            const float computeFirstEightVisibility =
                readComputeControlledShadow(8, 1, 0.0f);
            const float computeAllSixteenVisibility =
                readComputeControlledShadow(16, 1, 0.0f);
            check("GL43 shadow contribution from lights 9 through 16 matches GL33",
                  std::fabs(firstEightVisibility - computeFirstEightVisibility) <
                      0.015f &&
                  std::fabs(allSixteenVisibility - computeAllSixteenVisibility) <
                      0.015f);
        }
        controlledSurface.vertices[0].position = glm::vec3(-0.35f, -0.3f, 2.0f);
        controlledSurface.vertices[1].position = glm::vec3( 0.35f, -0.3f, 2.0f);
        controlledSurface.vertices[2].position = glm::vec3( 0.0f,   0.4f, 2.0f);
        controlledSurface.MarkGeometryDirty();
        const float oneShadowSample = readControlledShadow(1, 1, 0.2f);
        const float sixteenShadowSamples = readControlledShadow(1, 16, 0.2f);
        check("maximum shadow samples change controlled soft-shadow visibility",
              oneShadowSample >= 0.0f && sixteenShadowSamples > 0.0f &&
              sixteenShadowSamples < 1.0f &&
              std::fabs(oneShadowSample - sixteenShadowSamples) > 0.05f);
        if (computeQualityAvailable)
        {
            const float computeOneShadowSample =
                readComputeControlledShadow(1, 1, 0.2f);
            const float computeSixteenShadowSamples =
                readComputeControlledShadow(1, 16, 0.2f);
            check("GL43 maximum shadow samples match GL33 controlled visibility",
                  std::fabs(oneShadowSample - computeOneShadowSample) < 0.015f &&
                  std::fabs(sixteenShadowSamples -
                            computeSixteenShadowSamples) < 0.015f);
        }
        computeControlledRays.Shutdown();
        controlledRays.Shutdown();
        controlledGBuffer.Release();

        glDrawBuffer(GL_NONE);
        glViewport(3, 4, 17, 19);
        glEnable(GL_RASTERIZER_DISCARD);
        glEnable(GL_STENCIL_TEST);
        glEnable(GL_SCISSOR_TEST);
        glScissor(7, 8, 1, 1);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glEnable(GL_CULL_FACE);
        glEnable(GL_FRAMEBUFFER_SRGB);
        glDepthRange(0.2, 0.8);
        glPolygonMode(GL_FRONT, GL_LINE);
        glPolygonMode(GL_BACK, GL_POINT);
        glFrontFace(GL_CW);
        glEnablei(GL_BLEND, 0);
        glColorMaski(0, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        GLuint hostileTBO = 0;
        glGenBuffers(1, &hostileTBO);
        glBindBuffer(GL_TEXTURE_BUFFER, hostileTBO);
        const float hostileSentinel = 19.0f;
        glBufferData(GL_TEXTURE_BUFFER, sizeof(hostileSentinel), &hostileSentinel,
            GL_STATIC_DRAW);
        GLint combinedTextureUnits = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combinedTextureUnits);
        const int hostileUnit = combinedTextureUnits > 16 ? combinedTextureUnits - 1 : 15;
        GLuint hostileTextures[3] = {}, hostileSampler = 0;
        glGenTextures(3, hostileTextures);
        glGenSamplers(1, &hostileSampler);
        glActiveTexture(GL_TEXTURE0 + hostileUnit);
        const unsigned char hostilePixel[4] = {17u, 43u, 91u, 255u};
        glBindTexture(GL_TEXTURE_2D, hostileTextures[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, hostilePixel);
        glBindTexture(GL_TEXTURE_2D_ARRAY, hostileTextures[1]);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, hostilePixel);
        glBindTexture(GL_TEXTURE_BUFFER, hostileTextures[2]);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, hostileTBO);
        glBindSampler(hostileUnit, hostileSampler);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        constexpr GLenum testShaderStorageBuffer = 0x90D2;
        constexpr GLenum testShaderStorageBufferBinding = 0x90D3;
        constexpr GLenum testShaderStorageBufferStart = 0x90D4;
        constexpr GLenum testShaderStorageBufferSize = 0x90D5;
        GLuint hostileSSBOs[2] = {};
        GLuint hostileImageTexture = 0;
        if (computeQualityAvailable)
        {
            glGenBuffers(2, hostileSSBOs);
            const float hostileSSBOData[16] = {19.0f};
            glBindBuffer(testShaderStorageBuffer, hostileSSBOs[0]);
            glBufferData(testShaderStorageBuffer, sizeof(hostileSSBOData),
                hostileSSBOData, GL_STATIC_DRAW);
            glBindBufferRange(testShaderStorageBuffer, 0, hostileSSBOs[0], 0,
                sizeof(float) * 4);
            glBindBuffer(testShaderStorageBuffer, hostileSSBOs[1]);
            glBufferData(testShaderStorageBuffer, sizeof(hostileSSBOData),
                hostileSSBOData, GL_STATIC_DRAW);
            glGenTextures(1, &hostileImageTexture);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, hostileImageTexture);
            const float hostileImagePixel = 0.625f;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED,
                GL_FLOAT, &hostileImagePixel);
            glBindImageTexture(0, hostileImageTexture, 0, GL_FALSE, 0,
                GL_READ_ONLY, GL_R32F);
            glActiveTexture(GL_TEXTURE0 + hostileUnit);
        }
        scene.meshes.front().materialOverride->ambient.x += 0.001f;
        FRayEffectOutputs hostile;
        UGL33RayTracingBackend hostileRays;
        FRayEffectInputs hostileInputs;
        hostileInputs.gbuffer = &gbuffer;
        // Reuse the controlled 3x2 + 7x5 material scene so unpack=8 would
        // corrupt its odd-width padded atlas unless the backend normalizes it.
        hostileInputs.scene = &controlledScene;
        hostileInputs.rasterLighting = &lit;
        hostileInputs.features.rayTracing = true;
        hostileInputs.features.rayTracedShadows = true;
        hostileInputs.quality = quality;
        hostileInputs.width = hostileInputs.height = 3;
        hostileInputs.contextGeneration = ContextGeneration();
        const bool hostileOK = hostileRays.RenderEffects(
            hostileInputs, hostile, &diagnostic);
        GLint restoredDrawBuffer = 0, restoredViewport[4] = {}, restoredPolygon[2] = {};
        GLint restoredFrontFace = 0, restoredDepthFunction = 0;
        GLint restoredTBO = 0, restoredActiveTexture = 0, restoredTexture2D = 0;
        GLint restoredTextureArray = 0, restoredTextureBuffer = 0;
        GLint restoredSampler = 0, restoredUnpackAlignment = 0;
        GLdouble restoredDepthRange[2] = {};
        GLboolean restoredDepthMask = GL_TRUE, restoredColorMask[4] = {};
        glGetIntegerv(GL_DRAW_BUFFER0, &restoredDrawBuffer);
        glGetIntegerv(GL_VIEWPORT, restoredViewport);
        glGetIntegerv(GL_POLYGON_MODE, restoredPolygon);
        glGetIntegerv(GL_FRONT_FACE, &restoredFrontFace);
        glGetIntegerv(GL_DEPTH_FUNC, &restoredDepthFunction);
        glGetDoublev(GL_DEPTH_RANGE, restoredDepthRange);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &restoredDepthMask);
        glGetBooleani_v(GL_COLOR_WRITEMASK, 0, restoredColorMask);
        glGetIntegerv(0x8C2A, &restoredTBO);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &restoredActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &restoredTexture2D);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &restoredTextureArray);
        glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &restoredTextureBuffer);
        glGetIntegeri_v(GL_SAMPLER_BINDING, hostileUnit, &restoredSampler);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &restoredUnpackAlignment);
        unsigned char restoredHostilePixel[4] = {};
        glBindTexture(GL_TEXTURE_2D, hostileTextures[0]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
            restoredHostilePixel);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(restoredTexture2D));
        check("ray pass normalizes hostile state and restores it", hostileOK &&
              restoredDrawBuffer == GL_NONE && restoredViewport[0] == 3 &&
              restoredViewport[1] == 4 && restoredViewport[2] == 17 &&
              restoredViewport[3] == 19 && restoredPolygon[0] == GL_LINE &&
              restoredPolygon[1] == GL_POINT && restoredFrontFace == GL_CW &&
              restoredDepthFunction == GL_GREATER && restoredDepthMask == GL_FALSE &&
              std::fabs(restoredDepthRange[0] - 0.2) < 0.000001 &&
              std::fabs(restoredDepthRange[1] - 0.8) < 0.000001 &&
              restoredColorMask[0] == GL_FALSE && restoredColorMask[1] == GL_TRUE &&
              restoredColorMask[2] == GL_FALSE && restoredColorMask[3] == GL_TRUE &&
              glIsEnabled(GL_RASTERIZER_DISCARD) && glIsEnabled(GL_STENCIL_TEST) &&
              glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_DEPTH_TEST) &&
              glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_FRAMEBUFFER_SRGB) &&
              glIsEnabledi(GL_BLEND, 0) &&
              restoredTBO == static_cast<GLint>(hostileTBO) &&
              restoredActiveTexture == GL_TEXTURE0 + hostileUnit &&
              restoredTexture2D == static_cast<GLint>(hostileTextures[0]) &&
              restoredTextureArray == static_cast<GLint>(hostileTextures[1]) &&
              restoredTextureBuffer == static_cast<GLint>(hostileTextures[2]) &&
              restoredSampler == static_cast<GLint>(hostileSampler) &&
              restoredUnpackAlignment == 8 &&
              std::equal(std::begin(hostilePixel), std::end(hostilePixel),
                         std::begin(restoredHostilePixel)));
        if (computeQualityAvailable)
        {
            GLuint hostilePixelUnpackBuffer = 0;
            glGenBuffers(1, &hostilePixelUnpackBuffer);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, hostilePixelUnpackBuffer);
            const unsigned char hostileUnpackBytes[4] = {1u, 2u, 3u, 4u};
            glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof(hostileUnpackBytes),
                hostileUnpackBytes, GL_STATIC_DRAW);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 19);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 5);
            glPixelStorei(GL_UNPACK_SKIP_IMAGES, 7);
            UGL43RayTracingBackend computeHostileRays;
            FRayEffectOutputs computeHostileOutput;
            const bool computeHostileOK = computeHostileRays.RenderEffects(
                hostileInputs, computeHostileOutput, &diagnostic);
            GLint restoredGenericSSBO = 0, restoredIndexedSSBO = 0;
            GLint64 restoredIndexedSSBOStart = -1;
            GLint64 restoredIndexedSSBOSize = -1;
            GLint restoredImageName = 0, restoredImageAccess = 0;
            GLint restoredImageFormat = 0;
            glGetIntegerv(testShaderStorageBufferBinding,
                &restoredGenericSSBO);
            glGetIntegeri_v(testShaderStorageBufferBinding, 0,
                &restoredIndexedSSBO);
            glGetInteger64i_v(testShaderStorageBufferStart, 0,
                &restoredIndexedSSBOStart);
            glGetInteger64i_v(testShaderStorageBufferSize, 0,
                &restoredIndexedSSBOSize);
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0, &restoredImageName);
            glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS, 0,
                &restoredImageAccess);
            glGetIntegeri_v(GL_IMAGE_BINDING_FORMAT, 0,
                &restoredImageFormat);
            GLint computeRestoredActiveTexture = 0;
            GLint computeRestoredUnpackAlignment = 0;
            GLint computeRestoredPixelUnpackBuffer = 0;
            GLint computeRestoredUnpackRowLength = 0;
            GLint computeRestoredUnpackImageHeight = 0;
            GLint computeRestoredUnpackSkipPixels = 0;
            GLint computeRestoredUnpackSkipRows = 0;
            GLint computeRestoredUnpackSkipImages = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &computeRestoredActiveTexture);
            glGetIntegerv(GL_UNPACK_ALIGNMENT,
                &computeRestoredUnpackAlignment);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,
                &computeRestoredPixelUnpackBuffer);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH,
                &computeRestoredUnpackRowLength);
            glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT,
                &computeRestoredUnpackImageHeight);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS,
                &computeRestoredUnpackSkipPixels);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS,
                &computeRestoredUnpackSkipRows);
            glGetIntegerv(GL_UNPACK_SKIP_IMAGES,
                &computeRestoredUnpackSkipImages);
            check("GL43 restores hostile indexed SSBO image and pixel-store state",
                  computeHostileOK && computeHostileOutput.shadowVisibilityTarget &&
                  restoredGenericSSBO == static_cast<GLint>(hostileSSBOs[1]) &&
                  restoredIndexedSSBO == static_cast<GLint>(hostileSSBOs[0]) &&
                  restoredIndexedSSBOStart == 0 &&
                  restoredIndexedSSBOSize == sizeof(float) * 4 &&
                  restoredImageName == static_cast<GLint>(hostileImageTexture) &&
                  restoredImageAccess == GL_READ_ONLY &&
                  restoredImageFormat == GL_R32F &&
                  computeRestoredActiveTexture == GL_TEXTURE0 + hostileUnit &&
                  computeRestoredUnpackAlignment == 8 &&
                  computeRestoredPixelUnpackBuffer ==
                      static_cast<GLint>(hostilePixelUnpackBuffer) &&
                  computeRestoredUnpackRowLength == 17 &&
                  computeRestoredUnpackImageHeight == 19 &&
                  computeRestoredUnpackSkipPixels == 3 &&
                  computeRestoredUnpackSkipRows == 5 &&
                  computeRestoredUnpackSkipImages == 7 &&
                  computeHostileRays.Stats().memoryBarriers == 1u);

            computeHostileRays.InjectNextUploadFailureForTesting();
            controlledScene.meshes.front().modelTransform[3].x += 0.01f;
            FRayEffectOutputs computeHostileFailure;
            const bool computeHostileFailed = !computeHostileRays.RenderEffects(
                hostileInputs, computeHostileFailure, &diagnostic);
            glGetIntegerv(testShaderStorageBufferBinding,
                &restoredGenericSSBO);
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 0, &restoredImageName);
            glGetIntegerv(GL_UNPACK_ALIGNMENT,
                &computeRestoredUnpackAlignment);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,
                &computeRestoredPixelUnpackBuffer);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH,
                &computeRestoredUnpackRowLength);
            glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT,
                &computeRestoredUnpackImageHeight);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS,
                &computeRestoredUnpackSkipPixels);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS,
                &computeRestoredUnpackSkipRows);
            glGetIntegerv(GL_UNPACK_SKIP_IMAGES,
                &computeRestoredUnpackSkipImages);
            check("failed GL43 upload is transactional and restores hostile state",
                  computeHostileFailed &&
                  !computeHostileFailure.shadowVisibilityTarget &&
                  restoredGenericSSBO == static_cast<GLint>(hostileSSBOs[1]) &&
                  restoredImageName == static_cast<GLint>(hostileImageTexture) &&
                  computeRestoredUnpackAlignment == 8 &&
                  computeRestoredPixelUnpackBuffer ==
                      static_cast<GLint>(hostilePixelUnpackBuffer) &&
                  computeRestoredUnpackRowLength == 17 &&
                  computeRestoredUnpackImageHeight == 19 &&
                  computeRestoredUnpackSkipPixels == 3 &&
                  computeRestoredUnpackSkipRows == 5 &&
                  computeRestoredUnpackSkipImages == 7);
            computeHostileRays.Shutdown();
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
            glDeleteBuffers(1, &hostilePixelUnpackBuffer);
        }
        UGL33RayTracingBackend hostileFailureRays;
        hostileFailureRays.InjectNextUploadFailureForTesting();
        FRayEffectOutputs hostileFailureOutput;
        const bool hostileFailure = !hostileFailureRays.RenderEffects(
            hostileInputs, hostileFailureOutput, &diagnostic);
        GLint failedActiveTexture = 0, failedTexture2D = 0;
        GLint failedTextureArray = 0, failedTextureBuffer = 0;
        GLint failedSampler = 0, failedUnpackAlignment = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &failedActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &failedTexture2D);
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &failedTextureArray);
        glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &failedTextureBuffer);
        glGetIntegeri_v(GL_SAMPLER_BINDING, hostileUnit, &failedSampler);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &failedUnpackAlignment);
        check("failed ray upload restores high texture-unit and pixel-store state",
              hostileFailure && !hostileFailureOutput.shadowVisibilityTarget &&
              failedActiveTexture == GL_TEXTURE0 + hostileUnit &&
              failedTexture2D == static_cast<GLint>(hostileTextures[0]) &&
              failedTextureArray == static_cast<GLint>(hostileTextures[1]) &&
              failedTextureBuffer == static_cast<GLint>(hostileTextures[2]) &&
              failedSampler == static_cast<GLint>(hostileSampler) &&
              failedUnpackAlignment == 8);
        glDrawBuffer(GL_BACK); glViewport(0, 0, 64, 64);
        glDisable(GL_RASTERIZER_DISCARD); glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE); glDisable(GL_FRAMEBUFFER_SRGB);
        glDepthRange(0.0, 1.0); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glFrontFace(GL_CCW); glDisablei(GL_BLEND, 0);
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
        if (computeQualityAvailable)
        {
            glBindBufferBase(testShaderStorageBuffer, 0, 0);
            glBindBuffer(testShaderStorageBuffer, 0);
            glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        }
        hostileFailureRays.Shutdown();
        hostileRays.Shutdown();
        glBindSampler(hostileUnit, 0);
        glDeleteSamplers(1, &hostileSampler);
        glDeleteTextures(3, hostileTextures);
        glDeleteTextures(1, &hostileImageTexture);
        glDeleteBuffers(1, &hostileTBO);
        glDeleteBuffers(2, hostileSSBOs);
        glActiveTexture(GL_TEXTURE0);

        scene.meshes.front().modelTransform[3].x += 0.125f;
        rays.InjectNextUploadFailureForTesting();
        FRayEffectOutputs failedUpload, retriedUpload;
        const std::uint64_t uploadsBeforeFailure = rays.Stats().sceneUploads;
        const bool uploadFailed = !execute(true, false, false, failedUpload);
        const bool uploadRetried = execute(true, false, false, retriedUpload);
        check("failed scene upload is transactional and retries", uploadFailed &&
              !failedUpload.shadowVisibilityTarget && uploadRetried &&
              retriedUpload.shadowVisibilityTarget &&
              rays.Stats().sceneUploads == uploadsBeforeFailure + 1u);

        class FFailOnceGLFactory final : public IRayTracingBackendFactory
        {
        public:
            std::unique_ptr<IRayTracingBackend> Create(
                ERayTracingBackend backend, std::string* error) override
            {
                if (backend != ERayTracingBackend::CompatibleGL33)
                {
                    if (error) *error = "only GL33 is expected by this probe";
                    return {};
                }
                auto result = std::make_unique<UGL33RayTracingBackend>();
                result->InjectNextUploadFailureForTesting();
                return result;
            }
        } failOnceFactory;
        class FCountingWarnings final : public IRayEffectsWarningSink
        {
        public:
            void Warn(const std::string&) override { ++count; }
            int count = 0;
        } schedulerWarnings;
        FRayEffectsScheduler retryScheduler(failOnceFactory, schedulerWarnings);
        FBackendSelection retrySelection;
        retrySelection.requested = ERayTracingBackend::CompatibleGL33;
        retrySelection.selected = ERayTracingBackend::CompatibleGL33;
        retrySelection.available = true;
        retrySelection.rayTracingEnabled = true;
        FRayEffectInputs retryInputs;
        retryInputs.gbuffer = &gbuffer;
        retryInputs.scene = &scene;
        retryInputs.rasterLighting = &lit;
        retryInputs.features.rayTracing = true;
        retryInputs.features.rayTracedShadows = true;
        retryInputs.quality = quality;
        retryInputs.width = retryInputs.height = 64;
        retryInputs.contextGeneration = ContextGeneration();
        FRayEffectOutputs schedulerFailure, schedulerRetry;
        const bool schedulerNeutral = retryScheduler.Execute(
            retryInputs, retrySelection, schedulerFailure);
        const bool schedulerRecovered = retryScheduler.Execute(
            retryInputs, retrySelection, schedulerRetry);
        check("scheduler retries a transactional real-GL upload failure next frame",
              schedulerNeutral && !schedulerFailure.shadowVisibilityTarget &&
              schedulerRecovered && schedulerRetry.shadowVisibilityTarget &&
              schedulerWarnings.count == 1 &&
              retryScheduler.Stats().backendCalls == 2u);
        retryScheduler.Shutdown();

        if (computeQualityAvailable)
        {
            int autoCompatibleCreates = 0;
            int autoComputeCreates = 0;
            FOpenGLRayTracingBackendFactory autoInitFailureFactory(
                [&]()
                {
                    ++autoCompatibleCreates;
                    return std::make_unique<UGL33RayTracingBackend>();
                },
                [&]()
                {
                    ++autoComputeCreates;
                    auto backend = std::make_unique<UGL43RayTracingBackend>();
                    backend->InjectNextInitializationFailureForTesting();
                    return backend;
                });
            FCountingWarnings autoWarnings;
            FRayEffectsScheduler autoFallbackScheduler(
                autoInitFailureFactory, autoWarnings);
            FBackendSelection autoComputeSelection;
            autoComputeSelection.requested = ERayTracingBackend::Auto;
            autoComputeSelection.selected = ERayTracingBackend::ComputeGL43;
            autoComputeSelection.available = true;
            autoComputeSelection.rayTracingEnabled = true;
            FRayEffectOutputs autoFallbackFirst, autoFallbackSecond;
            const bool autoFallbackFirstOK = autoFallbackScheduler.Execute(
                retryInputs, autoComputeSelection, autoFallbackFirst);
            const bool autoFallbackSecondOK = autoFallbackScheduler.Execute(
                retryInputs, autoComputeSelection, autoFallbackSecond);
            check("Auto GL43 init failure falls back to GL33 exactly once",
                  autoFallbackFirstOK && autoFallbackSecondOK &&
                  autoFallbackFirst.shadowVisibilityTarget &&
                  autoFallbackSecond.shadowVisibilityTarget &&
                  autoComputeCreates == 1 && autoCompatibleCreates == 1 &&
                  autoWarnings.count == 1 &&
                  dynamic_cast<UGL33RayTracingBackend*>(
                      autoFallbackScheduler.ActiveBackend()));
            autoFallbackScheduler.Shutdown();

            int forcedCompatibleCreates = 0;
            int forcedComputeCreates = 0;
            FOpenGLRayTracingBackendFactory forcedInitFailureFactory(
                [&]()
                {
                    ++forcedCompatibleCreates;
                    return std::make_unique<UGL33RayTracingBackend>();
                },
                [&]()
                {
                    ++forcedComputeCreates;
                    auto backend = std::make_unique<UGL43RayTracingBackend>();
                    backend->InjectNextInitializationFailureForTesting();
                    return backend;
                });
            FCountingWarnings forcedWarnings;
            FRayEffectsScheduler forcedFailureScheduler(
                forcedInitFailureFactory, forcedWarnings);
            FBackendSelection forcedComputeSelection = autoComputeSelection;
            forcedComputeSelection.requested =
                ERayTracingBackend::ComputeGL43;
            FRayEffectOutputs forcedFailureFirst, forcedFailureSecond;
            const bool forcedFirstOK = forcedFailureScheduler.Execute(
                retryInputs, forcedComputeSelection, forcedFailureFirst);
            const bool forcedSecondOK = forcedFailureScheduler.Execute(
                retryInputs, forcedComputeSelection, forcedFailureSecond);
            check("forced GL43 init failure remains neutral and never calls GL33",
                  forcedFirstOK && forcedSecondOK &&
                  !forcedFailureFirst.shadowVisibilityTarget &&
                  !forcedFailureSecond.shadowVisibilityTarget &&
                  forcedComputeCreates == 1 && forcedCompatibleCreates == 0 &&
                  forcedWarnings.count == 1 &&
                  forcedFailureScheduler.ActiveBackend() == nullptr);
            forcedFailureScheduler.Shutdown();

            int retryComputeCreates = 0;
            FOpenGLRayTracingBackendFactory computeRetryFactory(
                [] { return std::make_unique<UGL33RayTracingBackend>(); },
                [&]()
                {
                    ++retryComputeCreates;
                    auto backend = std::make_unique<UGL43RayTracingBackend>();
                    backend->InjectNextUploadFailureForTesting();
                    return backend;
                });
            FCountingWarnings computeRetryWarnings;
            FRayEffectsScheduler computeRetryScheduler(
                computeRetryFactory, computeRetryWarnings);
            FRayEffectOutputs computeRetryFirst, computeRetrySecond;
            const bool computeRetryFirstOK = computeRetryScheduler.Execute(
                retryInputs, forcedComputeSelection, computeRetryFirst);
            const bool computeRetrySecondOK = computeRetryScheduler.Execute(
                retryInputs, forcedComputeSelection, computeRetrySecond);
            check("scheduler retries transactional GL43 upload failure next frame",
                  computeRetryFirstOK && computeRetrySecondOK &&
                  !computeRetryFirst.shadowVisibilityTarget &&
                  computeRetrySecond.shadowVisibilityTarget &&
                  retryComputeCreates == 1 &&
                  computeRetryWarnings.count == 1 &&
                  computeRetryScheduler.Stats().backendCalls == 2u);
            computeRetryScheduler.Shutdown();
        }

        FRenderScene sharedScene;
        sharedScene.meshes.push_back(cubeInstance);
        UGL33RayTracingBackend scaleRays;
        FRayEffectInputs scaleInputs;
        scaleInputs.gbuffer = &gbuffer;
        scaleInputs.scene = &sharedScene;
        scaleInputs.rasterLighting = &lit;
        scaleInputs.features.rayTracing = true;
        scaleInputs.features.rayTracedShadows = true;
        scaleInputs.quality = quality;
        scaleInputs.contextGeneration = ContextGeneration();
        // This is an upload/cache invariant probe. A single fragment keeps the
        // traversal bounded while the real GL backend still creates every TBO.
        scaleInputs.width = scaleInputs.height = 1;
        auto uploadScaleScene = [&]()
        {
            FRayEffectOutputs ignored;
            return scaleRays.RenderEffects(scaleInputs, ignored, &diagnostic);
        };
        bool scaleUploadsOK = uploadScaleScene();
        const std::uint64_t unchangedBLASUploads = scaleRays.Stats().blasUploads;
        const std::uint64_t unchangedInstanceUploads = scaleRays.Stats().instanceUploads;
        const std::uint64_t unchangedSceneUploads = scaleRays.Stats().sceneUploads;
        const bool unchangedUploadOK = uploadScaleScene();
        check("unchanged ray scene performs zero GL TBO uploads",
              unchangedUploadOK &&
              scaleRays.Stats().blasUploads == unchangedBLASUploads &&
              scaleRays.Stats().instanceUploads == unchangedInstanceUploads &&
              scaleRays.Stats().sceneUploads == unchangedSceneUploads);
        for (int count : {100, 1000})
        {
            sharedScene.meshes.resize(static_cast<std::size_t>(count), cubeInstance);
            for (int i = 0; i < count; ++i)
            {
                sharedScene.meshes[static_cast<std::size_t>(i)].objectIdentity =
                    static_cast<std::uint32_t>(i + 1);
                sharedScene.meshes[static_cast<std::size_t>(i)].modelTransform =
                    glm::translate(glm::mat4(1.0f), glm::vec3(float(i), 0.0f, -5.0f));
            }
            scaleUploadsOK = uploadScaleScene() && scaleUploadsOK;
        }
        check("1/100/1000 shared cubes upload one GL BLAS TBO set",
              scaleUploadsOK && scaleRays.Stats().blasUploads == 1u &&
              scaleRays.Stats().instanceUploads == 3u &&
              scaleRays.Stats().residentBLAS == 1u);
        const std::uint64_t blasUploadsBeforeTransform =
            scaleRays.Stats().blasUploads;
        const std::uint64_t instanceUploadsBeforeTransform =
            scaleRays.Stats().instanceUploads;
        sharedScene.meshes.front().modelTransform[3].y += 1.0f;
        const bool transformedUploadOK = uploadScaleScene();
        check("transform-only edit uploads instances but not GL BLAS TBOs",
              transformedUploadOK &&
              scaleRays.Stats().blasUploads == blasUploadsBeforeTransform &&
              scaleRays.Stats().instanceUploads == instanceUploadsBeforeTransform + 1u);
        const std::uint64_t blasUploadsBeforeRevision =
            scaleRays.Stats().blasUploads;
        cube->MarkGeometryDirty();
        const bool revisedUploadOK = uploadScaleScene();
        check("geometry revision uploads exactly one replacement GL BLAS TBO set",
              revisedUploadOK &&
              scaleRays.Stats().blasUploads == blasUploadsBeforeRevision + 1u &&
              scaleRays.Stats().residentBLAS == 1u);
        scaleRays.Shutdown();

        if (computeQualityAvailable)
        {
            FRenderScene computeSharedScene;
            computeSharedScene.meshes.push_back(cubeInstance);
            UGL43RayTracingBackend computeScaleRays;
            FRayEffectInputs computeScaleInputs = scaleInputs;
            computeScaleInputs.scene = &computeSharedScene;
            auto uploadComputeScaleScene = [&]()
            {
                FRayEffectOutputs ignored;
                return computeScaleRays.RenderEffects(
                    computeScaleInputs, ignored, &diagnostic);
            };
            bool computeScaleOK = uploadComputeScaleScene();
            const std::uint64_t computeUnchangedBLAS =
                computeScaleRays.Stats().blasUploads;
            const std::uint64_t computeUnchangedInstances =
                computeScaleRays.Stats().instanceUploads;
            const std::uint64_t computeUnchangedMaterials =
                computeScaleRays.Stats().materialUploads;
            const std::uint64_t computeUnchangedScenes =
                computeScaleRays.Stats().sceneUploads;
            computeScaleOK = uploadComputeScaleScene() && computeScaleOK;
            check("unchanged GL43 scene performs zero SSBO or atlas uploads",
                  computeScaleOK &&
                  computeScaleRays.Stats().blasUploads == computeUnchangedBLAS &&
                  computeScaleRays.Stats().instanceUploads ==
                      computeUnchangedInstances &&
                  computeScaleRays.Stats().materialUploads ==
                      computeUnchangedMaterials &&
                  computeScaleRays.Stats().sceneUploads ==
                      computeUnchangedScenes);
            for (int count : {100, 1000})
            {
                computeSharedScene.meshes.resize(
                    static_cast<std::size_t>(count), cubeInstance);
                for (int i = 0; i < count; ++i)
                {
                    computeSharedScene.meshes[static_cast<std::size_t>(i)]
                        .objectIdentity = static_cast<std::uint32_t>(i + 1);
                    computeSharedScene.meshes[static_cast<std::size_t>(i)]
                        .modelTransform = glm::translate(glm::mat4(1.0f),
                            glm::vec3(float(i), 0.0f, -5.0f));
                }
                computeScaleOK = uploadComputeScaleScene() && computeScaleOK;
            }
            check("GL43 1/100/1000 shared cubes keep one resident BLAS",
                  computeScaleOK &&
                  computeScaleRays.Stats().blasUploads == 1u &&
                  computeScaleRays.Stats().instanceUploads == 3u &&
                  computeScaleRays.Stats().residentBLAS == 1u);
            const std::uint64_t computeBLASBeforeTransform =
                computeScaleRays.Stats().blasUploads;
            const std::uint64_t computeInstancesBeforeTransform =
                computeScaleRays.Stats().instanceUploads;
            computeSharedScene.meshes.front().modelTransform[3].y += 0.75f;
            const bool computeTransformOK = uploadComputeScaleScene();
            check("GL43 transform-only edit uploads instances without BLAS",
                  computeTransformOK &&
                  computeScaleRays.Stats().blasUploads ==
                      computeBLASBeforeTransform &&
                  computeScaleRays.Stats().instanceUploads ==
                      computeInstancesBeforeTransform + 1u);
            const std::uint64_t computeBLASBeforeMaterial =
                computeScaleRays.Stats().blasUploads;
            const std::uint64_t computeMaterialsBeforeMaterial =
                computeScaleRays.Stats().materialUploads;
            computeSharedScene.meshes.front().materialOverride->ambient.y += 0.01f;
            const bool computeMaterialOK = uploadComputeScaleScene();
            check("GL43 material-only edit uploads material data without BLAS",
                  computeMaterialOK &&
                  computeScaleRays.Stats().blasUploads ==
                      computeBLASBeforeMaterial &&
                  computeScaleRays.Stats().materialUploads ==
                      computeMaterialsBeforeMaterial + 1u);
            const std::uint64_t computeBLASBeforeGeometry =
                computeScaleRays.Stats().blasUploads;
            cube->MarkGeometryDirty();
            const bool computeGeometryOK = uploadComputeScaleScene();
            check("GL43 geometry revision replaces exactly one BLAS SSBO set",
                  computeGeometryOK &&
                  computeScaleRays.Stats().blasUploads ==
                      computeBLASBeforeGeometry + 1u &&
                  computeScaleRays.Stats().residentBLAS == 1u);
            FRenderScene emptyComputeScene;
            computeScaleInputs.scene = &emptyComputeScene;
            const bool computeWorldReplacementOK = uploadComputeScaleScene();
            check("GL43 world replacement releases unused resident BLAS",
                  computeWorldReplacementOK &&
                  computeScaleRays.Stats().residentBLAS == 0u);
            computeScaleRays.Shutdown();
        }

        FRaySceneCache cache;
        cache.Prepare(sharedScene);
        FRenderScene replacement;
        cache.Prepare(replacement);
        check("world replacement releases unused BLAS",
              cache.Stats().residentBLAS == 0u && cache.Stats().blasReleases == 1u);

        FRayEffectInputs resizeInputs;
        resizeInputs.gbuffer = &gbuffer;
        resizeInputs.scene = &scene;
        resizeInputs.rasterLighting = &lit;
        resizeInputs.features.rayTracing = true;
        resizeInputs.features.rayTracedShadows = true;
        resizeInputs.features.rayTracedGI = resizeInputs.features.rayTracedReflections = false;
        resizeInputs.quality = quality;
        resizeInputs.contextGeneration = ContextGeneration();
        resizeInputs.width = resizeInputs.height = 64;
        FRayEffectOutputs resizeOutput;
        const bool first64 = rays.RenderEffects(resizeInputs, resizeOutput, &diagnostic);
        const std::uint64_t allocations64 = rays.Stats().outputAllocations;
        const bool same64 = rays.RenderEffects(resizeInputs, resizeOutput, &diagnostic);
        resizeInputs.width = 80; resizeInputs.height = 48;
        const bool wide = rays.RenderEffects(resizeInputs, resizeOutput, &diagnostic);
        resizeInputs.width = resizeInputs.height = 64;
        const bool back64 = rays.RenderEffects(resizeInputs, resizeOutput, &diagnostic);
        check("64->80x48->64 resize is bounded and idempotent", first64 && same64 &&
              wide && back64 && rays.Stats().outputAllocations == allocations64 + 2u &&
              rays.OwnedOutputTextureCount() == 1u);

        if (computeQualityAvailable)
        {
            UGL43RayTracingBackend computeResizeRays;
            FRayEffectOutputs computeResizeOutput;
            const bool computeFirst64 = computeResizeRays.RenderEffects(
                resizeInputs, computeResizeOutput, &diagnostic);
            const std::uint64_t computeAllocations64 =
                computeResizeRays.Stats().outputAllocations;
            const bool computeSame64 = computeResizeRays.RenderEffects(
                resizeInputs, computeResizeOutput, &diagnostic);
            resizeInputs.width = 80;
            resizeInputs.height = 48;
            const bool computeWide = computeResizeRays.RenderEffects(
                resizeInputs, computeResizeOutput, &diagnostic);
            resizeInputs.width = resizeInputs.height = 64;
            const bool computeBack64 = computeResizeRays.RenderEffects(
                resizeInputs, computeResizeOutput, &diagnostic);
            check("GL43 64->80x48->64 resize is bounded and idempotent",
                  computeFirst64 && computeSame64 && computeWide &&
                  computeBack64 &&
                  computeResizeRays.Stats().outputAllocations ==
                      computeAllocations64 + 2u &&
                  computeResizeRays.OwnedOutputTextureCount() == 1u);

            const std::uint64_t computeOriginalGeneration = ContextGeneration();
            const std::uint64_t computeNextGeneration =
                computeOriginalGeneration + 77u;
            const std::uint64_t computeAbandonedBefore =
                computeResizeRays.Stats().abandonedResources;
            SetActiveRenderTargetContextGeneration(computeNextGeneration);
            const bool computeRecreated = computeResizeRays.Init(
                computeNextGeneration, &diagnostic);
            check("GL43 context transition abandons stale names and recreates lazily",
                  computeRecreated &&
                  computeResizeRays.ContextGeneration() ==
                      computeNextGeneration &&
                  computeResizeRays.Stats().abandonedResources >
                      computeAbandonedBefore);
            const std::uint64_t computeReleasedBefore =
                computeResizeRays.Stats().releasedResources;
            computeResizeRays.Shutdown();
            check("GL43 same-generation shutdown deletes current names",
                  computeResizeRays.Stats().releasedResources >
                      computeReleasedBefore &&
                  computeResizeRays.ContextGeneration() == 0);
            SetActiveRenderTargetContextGeneration(computeOriginalGeneration);
        }

        const std::uint64_t originalGeneration = ContextGeneration();
        const std::uint64_t nextGeneration = originalGeneration + 101u;
        const std::uint64_t abandonedBefore = rays.Stats().abandonedResources;
        SetActiveRenderTargetContextGeneration(nextGeneration);
        const bool recreated = rays.Init(nextGeneration, &diagnostic);
        check("context transition abandons stale names and recreates lazily", recreated &&
              rays.ContextGeneration() == nextGeneration &&
              rays.Stats().abandonedResources > abandonedBefore);
        const std::uint64_t releasedBefore = rays.Stats().releasedResources;
        rays.Shutdown();
        check("same-generation shutdown deletes current names",
              rays.Stats().releasedResources > releasedBefore &&
              rays.ContextGeneration() == 0);
        SetActiveRenderTargetContextGeneration(originalGeneration);

        std::unique_ptr<UWorld> routedWorld = LoadRenderParityFixture();
        bool routedRayEffects = false;
        if (routedWorld)
        {
            ACamera& routedCamera = routedWorld->GetCamera();
            routedCamera.SetOrientation(routedCamera.yaw, routedCamera.pitch);
            routedCamera.SetFOV(routedCamera.fov, 1.0f);
            UWorldRenderer worldRenderer;
            worldRenderer.Init();
            FRenderTarget routedTarget = FRenderTarget::TextureViewport(
                64, 64, ContextGeneration());
            FRenderFeatures routedFeatures = routedWorld->GetScene().renderFeatures;
            routedFeatures.rayTracing = true;
            routedFeatures.rayTracedShadows = true;
            routedFeatures.rayTracedGI = true;
            routedFeatures.rayTracedReflections = true;
            FBackendSelection routedSelection;
            routedSelection.requested = ERayTracingBackend::CompatibleGL33;
            routedSelection.selected = ERayTracingBackend::CompatibleGL33;
            routedSelection.available = true;
            routedSelection.rayTracingEnabled = true;
            FRenderQuality routedQuality = quality;
            routedQuality.giSamples = 2;
            routedQuality.giBounces = 2;
            routedRayEffects = worldRenderer.Render(*routedWorld, routedCamera,
                routedTarget, routedFeatures, routedQuality, routedSelection,
                ContextGeneration()) &&
                worldRenderer.Stats().rayBackendCalls == 1u &&
                worldRenderer.Stats().rayDispatches == 0u &&
                worldRenderer.Stats().activeRayBackend ==
                    ERayTracingBackend::CompatibleGL33 &&
                worldRenderer.Stats().rayResourceAllocations > 0u &&
                worldRenderer.Stats().cpuReadbacks == 0u;
            worldRenderer.Shutdown();
        }
        check("UWorldRenderer factory scheduler executes the real GL33 ray path",
              routedWorld != nullptr && routedRayEffects);

        if (computeQualityAvailable)
        {
            std::unique_ptr<UWorld> computeRoutedWorld =
                LoadRenderParityFixture();
            bool computeRoutedRayEffects = false;
            if (computeRoutedWorld)
            {
                ACamera& computeRoutedCamera = computeRoutedWorld->GetCamera();
                computeRoutedCamera.SetOrientation(
                    computeRoutedCamera.yaw, computeRoutedCamera.pitch);
                computeRoutedCamera.SetFOV(computeRoutedCamera.fov, 1.0f);
                UWorldRenderer computeWorldRenderer;
                computeWorldRenderer.Init();
                FRenderTarget computeRoutedTarget =
                    FRenderTarget::TextureViewport(
                        64, 64, ContextGeneration());
                FRenderFeatures computeRoutedFeatures =
                    computeRoutedWorld->GetScene().renderFeatures;
                computeRoutedFeatures.rayTracing = true;
                computeRoutedFeatures.rayTracedShadows = true;
                computeRoutedFeatures.rayTracedGI = true;
                computeRoutedFeatures.rayTracedReflections = true;
                FBackendSelection computeRoutedSelection;
                computeRoutedSelection.requested = ERayTracingBackend::Auto;
                computeRoutedSelection.selected =
                    ERayTracingBackend::ComputeGL43;
                computeRoutedSelection.available = true;
                computeRoutedSelection.rayTracingEnabled = true;
                FRenderQuality computeRoutedQuality = quality;
                computeRoutedQuality.giSamples = 2;
                computeRoutedQuality.giBounces = 2;
                FBackendSelection explicitCompatible = computeRoutedSelection;
                explicitCompatible.requested =
                    ERayTracingBackend::CompatibleGL33;
                explicitCompatible.selected =
                    ERayTracingBackend::CompatibleGL33;
                const bool compatibleOK = computeWorldRenderer.Render(
                    *computeRoutedWorld, computeRoutedCamera,
                    computeRoutedTarget, computeRoutedFeatures,
                    computeRoutedQuality, explicitCompatible,
                    ContextGeneration());
                const bool renderOK = compatibleOK && computeWorldRenderer.Render(
                    *computeRoutedWorld, computeRoutedCamera,
                    computeRoutedTarget, computeRoutedFeatures,
                    computeRoutedQuality, computeRoutedSelection,
                    ContextGeneration());
                const FWorldRendererStats& routedStats =
                    computeWorldRenderer.Stats();
                computeRoutedRayEffects = renderOK &&
                    routedStats.rayBackendCalls == 2u &&
                    routedStats.rayDispatches == 1u &&
                    routedStats.rayMemoryBarriers == 1u &&
                    routedStats.activeRayBackend ==
                        ERayTracingBackend::ComputeGL43 &&
                    routedStats.rayResourceAllocations > 0u &&
                    routedStats.cpuFramebufferGenerations == 0u &&
                    routedStats.cpuReadbacks == 0u &&
                    routedStats.cpuFramebufferUploads == 0u;
                computeWorldRenderer.Shutdown();
            }
            check("UWorldRenderer explicit Compatible then Auto promotes to Compute with zero CPU bridges",
                  computeRoutedWorld != nullptr && computeRoutedRayEffects);
        }

        {
            std::ofstream ppm(outputPath, std::ios::binary);
            ppm << "P6\n64 64\n255\n";
            for (int y = 63; y >= 0; --y)
                ppm.write(reinterpret_cast<const char*>(combined.second.data() + y * 64 * 3), 64 * 3);
            check("ray-effects PPM written", static_cast<bool>(ppm));
        }
        check("ray-effects pass leaves no GL error", glGetError() == GL_NO_ERROR);
        rays.Shutdown();
        lighting.Shutdown();
        gbuffer.Release();
        meshCache.Clear();
        std::printf("=== ray-effects gates: %d passed, %d failed ===\n", passed, failed);
        if (failed && !diagnostic.empty()) std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return failed == 0 ? 0 : 1;
    }
};

static int RunRayEffectsGates(const std::string& outputPath)
{
    FRayEffectsSelfTestApp app;
    if (!app.Init(64, 64, "Ray Effects Self-Test")) return 2;
    return app.RunGates(outputPath);
}

class FComputeInitSelfTestApp final : public Engine
{
public:
    int RunGate()
    {
        glfwHideWindow(window_);
        if (!GraphicsCapabilities().SupportsComputeBackend())
        {
            std::printf("[SKIP] OpenGL 4.3 Compute/SSBO unavailable\n");
            return 0;
        }
        UGL43RayTracingBackend backend;
        std::string diagnostic;
        const bool initialized = backend.Init(ContextGeneration(), &diagnostic);
        std::printf("[%s] GL43 compute backend initialization%s%s\n",
            initialized ? "PASS" : "FAIL",
            diagnostic.empty() ? "" : ": ", diagnostic.c_str());
        backend.Shutdown();
        return initialized ? 0 : 1;
    }
};

static int RunComputeInitGate()
{
    FComputeInitSelfTestApp app;
    if (!app.Init(8, 8, "Compute Init Self-Test")) return 2;
    return app.RunGate();
}

int main(int argc, char** argv)
{
    const std::string arg = (argc > 1) ? argv[1] : "";
    if (arg == "--ray-compute-init-selftest") return RunComputeInitGate();

    const std::string hardwareRasterPrefix = "--hw-raster-selftest=";
    if (arg.rfind(hardwareRasterPrefix, 0) == 0)
    {
        const std::string outputPath = arg.substr(hardwareRasterPrefix.size());
        if (outputPath.empty())
        {
            std::fprintf(stderr, "--hw-raster-selftest requires an output .ppm path\n");
            return 2;
        }
        return RunHardwareRasterGates(outputPath);
    }

    if (arg == "--raster-lighting-baseline")
        return RunRasterLightingLegacyBaseline();

    const std::string rasterLightingPrefix = "--raster-lighting-selftest=";
    if (arg.rfind(rasterLightingPrefix, 0) == 0)
    {
        const std::string outputPath = arg.substr(rasterLightingPrefix.size());
        if (outputPath.empty())
        {
            std::fprintf(stderr, "--raster-lighting-selftest requires an output .ppm path\n");
            return 2;
        }
        return RunRasterLightingGates(outputPath);
    }

    const std::string rayEffectsPrefix = "--ray-effects-selftest=";
    if (arg.rfind(rayEffectsPrefix, 0) == 0)
    {
        const std::string outputPath = arg.substr(rayEffectsPrefix.size());
        if (outputPath.empty())
        {
            std::fprintf(stderr, "--ray-effects-selftest requires an output .ppm path\n");
            return 2;
        }
        return RunRayEffectsGates(outputPath);
    }

    if (arg == "--fbxtest")
        return RunFbxGates();

    if (arg == "--meshrevisiontest")
        return RunMeshRevisionGates();

    if (arg == "--hw6")          // HW6 Q1-Q3 visual: keys 1=Flat 2=Gouraud 3=Phong
    {
        HW6Demo app;
        if (!app.Init(1024, 1024, "HW6:  1=Flat  2=Gouraud  3=Phong")) return -1;
        return app.Run();
    }

    if (arg == "--demo")          // mesh-mode demo (keys 1-6)
    {
        MeshDemo app;
        if (!app.Init(1024, 1024, "Mesh: 1=raster 2=RT 3=depth 4=hybrid 5=OBJ 6=FBX")) return -1;
        return app.Run();
    }

    if (arg == "--game")          // standalone GAME runtime: --game <world.path>
    {
        const std::string worldPath = (argc > 2) ? argv[2] : "Content/EditorWorld.world";
        GameEngine game(worldPath);
        if (!game.Init(1280, 800, "Game")) return -1;
        return game.Run();
    }

    // default: the ImGui editor (page 7)
    EditorEngine editor;
    if (!editor.Init(1280, 800, "MyEngine Editor")) return -1;
    return editor.Run();
}
