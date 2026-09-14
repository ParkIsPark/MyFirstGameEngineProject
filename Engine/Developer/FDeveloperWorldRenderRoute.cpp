#include "FDeveloperWorldRenderRoute.h"
#include "../Render/FDeprecatedWorldRenderExecutor.h"

namespace {
class FDeveloperWorldRenderRoute final : public IDeveloperRenderRoute
{
public:
    explicit FDeveloperWorldRenderRoute(std::unique_ptr<IDeprecatedWorldRenderExecutor> executor)
        : kind_(executor->OverrideKind()), renderer_(std::move(executor)) {}
    ELegacyRendererOverride OverrideKind() const noexcept override { return kind_; }
    void Init() override { renderer_.Init(); }
    void Shutdown() noexcept override { renderer_.Shutdown(); }
    bool Render(const FDeveloperRenderFrame& frame) override
    {
        if (!frame.world || !frame.camera || !frame.target || !frame.features || !frame.quality || !frame.backend) return false;
        return renderer_.Render(*frame.world,*frame.camera,*frame.target,*frame.features,
                                *frame.quality,*frame.backend,frame.contextGeneration);
    }
private:
    const ELegacyRendererOverride kind_;
    UWorldRenderer renderer_;
};
}
std::unique_ptr<IDeveloperRenderRoute> CreateDeveloperWorldRenderRoute(ELegacyRendererOverride kind)
{
    auto executor=CreateDeprecatedWorldRenderExecutor(kind);
    if (!executor) return nullptr;
    return std::make_unique<FDeveloperWorldRenderRoute>(std::move(executor));
}
