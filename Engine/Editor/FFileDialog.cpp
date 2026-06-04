#include "FFileDialog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

namespace FFileDialog
{
    std::string OpenAsset()
    {
        char buf[2048] = {};
        // Filter is a double-null-terminated "label\0pattern\0...\0\0" block.
        static const char kFilter[] =
            "Assets (*.obj;*.fbx;*.world;*.hdr;*.png;*.jpg)\0*.obj;*.fbx;*.world;*.hdr;*.png;*.jpg;*.jpeg\0"
            "All Files (*.*)\0*.*\0";

        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = GetActiveWindow();
        ofn.lpstrFilter = kFilter;
        ofn.lpstrFile   = buf;
        ofn.nMaxFile    = sizeof(buf);
        ofn.lpstrTitle  = "Import Asset";
        // OFN_NOCHANGEDIR: keep the process CWD (Content/ paths stay relative).
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn)) return std::string(buf);
        return std::string();
    }
}
