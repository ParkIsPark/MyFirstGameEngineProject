#include "FFileDialog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

namespace FFileDialog
{
    namespace
    {
        std::string OpenWithFilter(const char* filter, const char* title)
        {
            char buf[2048] = {};
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = GetActiveWindow();
            ofn.lpstrFilter = filter;
            ofn.lpstrFile   = buf;
            ofn.nMaxFile    = sizeof(buf);
            ofn.lpstrTitle  = title;
            // OFN_NOCHANGEDIR: keep the process CWD (Content/ paths stay relative).
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameA(&ofn)) return std::string(buf);
            return std::string();
        }
    }

    std::string OpenAsset()
    {
        static const char kFilter[] =
            "Assets (*.obj;*.fbx;*.world;*.hdr;*.png;*.jpg)\0*.obj;*.fbx;*.world;*.hdr;*.png;*.jpg;*.jpeg\0"
            "All Files (*.*)\0*.*\0";
        return OpenWithFilter(kFilter, "Import Asset");
    }

    std::string OpenLuaScript()
    {
        static const char kFilter[] =
            "Lua Scripts (*.lua)\0*.lua\0"
            "All Files (*.*)\0*.*\0";
        return OpenWithFilter(kFilter, "Assign Lua Script");
    }
}
