#include "FDeveloperSettings.h"
#include "../Editor/DeveloperSettingsPanel.h"
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
std::string Trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
const char* OverrideName(ELegacyRendererOverride kind)
{
    switch (kind) {
    case ELegacyRendererOverride::None: return "None";
    case ELegacyRendererOverride::SoftwareRasterizer: return "SoftwareRasterizer";
    case ELegacyRendererOverride::PureGPURayTracer: return "PureGPURayTracer";
    }
    throw std::invalid_argument("LegacyOverride must be None, SoftwareRasterizer, or PureGPURayTracer");
}
std::string Details(const FFeatureDescriptor& feature)
{
    return "Deprecated: " + feature.deprecatedVersion + " | Replacement: " + feature.replacement +
        " | Removal: " + (feature.removalPolicy == EFeatureRemovalPolicy::None ? "none" : "planned") +
        "\n" + feature.purpose;
}
const char* LifecycleLabel(EFeatureLifecycle lifecycle)
{
    switch (lifecycle) {
    case EFeatureLifecycle::Stable: return "Stable";
    case EFeatureLifecycle::Experimental: return "Experimental";
    case EFeatureLifecycle::Deprecated: return "Deprecated";
    case EFeatureLifecycle::Internal: return "Internal";
    }
    return "Unknown";
}
void AddStats(FDeveloperRouteStats& total, const FDeveloperRouteStats& value)
{
    total.initializationAttempts += value.initializationAttempts;
    total.initializations += value.initializations;
    total.executions += value.executions;
    total.shutdowns += value.shutdowns;
}
}

void EmitDeveloperWarning(const FDeveloperWarningSink& sink, const std::string& message) noexcept
{
    try { if (sink) sink(message); else std::cerr << "[Developer Settings] " << message << '\n'; }
    catch (...) {} // Diagnostics must never abort Editor startup or teardown.
}

FDeveloperSettings FDeveloperSettings::Load(const std::filesystem::path& path,
                                          const FDeveloperWarningSink& warning) noexcept
{
    try {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("cannot read file; create it using Developer Settings");
        std::map<std::string,std::string> values;
        bool rendering = false;
        std::string line;
        while (std::getline(in,line)) {
            line=Trim(line);
            if (line.empty() || line[0]==';' || line[0]=='#') continue;
            if (line=="[Rendering]") { rendering=true; continue; }
            if (!rendering || line[0]=='[') throw std::runtime_error("expected [Rendering] section");
            const auto equal=line.find('=');
            if (equal==std::string::npos) throw std::runtime_error("expected Key=Value");
            const auto key=Trim(line.substr(0,equal));
            if (key!="LegacyOverride" && key!="ShowDeprecatedFeatures" && key!="ShowExperimentalWarnings")
                throw std::runtime_error("unknown key " + key);
            if (!values.emplace(key,Trim(line.substr(equal+1))).second)
                throw std::runtime_error("duplicate key " + key);
        }
        if (in.bad()) throw std::runtime_error("read failed");
        if (values.size()!=3) throw std::runtime_error("incomplete file; expected LegacyOverride, ShowDeprecatedFeatures, ShowExperimentalWarnings");
        FDeveloperSettings result;
        const auto& value=values.at("LegacyOverride");
        if (value=="None") result.legacyOverride=ELegacyRendererOverride::None;
        else if (value=="SoftwareRasterizer") result.legacyOverride=ELegacyRendererOverride::SoftwareRasterizer;
        else if (value=="PureGPURayTracer") result.legacyOverride=ELegacyRendererOverride::PureGPURayTracer;
        else throw std::runtime_error("LegacyOverride must be None, SoftwareRasterizer, or PureGPURayTracer");
        auto boolean=[&](const char* key) {
            if (values.at(key)=="true") return true;
            if (values.at(key)=="false") return false;
            throw std::runtime_error(std::string(key)+" must be true or false");
        };
        result.showDeprecatedFeatures=boolean("ShowDeprecatedFeatures");
        result.showExperimentalWarnings=boolean("ShowExperimentalWarnings");
        return result;
    } catch (const std::exception& e) {
        EmitDeveloperWarning(warning, std::string("DeveloperSettings.ini: ")+e.what()+"; using None/true/true defaults.");
    } catch (...) { EmitDeveloperWarning(warning,"DeveloperSettings.ini: load failed; using None/true/true defaults."); }
    return {};
}

bool FDeveloperSettings::Save(const std::filesystem::path& path,
                             const FDeveloperWarningSink& warning) const noexcept
{
    std::filesystem::path temporary;
    bool ownsTemporary=false;
    try {
        const auto name=OverrideName(legacyOverride);
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        temporary=path; temporary += ".tmp";
        // Do not overwrite an existing temporary file (another editor may be saving).
#ifdef _WIN32
        const std::string contents = std::string("[Rendering]\nLegacyOverride=") + name +
            "\nShowDeprecatedFeatures=" + (showDeprecatedFeatures?"true":"false") +
            "\nShowExperimentalWarnings=" + (showExperimentalWarnings?"true":"false") + "\n";
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("cannot create exclusive temporary save; check permissions or pending .tmp file");
        ownsTemporary=true;
        DWORD written=0;
        const bool wrote = WriteFile(file,contents.data(),static_cast<DWORD>(contents.size()),&written,nullptr) != FALSE;
        const bool flushed = FlushFileBuffers(file) != FALSE;
        const bool closed = CloseHandle(file) != FALSE;
        if (!wrote || written!=contents.size() || !flushed || !closed)
            throw std::runtime_error("temporary write/flush/close failed");
#else
        if (std::filesystem::exists(temporary)) throw std::runtime_error("temporary save path already exists; retry after checking " + temporary.string());
        std::ofstream out(temporary,std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot create temporary save");
        ownsTemporary=true;
        out << "[Rendering]\nLegacyOverride=" << name
            << "\nShowDeprecatedFeatures=" << (showDeprecatedFeatures?"true":"false")
            << "\nShowExperimentalWarnings=" << (showExperimentalWarnings?"true":"false") << "\n";
        out.flush();
        if (!out) throw std::runtime_error("temporary write failed");
        out.close();
        if (!out) throw std::runtime_error("temporary close failed");
#endif
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot replace settings file (check directory permissions)");
#else
        std::filesystem::rename(temporary,path);
#endif
        return true;
    } catch (const std::exception& e) {
        EmitDeveloperWarning(warning,std::string("DeveloperSettings.ini: save failed: ")+e.what()+"; previous file preserved.");
    } catch (...) { EmitDeveloperWarning(warning,"DeveloperSettings.ini: save failed; previous file preserved."); }
    if (ownsTemporary) { std::error_code ec; std::filesystem::remove(temporary,ec); }
    return false;
}

const FFeatureDescriptor* LegacyFeature(ELegacyRendererOverride kind)
{
    switch (kind) {
    case ELegacyRendererOverride::SoftwareRasterizer: return &FFeatureLifecycleRegistry::Require("renderer.cpu_software");
    case ELegacyRendererOverride::PureGPURayTracer: return &FFeatureLifecycleRegistry::Require("renderer.gpu_pure_raytracer");
    default: return nullptr;
    }
}
std::string DeveloperLifecycleWarning(const FFeatureDescriptor& feature)
{
    return feature.displayName + " | Deprecated " + feature.deprecatedVersion + ": " +
        feature.warning + " Replacement: " + feature.replacement;
}
FDeveloperSettingsDescription DescribeDeveloperSettings(
    const FDeveloperSettings& settings, const FRenderFeatures& features, ERayTracingBackend activeBackend)
{
    FDeveloperSettingsDescription result;
    if (settings.showDeprecatedFeatures) {
        result.legacyChoices.push_back({ELegacyRendererOverride::None,"None",nullptr,"",""});
        for (auto kind : {ELegacyRendererOverride::SoftwareRasterizer,ELegacyRendererOverride::PureGPURayTracer}) {
            const auto* feature=LegacyFeature(kind);
            result.legacyChoices.push_back({kind,feature->displayName,feature,LifecycleLabel(feature->lifecycle),Details(*feature)});
        }
    }
    if (const auto* feature=LegacyFeature(settings.legacyOverride))
        result.activeWarning=DeveloperLifecycleWarning(*feature);
    if (settings.legacyOverride==ELegacyRendererOverride::None && settings.showExperimentalWarnings && features.rayTracing &&
        (features.rayTracingBackend==ERayTracingBackend::ComputeGL43 || activeBackend==ERayTracingBackend::ComputeGL43)) {
        const auto& feature=FFeatureLifecycleRegistry::Require("renderer.rt_compute_gl43");
        result.experimentalWarning=std::string(LifecycleLabel(feature.lifecycle)) + ": " + feature.displayName + " | " + feature.purpose + " " + feature.replacement;
    }
    return result;
}

FDeveloperOverrideController::FDeveloperOverrideController(Factory factory,FDeveloperWarningSink warning)
    : factory_(std::move(factory)),warning_(std::move(warning)) {}
FDeveloperOverrideController::~FDeveloperOverrideController() { Shutdown(); }
void FDeveloperOverrideController::Shutdown() noexcept
{
    if (route_) {
        route_->Shutdown();
        const auto kind=route_->OverrideKind();
        if (kind==ELegacyRendererOverride::SoftwareRasterizer || kind==ELegacyRendererOverride::PureGPURayTracer)
            AddStats(closedStats_[kind==ELegacyRendererOverride::SoftwareRasterizer?0:1],route_->Stats());
        route_.reset();
    }
    active_=ELegacyRendererOverride::None;
}
FDeveloperRouteStats FDeveloperOverrideController::Stats(ELegacyRendererOverride kind) const noexcept
{
    if (kind!=ELegacyRendererOverride::SoftwareRasterizer && kind!=ELegacyRendererOverride::PureGPURayTracer) return {};
    auto result=closedStats_[kind==ELegacyRendererOverride::SoftwareRasterizer?0:1];
    if (route_ && route_->OverrideKind()==kind) AddStats(result,route_->Stats());
    return result;
}
bool FDeveloperOverrideController::Apply(ELegacyRendererOverride kind) noexcept
{
    if (kind==active_) return true;
    Shutdown();
    if (kind==ELegacyRendererOverride::None) return true;
    try {
        const auto* feature=LegacyFeature(kind);
        if (!feature) throw std::runtime_error("invalid legacy override");
        route_=factory_(kind);
        if (!route_) throw std::runtime_error("legacy renderer factory returned no route");
        if (route_->OverrideKind()!=kind) throw std::runtime_error("legacy renderer factory returned the wrong implementation");
        if (!route_->Init()) throw std::runtime_error("renderer initialization is not ready; check shader/link diagnostics and retry after correcting the GPU/driver problem");
        active_=kind;
        const std::size_t index=kind==ELegacyRendererOverride::SoftwareRasterizer?0:1;
        if (!warned_[index]) {
            warned_[index]=true;
            EmitDeveloperWarning(warning_,DeveloperLifecycleWarning(*feature));
        }
        return true;
    } catch (const std::exception& e) {
        EmitDeveloperWarning(warning_,std::string("Developer Settings: renderer activation failed: ")+e.what()+"; using Hardware Rasterizer.");
    } catch (...) { EmitDeveloperWarning(warning_,"Developer Settings: renderer activation failed; using Hardware Rasterizer."); }
    Shutdown();
    return false;
}
bool FDeveloperOverrideController::Render(const FDeveloperRenderFrame& frame,const std::function<bool()>& normal)
{
    if (!route_) return normal();
    try {
        if (route_->Render(frame)) return true;
        EmitDeveloperWarning(warning_,"Developer Settings: legacy render failed; disabling the override. Hardware Rasterizer resumes next frame; check GPU/target diagnostics before retrying.");
    } catch (const std::exception& e) {
        EmitDeveloperWarning(warning_,std::string("Developer Settings: legacy render failed: ")+e.what()+"; disabling the override. Hardware Rasterizer resumes next frame.");
    } catch (...) {
        EmitDeveloperWarning(warning_,"Developer Settings: legacy render failed; disabling the override. Hardware Rasterizer resumes next frame.");
    }
    // The failed route may already have drawn. Never render a second route in
    // this frame; release it now and let the next frame use normal hardware.
    Shutdown();
    return false;
}
