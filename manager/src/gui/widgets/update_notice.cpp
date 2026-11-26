#include "update_notice.h"
#include "../gui_scale.h"
#include "../../config/config_manager.h"
#include "imgui.h"
#include "eam/widgets.h"
#include <windows.h>
#include <shellapi.h>

namespace eam::widgets {
namespace {

float drawnBannerHeight = 0;

void SavePolicy(updates::Checker& checker, bool enabled) {
    auto& config = ConfigManager::Instance();
    if (config.RestartPending()) return;
    config.GlobalSet("updates", "check", enabled);
    config.Save();
    checker.SetPolicy(enabled ? updates::Policy::Enabled : updates::Policy::Disabled);
}

} // namespace

void InitializeUpdateCheck(updates::Checker& checker) {
    const auto saved = ConfigManager::Instance().GlobalGet("updates", "check");
    checker.SetPolicy(!saved.is_boolean() ? updates::Policy::Ask :
                      saved.get<bool>() ? updates::Policy::Enabled : updates::Policy::Disabled);
}

void RenderUpdateConsent(updates::Checker& checker) {
    if (checker.GetPolicy() != updates::Policy::Ask || ConfigManager::Instance().RestartPending()) return;
    constexpr const char* title = "自动更新检查";
    if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(420));
        ImGui::TextWrapped("是否自动检查 GitHub 更新？");
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::TextWrapped("如果不启用，应用将完全离线运行。");
        ImGui::TextWrapped("你可以稍后在设置中更改此选项。");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(8)));
        if (ImGui::Button("是，启用")) { SavePolicy(checker, true); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("否，保持离线")) { SavePolicy(checker, false); ImGui::CloseCurrentPopup(); }
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
}

void RenderUpdateSettings(updates::Checker& checker) {
    eam::ui::SectionLabel("更新");
    bool enabled = checker.GetPolicy() == updates::Policy::Enabled;
    ImGui::BeginDisabled(ConfigManager::Instance().RestartPending());
    if (ImGui::Checkbox("自动检查 GitHub 更新", &enabled)) SavePolicy(checker, enabled);
    ImGui::EndDisabled();
    ImGui::TextDisabled("启动时检查。点击下载将在浏览器中打开发布页面。");
    if (checker.Get().status == updates::Status::Checking) ImGui::TextDisabled("正在检查更新...");
    else if (checker.Get().status == updates::Status::Failed) ImGui::TextDisabled("无法检查更新。下次启动时将重试。");
}

float UpdateBannerHeight(const updates::Result& result) {
    return result.status == updates::Status::Available ? ImGui::GetFrameHeight() + S(12) : 0;
}

float DrawnUpdateBannerHeight() { return drawnBannerHeight; }

void RenderUpdateBanner(const updates::Result& result) {
    drawnBannerHeight = UpdateBannerHeight(result);
    if (!drawnBannerHeight) return;
    const auto origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float inset = S(8);
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + drawnBannerHeight),
        eam::ui::theme::U(eam::ui::theme::kSelection), S(3));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + inset, origin.y + S(6)));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(updates::BannerText(result).c_str());
    const float buttonWidth = ImGui::CalcTextSize("下载").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - buttonWidth - inset, origin.y + S(6)));
    if (ImGui::Button("下载")) ShellExecuteW(nullptr, L"open", updates::kReleasesUrl, nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, drawnBannerHeight));
}

} // namespace eam::widgets
