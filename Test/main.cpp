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
            check("raster-only/neutral-RT hardware executor performs zero CPU or ray work",
                  normalStats.hardwareGBufferPasses == 3u &&
                  normalStats.rasterLightingPasses == 3u &&
                  normalStats.compositePasses == 3u &&
                  normalStats.rayResourceAllocations == 0u &&
                  normalStats.rayDispatches == 0u &&
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

int main(int argc, char** argv)
{
    const std::string arg = (argc > 1) ? argv[1] : "";

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
