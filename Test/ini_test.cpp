// ---------------------------------------------------------------------------
// ini_test.cpp — GL-free self-test for P7 ini parsing + project/settings load.
//
// Build (msys2 ucrt64):
//   g++ -std=c++17 -I include -I Engine/Serialization -I Engine/Framework \
//       Test/ini_test.cpp Engine/Serialization/FIniFile.cpp \
//       Engine/Framework/FProjectDescriptor.cpp -o ini_test && ./ini_test
// ---------------------------------------------------------------------------
#include "FIniFile.h"
#include "FProjectDescriptor.h"
#include <cstdio>
#include <cmath>
#include <fstream>

static int g_pass = 0, g_fail = 0;
static void ck(const char* t, const char* w, bool ok)
{
    std::printf("[%s] %s -> %s\n", t, w, ok ? "PASS" : "FAIL");
    ok ? ++g_pass : ++g_fail;
}
static void writeFile(const char* path, const char* text)
{
    std::ofstream f(path); f << text;
}

int main()
{
    FIniFile ini;
    ini.Parse(
        "# comment line\n"
        "[Display]\n"
        "Title = My Project\n"
        "Width = 1280\n"
        "Height = 720\n"
        "\n"
        "[Physics]\n"
        "GravityEnabled = true\n"
        "Gravity = 0 -9.8 0\n"
        "Exp = 0.6\n"
        "garbage line no equals\n");

    ck("T1", "GetString section/key", ini.GetString("Display", "Title") == "My Project");
    ck("T2", "GetInt", ini.GetInt("Display", "Width") == 1280 && ini.GetInt("Display", "Height") == 720);
    ck("T3", "GetVec3", ini.GetVec3("Physics", "Gravity") == glm::vec3(0, -9.8f, 0));
    ck("T4", "GetBool true", ini.GetBool("Physics", "GravityEnabled") == true);
    ck("T5", "case-insensitive", ini.GetInt("display", "WIDTH") == 1280);
    ck("T6", "missing -> default", ini.GetInt("Display", "Depth", 42) == 42 && ini.GetString("None", "X", "d") == "d");
    ck("T7", "comments/garbage skipped (no spurious keys)", !ini.Has("Display", "garbage line no equals"));

    // FProjectDescriptor.LoadSettings (Setting/DefaultEngine.ini)
    {
        writeFile("ini_test.engine.tmp",
            "[Display]\nTitle = Game X\nWidth = 800\nHeight = 600\n"
            "[Render]\nMode = Hybrid\n[Startup]\nDefaultWorld = Level1\n");
        FProjectDescriptor d;
        bool ok = d.LoadSettings("ini_test.engine.tmp");
        ck("T8", "LoadSettings Display/Render/Startup",
           ok && d.windowTitle == "Game X" && d.width == 800 && d.height == 600 &&
           d.renderMode == EProjectRenderMode::Hybrid && d.startupWorld == "Level1");
        std::remove("ini_test.engine.tmp");
    }
    // FProjectDescriptor.LoadProject (.proj manifest)
    {
        writeFile("ini_test.proj.tmp", "ProjectName = MyGame\nEngineVersion = 1.0\n");
        FProjectDescriptor d;
        bool ok = d.LoadProject("ini_test.proj.tmp");
        ck("T9", "LoadProject ProjectName/EngineVersion",
           ok && d.projectName == "MyGame" && d.engineVersion == "1.0");
        std::remove("ini_test.proj.tmp");
    }
    // robustness: missing file -> false + defaults kept
    {
        FProjectDescriptor d;
        bool missing = d.LoadSettings("does_not_exist_zzz.ini");
        ck("T10", "missing ini -> false + defaults", !missing && d.width == 1280 && d.windowTitle == "Engine");
    }

    std::printf("=== ini: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
