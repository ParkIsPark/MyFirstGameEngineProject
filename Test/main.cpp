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
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
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

int main(int argc, char** argv)
{
    const std::string arg = (argc > 1) ? argv[1] : "";

    if (arg == "--fbxtest")
        return RunFbxGates();

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
