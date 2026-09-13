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
        for (GLuint drawBuffer = 0; drawBuffer < 6; ++drawBuffer)
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
        check("resize owns one bounded attachment set", gbuffer.OwnedTextureCount() == 7u &&
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
        GLuint oldColorTextures[6] = {};
        for (unsigned semantic = 0; semantic < 6; ++semantic)
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
        glDeleteTextures(6, oldColorTextures);
        glDeleteTextures(1, &oldDepthTexture);
        glDeleteFramebuffers(1, &oldFramebuffer);

        const GLuint currentProgram = rasterizer.Program();
        const GLuint currentFramebuffer = gbuffer.Framebuffer();
        const GLuint currentDepthTexture = gbuffer.DepthTexture();
        GLuint currentColorTextures[6] = {};
        for (unsigned semantic = 0; semantic < 6; ++semantic)
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
