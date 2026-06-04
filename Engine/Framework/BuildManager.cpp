#include "BuildManager.h"
#include "FIniFile.h"

#include <cstdio>

BuildManager::~BuildManager()
{
    if (worker_.joinable()) worker_.join();
}

std::vector<std::string> BuildManager::Snapshot() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return log_;
}

void BuildManager::Start(const std::string& solution, const std::string& config,
                         const std::string& target, const std::string& iniPath)
{
    if (running_) return;
    if (worker_.joinable()) worker_.join();   // reap a finished previous build

    { std::lock_guard<std::mutex> lk(mtx_); log_.clear(); }
    done_ = false; success_ = false; running_ = true;

    // MSBuild path from the engine-internal ini (fallback: rely on PATH).
    FIniFile ini;
    std::string msb = "msbuild";
    if (ini.LoadFromFile(iniPath.c_str()))
        msb = ini.GetString("Build", "MSBuildPath", "msbuild");

    std::string cmd = "\"" + msb + "\" \"" + solution + "\"";
    if (!target.empty()) cmd += " /t:" + target;
    cmd += " /p:Configuration=" + config + " /p:Platform=Win32 /v:minimal /nologo 2>&1";

    worker_ = std::thread([this, cmd] { run(cmd); });
}

void BuildManager::run(std::string cmd)
{
    auto push = [this](const std::string& s)
    { std::lock_guard<std::mutex> lk(mtx_); log_.push_back(s); };

    push("> " + cmd);

    // _popen runs the command through cmd.exe /c; wrap the whole thing in quotes
    // so the inner quoted paths survive.
    const std::string full = "\"" + cmd + "\"";
    FILE* pipe = _popen(full.c_str(), "r");
    if (!pipe)
    {
        push("[BuildManager] failed to launch MSBuild (check Config/Engine.ini MSBuildPath)");
        success_ = false; done_ = true; running_ = false;
        return;
    }

    char buf[1024];
    while (std::fgets(buf, sizeof(buf), pipe))
    {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        push(line);
    }

    const int rc = _pclose(pipe);
    success_ = (rc == 0);
    push(success_ ? "[BuildManager] BUILD SUCCEEDED" : "[BuildManager] BUILD FAILED");
    done_ = true; running_ = false;
}
