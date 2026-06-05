#include "FProcess.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>

namespace FProcess
{
    std::string ExecutablePath()
    {
        char buf[MAX_PATH] = {};
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        return std::string(buf, buf + n);
    }

    bool LaunchDetached(const std::string& exe, const std::string& args)
    {
        std::string cmd = "\"" + exe + "\" " + args;   // quoted exe + args

        STARTUPINFOA si{};        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<char> cmdline(cmd.begin(), cmd.end());
        cmdline.push_back('\0');  // CreateProcess may modify the command line buffer

        BOOL ok = CreateProcessA(exe.c_str(), cmdline.data(),
                                 nullptr, nullptr, FALSE,
                                 0, nullptr, nullptr, &si, &pi);
        if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
        return ok != FALSE;
    }
}
