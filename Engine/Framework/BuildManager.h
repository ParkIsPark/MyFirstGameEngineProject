#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

// ---------------------------------------------------------------------------
// BuildManager (P9) — runs MSBuild in a background thread and streams its output
// into a log the editor can display. The MSBuild path comes from
// Config/Engine.ini ([Build] MSBuildPath) -- an engine-internal setting, not the
// project's ini (per the PIE/build design decision).
//
// In a generated project this builds the game's <name>.exe; in this engine repo
// the editor builds the `Engine` library target (the running Test.exe is locked,
// so the whole-solution exe link can't be rebuilt in-place) -- enough to exercise
// + demonstrate the ini->msbuild->log pipeline.
// ---------------------------------------------------------------------------
class BuildManager
{
public:
    ~BuildManager();

    // Kick off `msbuild <solution> /t:<target> /p:Configuration=<config> ...` in a
    // worker thread. No-op if a build is already running. target "" = whole sln.
    void Start(const std::string& solution, const std::string& config,
               const std::string& target = "", const std::string& iniPath = "Config/Engine.ini");

    bool IsRunning() const { return running_; }
    bool Done()      const { return done_; }
    bool Succeeded() const { return success_; }
    std::vector<std::string> Snapshot() const;   // thread-safe copy of the log

private:
    void run(std::string cmd);

    std::thread              worker_;
    mutable std::mutex       mtx_;
    std::vector<std::string> log_;
    std::atomic<bool>        running_{ false };
    std::atomic<bool>        done_{ false };
    std::atomic<bool>        success_{ false };
};
