#include "DeveloperSettingsPanel.h"
#include "imgui.h"

bool DrawDeveloperSettingsPanel(bool& open,FDeveloperSettings& settings,
                               const FDeveloperSettingsDescription& description)
{
    bool changed=false;
    ImGui::SetNextWindowSize(ImVec2(620,400),ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Developer Settings",&open)) {
        changed |= ImGui::Checkbox("Show Deprecated Features",&settings.showDeprecatedFeatures);
        changed |= ImGui::Checkbox("Show Experimental Warnings",&settings.showExperimentalWarnings);
        if (settings.showDeprecatedFeatures) {
            ImGui::SeparatorText("Legacy Renderers");
            for (const auto& row : description.legacyChoices) {
                if (ImGui::RadioButton(row.label.c_str(),settings.legacyOverride==row.overrideKind)) {
                    settings.legacyOverride=row.overrideKind; changed=true;
                }
                if (row.feature) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f,0.58f,0.16f,1.0f),"%s",row.badge.c_str());
                    ImGui::TextWrapped("%s",row.details.c_str());
                }
            }
        }
    }
    ImGui::End();
    return changed;
}
void DrawDeveloperLifecycleBadges(const FDeveloperSettingsDescription& description)
{
    const ImVec4 orange(1.0f,0.58f,0.16f,1.0f);
    if (!description.activeWarning.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,orange);
        ImGui::TextWrapped("%s",description.activeWarning.c_str());
        ImGui::PopStyleColor();
    }
    if (!description.experimentalWarning.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,orange);
        ImGui::TextWrapped("%s",description.experimentalWarning.c_str());
        ImGui::PopStyleColor();
    }
}
