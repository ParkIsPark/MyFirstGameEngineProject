// Current complete recipe: powershell -NoProfile -ExecutionPolicy Bypass -File Test/RunStandaloneTests.ps1 -Name DeveloperSettingsTest
// Prerequisite: Engine.sln Debug|Win32. Older direct compile examples follow.
// PowerShell (MSYS2 UCRT64):
// & C:/msys64/ucrt64/bin/g++.exe -std=c++17 -Wall -Wextra -pedantic -IEngine/Render -IEngine/Core -IEngine/Developer -IEngine/Editor Test/DeveloperSettingsTest.cpp Engine/Developer/FDeveloperSettings.cpp Engine/Core/FFeatureLifecycle.cpp -o DeveloperSettingsTest.exe
// ./DeveloperSettingsTest.exe
#include <iostream>
#if !__has_include("FDeveloperSettings.h")
int main() { std::cerr << "FAIL: local Developer Settings contract is not implemented\n"; return 1; }
#else
#include "FDeveloperSettings.h"
#include "DeveloperSettingsPanel.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

static int checks = 0;
static void Check(bool ok, const char* label) { if (!ok) throw std::runtime_error(label); ++checks; }
static std::string Read(const std::filesystem::path& p) { std::ifstream in(p); return {std::istreambuf_iterator<char>(in), {}}; }
static void Write(const std::filesystem::path& p, const std::string& s) { std::ofstream(p) << s; }
struct Counts { int made=0, init=0, execute=0, stop=0, destroyed=0; };
struct Route : IDeveloperRenderRoute {
    Counts& c;
    ELegacyRendererOverride kind;
    bool initSucceeds=true, renderSucceeds=true, initThrows=false, renderThrows=false;
    explicit Route(Counts& value, ELegacyRendererOverride selected) : c(value),kind(selected) { ++c.made; }
    ELegacyRendererOverride OverrideKind() const noexcept override { return kind; }
    ~Route() override { ++c.destroyed; }
    bool Init() override { ++c.init; if(initThrows) throw std::runtime_error("partial init failure"); return initSucceeds; }
    void Shutdown() noexcept override { ++c.stop; }
    bool Render(const FDeveloperRenderFrame&) override { ++c.execute; if(renderThrows) throw std::runtime_error("draw failed"); return renderSucceeds; }
    FDeveloperRouteStats Stats() const noexcept override { return {}; }
};
int main() {
    const auto dir = std::filesystem::path("Test/DeveloperSettingsTest.tmp");
    const auto path = dir / "DeveloperSettings.ini";
    std::filesystem::create_directory(dir);
    try {
        std::vector<std::string> warnings;
        auto sink = [&](const std::string& s) { warnings.push_back(s); };
        auto defaults = [&](const FDeveloperSettings& s) { Check(s.legacyOverride==ELegacyRendererOverride::None && s.showDeprecatedFeatures && s.showExperimentalWarnings, "invalid input must use safe defaults"); };
        defaults(FDeveloperSettings::Load(path, sink));
        Check(!warnings.empty(), "missing settings diagnostic");
        for (const auto& bad : std::vector<std::string>{"", "garbage", "[Rendering]\nLegacyOverride=bad\n", "[Rendering]\nShowDeprecatedFeatures=maybe\n", "[Rendering]\nShowExperimentalWarnings=2\n", "[Other]\nLegacyOverride=SoftwareRasterizer\n", "[Rendering]\nLegacyOverride=None\nLegacyOverride=SoftwareRasterizer\n"}) {
            warnings.clear(); Write(path,bad); defaults(FDeveloperSettings::Load(path,sink));
            Check(!warnings.empty() && warnings[0].find("DeveloperSettings")!=std::string::npos, "actionable rejected input diagnostic");
        }
        defaults(FDeveloperSettings::Load(dir, sink));
        for (const auto& invalid : std::vector<std::string>{
            "LegacyOverride=Invalid\nShowDeprecatedFeatures=true\nShowExperimentalWarnings=true\n",
            "LegacyOverride=SoftwareRasterizer\nShowDeprecatedFeatures=0\nShowExperimentalWarnings=true\n",
            "LegacyOverride=PureGPURayTracer\nShowDeprecatedFeatures=false\nShowExperimentalWarnings=yes\n"}) {
            warnings.clear(); Write(path,"[Rendering]\n"+invalid);
            defaults(FDeveloperSettings::Load(path,sink));
            Check(warnings.size()==1 && warnings[0].find("must be")!=std::string::npos, "each invalid complete value has specific actionable diagnostic");
        }
        const ELegacyRendererOverride kinds[] = {ELegacyRendererOverride::None, ELegacyRendererOverride::SoftwareRasterizer, ELegacyRendererOverride::PureGPURayTracer};
        const char* names[] = {"None", "SoftwareRasterizer", "PureGPURayTracer"};
        for (int k=0;k<3;++k) for (bool deprecated : {false,true}) for (bool experimental : {false,true}) {
            FDeveloperSettings s; s.legacyOverride=kinds[k]; s.showDeprecatedFeatures=deprecated; s.showExperimentalWarnings=experimental;
            Check(s.Save(path,sink), "transactional settings save");
            Check(Read(path)==std::string("[Rendering]\nLegacyOverride=")+names[k]+"\nShowDeprecatedFeatures="+(deprecated?"true":"false")+"\nShowExperimentalWarnings="+(experimental?"true":"false")+"\n", "canonical local shape");
            warnings.clear(); auto loaded=FDeveloperSettings::Load(path,sink);
            Check(loaded.legacyOverride==kinds[k] && loaded.showDeprecatedFeatures==deprecated && loaded.showExperimentalWarnings==experimental && warnings.empty(), "canonical round trip");
        }
        FDeveloperSettings s; s.legacyOverride=ELegacyRendererOverride::SoftwareRasterizer;
        const auto saved=Read(path);
        std::filesystem::create_directory(path.string()+".tmp");
        Check(!s.Save(path,sink) && Read(path)==saved && s.legacyOverride==ELegacyRendererOverride::SoftwareRasterizer, "failed temporary save preserves old file and memory");
        std::filesystem::remove(path.string()+".tmp");
        Write(path.string()+".tmp","other editor's pending valid save");
        Check(!s.Save(path,sink) && Read(path)==saved && Read(path.string()+".tmp")=="other editor's pending valid save", "pending save ownership is preserved");
        std::filesystem::remove(path.string()+".tmp");
        Check(!s.Save(dir,sink), "save to directory fails safely");
#ifdef _WIN32
        HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(locked!=INVALID_HANDLE_VALUE, "replacement failure fixture locks valid file");
        const bool lockedSave=s.Save(path,sink);
        CloseHandle(locked);
        Check(!lockedSave && Read(path)==saved && !std::filesystem::exists(path.string()+".tmp"), "failed replacement preserves valid file and removes owned temporary");
#endif
        Check(!s.Save(path/"child",[](const std::string&) { throw 1; }), "throwing warning sink cannot escape persistence");
        FRenderFeatures features;
        auto model=DescribeDeveloperSettings(s,features,ERayTracingBackend::CompatibleGL33);
        Check(model.legacyChoices.size()==3 && model.legacyChoices[0].label=="None", "explicit None choice");
        const char* ids[]={"", "renderer.cpu_software", "renderer.gpu_pure_raytracer"};
        for (int k=1;k<3;++k) {
            const auto& row=model.legacyChoices[k]; const auto& registry=FFeatureLifecycleRegistry::Require(ids[k]);
            Check(row.overrideKind==kinds[k] && row.label==registry.displayName && row.feature==&registry, "rows map exact enum to authoritative registry");
            Check(row.badge=="Deprecated" && row.details.find(registry.deprecatedVersion)!=std::string::npos && row.details.find(registry.replacement)!=std::string::npos && row.details.find(registry.purpose)!=std::string::npos && row.details.find("Removal: none")!=std::string::npos, "all lifecycle details retained");
        }
        s.showDeprecatedFeatures=false;
        model=DescribeDeveloperSettings(s,features,ERayTracingBackend::CompatibleGL33);
        Check(model.legacyChoices.empty() && model.activeWarning.find("CPU Software Rasterizer")!=std::string::npos && model.activeWarning.find("Hardware Rasterizer")!=std::string::npos, "active warning independent of hidden choices/window");
        s.legacyOverride=ELegacyRendererOverride::None; features.rayTracing=true; features.rayTracingBackend=ERayTracingBackend::ComputeGL43;
        model=DescribeDeveloperSettings(s,features,ERayTracingBackend::CompatibleGL33);
        Check(model.activeWarning.empty() && model.experimentalWarning.find(FFeatureLifecycleRegistry::Require("renderer.rt_compute_gl43").displayName)!=std::string::npos, "requested Compute lifecycle warning");
        features.rayTracingBackend=ERayTracingBackend::Auto;
        Check(!DescribeDeveloperSettings(s,features,ERayTracingBackend::ComputeGL43).experimentalWarning.empty(), "active Auto Compute warning");
        Check(DescribeDeveloperSettings(s,features,ERayTracingBackend::CompatibleGL33).experimentalWarning.empty(), "GL33 not experimental");
        s.showExperimentalWarnings=false;
        Check(DescribeDeveloperSettings(s,features,ERayTracingBackend::ComputeGL43).experimentalWarning.empty(), "experimental checkbox suppresses badge");
        s.showExperimentalWarnings=true; features.rayTracing=false;
        Check(DescribeDeveloperSettings(s,features,ERayTracingBackend::ComputeGL43).experimentalWarning.empty(), "disabled RT not experimental");
        Counts cpu,gpu; int normal=0; warnings.clear(); std::vector<ELegacyRendererOverride> selected;
        FDeveloperOverrideController controller([&](ELegacyRendererOverride kind) -> std::unique_ptr<IDeveloperRenderRoute> { selected.push_back(kind); return std::make_unique<Route>(kind==ELegacyRendererOverride::SoftwareRasterizer?cpu:gpu,kind); },sink);
        FDeveloperRenderFrame frame;
        auto render=[&] { return controller.Render(frame,[&] { ++normal; return true; }); };
        controller.Apply(ELegacyRendererOverride::None); Check(render() && normal==1 && selected.empty() && warnings.empty(), "None only renders normal route");
        controller.Apply(kinds[1]); controller.Apply(kinds[1]); render(); render();
        Check(cpu.made==1 && cpu.init==1 && cpu.execute==2 && normal==1 && warnings.size()==1, "same override no-op and exclusive legacy route");
        controller.Apply(kinds[2]); render();
        Check(cpu.stop==1 && cpu.destroyed==1 && gpu.made==1 && gpu.init==1 && gpu.execute==1 && selected[0]==kinds[1] && selected[1]==kinds[2] && warnings.size()==2, "switch stops old route and selects exact factory");
        controller.Apply(kinds[0]); render(); Check(normal==2 && gpu.stop==1 && gpu.destroyed==1, "None restores one normal route");
        controller.Apply(kinds[1]); controller.Apply(kinds[2]); Check(warnings.size()==2, "once per session per distinct deprecated override");
        Check(warnings[0].find("2.0")!=std::string::npos && warnings[1].find("ray-traced effects")!=std::string::npos, "warnings include registry version and replacement");
        controller.Shutdown(); controller.Shutdown(); Check(gpu.stop==2 && gpu.destroyed==2, "shutdown idempotent");
        FDeveloperOverrideController failedFactory([](ELegacyRendererOverride) -> std::unique_ptr<IDeveloperRenderRoute> { throw std::runtime_error("injected factory failure"); },sink);
        Check(!failedFactory.Apply(kinds[1]) && failedFactory.ActiveOverride()==kinds[0] && failedFactory.Render(frame,[]{return true;}), "factory failure safely returns to normal route");
        Counts mismatched;
        FDeveloperOverrideController wrongFactory([&](ELegacyRendererOverride) -> std::unique_ptr<IDeveloperRenderRoute> { return std::make_unique<Route>(mismatched,kinds[2]); },sink);
        Check(!wrongFactory.Apply(kinds[1]) && mismatched.init==0 && mismatched.execute==0 && mismatched.destroyed==1, "wrong concrete factory identity must be rejected before initialization");
        Counts failing; bool failInit=true; bool throwInit=false; bool failRender=false; bool throwRender=false;
        warnings.clear();
        FDeveloperOverrideController failures([&](ELegacyRendererOverride kind) -> std::unique_ptr<IDeveloperRenderRoute> {
            auto route=std::make_unique<Route>(failing,kind);
            route->initSucceeds=!failInit; route->initThrows=throwInit;
            route->renderSucceeds=!failRender; route->renderThrows=throwRender;
            return route;
        },sink);
        Check(!failures.Apply(kinds[2]) && failures.ActiveOverride()==kinds[0] && failing.init==1 && failing.stop==1 && failing.destroyed==1, "not-ready Init must clean partial route and remain None");
        Check(warnings.size()==1 && warnings[0].find("activation failed")!=std::string::npos && warnings[0].find("Hardware Rasterizer")!=std::string::npos, "failed activation emits actionable diagnostic, not deprecated activation warning");
        Check(failures.Render(frame,[]{return true;}) && failing.execute==0, "failed initialization resumes normal without legacy execution");
        failInit=false;
        Check(failures.Apply(kinds[2]) && failures.Render(frame,[]{return false;}) && failing.init==2 && failing.execute==1 && warnings.size()==2, "retry initializes successfully and emits first deprecated warning");
        failures.Shutdown(); throwInit=true;
        Check(!failures.Apply(kinds[2]) && failing.stop==3 && failing.destroyed==3 && failures.ActiveOverride()==kinds[0], "throwing partial Init is cleaned");
        throwInit=false; failRender=true;
        Check(failures.Apply(kinds[2]), "retry after throwing Init");
        int failedFrameNormal=0;
        Check(!failures.Render(frame,[&]{++failedFrameNormal;return true;}) && failedFrameNormal==0 && failures.ActiveOverride()==kinds[0] && failing.stop==4 && failing.destroyed==4, "failed Render disables route without double rendering failed frame");
        Check(failures.Render(frame,[&]{++failedFrameNormal;return true;}) && failedFrameNormal==1, "next frame after failed Render uses hardware");
        failRender=false; throwRender=true;
        Check(failures.Apply(kinds[2]) && !failures.Render(frame,[]{return true;}) && failing.stop==5 && failing.destroyed==5 && failures.ActiveOverride()==kinds[0], "throwing Render also cleans and disables broken override");
        int successfulActivationWarnings=0;
        for(const auto& warning:warnings) if(warning.find(" | Deprecated ")!=std::string::npos) ++successfulActivationWarnings;
        Check(successfulActivationWarnings==1, "failure/retry cycles never repeat a successful activation warning");
        FDeveloperOverrideController nullFactory([](ELegacyRendererOverride)->std::unique_ptr<IDeveloperRenderRoute>{return nullptr;},sink);
        Check(!nullFactory.Apply(kinds[1]) && nullFactory.ActiveOverride()==kinds[0], "null factory leaves None");
        Counts destructorCounts;
        { FDeveloperOverrideController scoped([&](ELegacyRendererOverride kind)->std::unique_ptr<IDeveloperRenderRoute>{return std::make_unique<Route>(destructorCounts,kind);},sink); scoped.Apply(kinds[1]); }
        Check(destructorCounts.stop==1 && destructorCounts.destroyed==1, "controller destructor closes active route once");
        for (const char* source : {"Engine/Framework/GameEngine.cpp", "Engine/Framework/GameEngine.h", "Engine/Serialization/FWorldSerializer.cpp", "Engine/Framework/FProjectDescriptor.cpp"}) {
            Check(std::filesystem::exists(source), "isolation source must exist");
            Check(Read(source).find("DeveloperSettings")==std::string::npos && Read(source).find("LegacyOverride")==std::string::npos, "game and serializers exclude local override");
        }
        Check(Read(".gitignore").find("Config/DeveloperSettings.ini")!=std::string::npos, "exact local path ignored");
        const auto editor=Read("Engine/Editor/EditorEngine.cpp"); const auto panelStart=editor.find("void EditorEngine::DrawRenderSettings()"); const auto panelEnd=editor.find("void EditorEngine::",panelStart+5);
        const auto normalPanel=editor.substr(panelStart,panelEnd-panelStart);
        Check(normalPanel.find("SoftwareRasterizer")==std::string::npos && normalPanel.find("PureGPURayTracer")==std::string::npos, "normal Render Settings excludes legacy choices");
        const auto saveStart=editor.find("void EditorEngine::SaveRenderSettings()");
        const auto saveEnd=editor.find("void EditorEngine::",saveStart+5);
        const auto renderSave=editor.substr(saveStart,saveEnd-saveStart);
        Check(renderSave.find("DeveloperSettings")==std::string::npos && renderSave.find("LegacyOverride")==std::string::npos, "Editor/Game quality serialization excludes Developer Settings");
        std::filesystem::remove_all(dir);
        std::cout << "DeveloperSettingsTest: " << checks << " checks passed\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; std::filesystem::remove_all(dir); return 1; }
}
#endif
