#include "tab_settings.h"
#include "../gui_scale.h"
#include "../../addon/addon_manager.h"
#include "../../config/config_manager.h"
#include "../../config/settings_backup.h"
#include "../../diag/diagnostics.h"
#include "../../host/gpu_stats.h"
#include "../../log/logger.h"
#include "../../../sdk/include/eam/version.h"
#include "../widgets/file_dialog.h"
#include "../widgets/toast.h"
#include "../widgets/tooltip.h"
#include "../widgets/update_notice.h"
#include "../gui_manager.h"
#include "imgui.h"
#include "eam/widgets.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eam {

namespace fs = std::filesystem;

static void OpenPath(const std::wstring& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

static std::wstring ExeDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return fs::path(buffer).parent_path().wstring();
}

static std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    std::string s((size_t)WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], (int)s.size(), nullptr, nullptr);
    return s;
}

static std::string WindowsVersion() {
    using RtlGetVersion_t = LONG (WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{ sizeof v };
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
        if (auto f = (RtlGetVersion_t)GetProcAddress(nt, "RtlGetVersion")) f(&v);
    char b[64]; snprintf(b, sizeof b, "Windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    return b;
}

// What goes into info.txt of the diagnostics zip: versions, the machine, and every addon's state. No personal data beyond the folder
// path of the install.
static std::string BuildDiagnosticsSummary(AddonManager* manager) {
    std::ostringstream o;
    o << EAM_PRODUCT_NAME << " diagnostics\n";
    o << "manager: " << EAM_VERSION_STRING << " (built " << __DATE__ << " " << __TIME__ << ")\n";
    o << "system: " << WindowsVersion() << "\n";
    o << "install folder: " << Utf8(ExeDir()) << "\n";
    GpuStats::Instance().SampleOnce();
    const GpuStats::Snapshot g = GpuStats::Instance().Get();
    if (g.ok) o << "gpu: " << g.name << ", driver " << g.driver << ", " << g.vramTotalMB << " MB, power limit " << (int)g.powerLimitW << " W\n";
    else o << "gpu: not available (" << g.why << ")\n";
    o << "\naddons:\n";
    if (manager) for (const auto& a : manager->GetAddons()) {
        o << "  " << a.GetDisplayName() << " [" << a.id << "] v" << a.GetDisplayVersion()
          << "  enabled=" << (a.enabled ? "yes" : "no") << "  loaded=" << (a.IsLoaded() ? "yes" : "no") << "  faulted=" << (a.faulted ? "yes" : "no")
          << "  restart-needed=" << (a.RequiresRestart() ? "yes" : "no");
        if (!a.errorMessage.empty()) o << "  error: " << a.errorMessage;
        o << "\n";
    }
    auto& cfg = ConfigManager::Instance();
    o << "\nmanager settings: log_level=" << cfg.GlobalGetOr<int>(nullptr, "log_level", 2)
      << " auto_load=" << cfg.GlobalGetOr<bool>(nullptr, "auto_load", true) << " interface_size=" << cfg.GlobalGetOr<int>("ui", "scale_percent", 100) << "%\n";
    return o.str();
}

static std::wstring DesktopDir() {
    PWSTR p = nullptr; std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p)) && p) { out = p; CoTaskMemFree(p); }
    if (out.empty()) out = ExeDir();
    return out;
}

static std::wstring s_importPath;
static std::string s_importText;   // a settings file waiting for confirmation
static int s_importAddons = 0;
static bool s_askImport = false;

// Every option here takes effect as soon as it is changed and is saved at once.
void RenderTabSettings(AddonManager* manager) {
    auto& config = ConfigManager::Instance();
    ImGui::Dummy(ImVec2(0, S(4)));

    // ---- Updates
    widgets::RenderUpdateSettings(updates::Instance());
    ImGui::Dummy(ImVec2(0, S(16)));

    // ---- Backup and restore
    eam::ui::SectionLabel("备份与恢复（保存和加载你的设置）");
    ImGui::Dummy(ImVec2(0, S(2)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("将此管理器及所有插件的设置保存到一个文件中，以便日后或在另一台电脑上重新加载。插件自身保存的外观也保留在同一文件中。");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(4)));
    if (eam::ui::Button("保存设置到文件", eam::ui::icons::kSave, eam::ui::ButtonKind::Primary)) {
        std::wstring path;
        SYSTEMTIME t; GetLocalTime(&t);
        wchar_t name[64]; swprintf(name, 64, L"LSAddonManager-settings-%04d%02d%02d.json", t.wYear, t.wMonth, t.wDay);
        if (widgets::PickSaveFile(L"保存设置", name, L"设置备份", L"*.json", path)) {
            std::ofstream out(fs::path(path), std::ios::binary);
            out << MakeSettingsBackupText(config.Snapshot(), EAM_VERSION_STRING);
            out.close();
            widgets::ToastShow(out ? "设置已保存。" : "无法写入该文件。", out ? widgets::ToastType::Success : widgets::ToastType::Error);
        }
    }
    widgets::Tip("将所有插件和此管理器的设置保存到一个文件中，便于保留或迁移到另一台电脑。");
    ImGui::SameLine();
    if (eam::ui::Button("从文件加载设置", eam::ui::icons::kDownload)) {
        std::wstring path;
        if (widgets::PickOpenFile(L"加载设置", L"设置备份", L"*.json", path)) {
            std::ifstream in(fs::path(path), std::ios::binary);
            std::stringstream ss; ss << in.rdbuf();
            const std::string text = ss.str();
            const BackupParse p = ParseSettingsBackup(text);
            if (!p.ok) widgets::ToastShow(p.message, widgets::ToastType::Error, 6.0f);
            else { s_importPath = path; s_importText = text; s_importAddons = p.addonCount; s_askImport = true; }
        }
    }
    widgets::Tip("用之前保存的文件中的设置替换当前设置。会先询问你，并保留当前设置。在 Lossless Scaling 重启后生效。");
    if (config.RestartPending()) {
        ImGui::PushStyleColor(ImGuiCol_Text, eam::ui::theme::V(eam::ui::theme::kWarn));
        ImGui::TextWrapped("设置已从文件加载。重启 Lossless Scaling 后才会使用这些设置。在此之前，你在此处或在插件中所做的更改不会被保存，因此无法撤销已加载的设置。");
        ImGui::PopStyleColor();
    }
    if (s_askImport) { ImGui::OpenPopup("加载设置"); s_askImport = false; }
    if (ImGui::BeginPopupModal("加载设置", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextWrapped("要用此文件中的设置替换你的当前设置吗？");
        ImGui::TextDisabled("%s", Utf8(fs::path(s_importPath).filename().wstring()).c_str());
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::TextWrapped("它包含 %d 个插件的设置%s。你的当前设置会先保存到配置文件旁，新设置在 Lossless Scaling 重启后生效。",
                           s_importAddons, "");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(6)));
        if (eam::ui::Button("加载", eam::ui::icons::kCheck, eam::ui::ButtonKind::Primary)) {
            const ImportResult r = ImportSettings(s_importText, config.Snapshot(), fs::path(ExeDir()) / L"backups", [&](const nlohmann::json& j) { config.Replace(j, /*untilRestart=*/true); });
            widgets::ToastShow(r.message, r.ok ? widgets::ToastType::Success : widgets::ToastType::Error, 9.0f);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (eam::ui::Button("取消", eam::ui::icons::kClose)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Interface
    ImGui::Dummy(ImVec2(0, S(16)));
    eam::ui::SectionLabel("界面");
    ImGui::Dummy(ImVec2(0, S(2)));
    {
        int pct = config.GlobalGetOr<int>("ui", "scale_percent", 100);
        const int def = 100;
        ImGui::SetNextItemWidth(S(320));
        if (eam::ui::SliderInt("界面大小", &pct, 75, 200, "%d%%", 0, &def)) {
            config.GlobalSet("ui", "scale_percent", pct);
            config.Save();
            GuiManager::RequestUserScale();   // applied at the start of the next frame
        }
        widgets::Tip("在 Windows 自身的显示缩放基础上，让此窗口中的所有内容变小或变大。100% 是 Windows 建议的大小。");
    }
    bool openOnStart = config.GlobalGetOr<bool>("ui", "open_on_start", true);
    if (ImGui::Checkbox("启动 Lossless Scaling 时打开此窗口", &openOnStart)) { config.GlobalSet("ui", "open_on_start", openOnStart); config.Save(); }
    widgets::Tip("关闭 = 管理器启动时隐藏在通知区域（托盘）中。无论哪种情况，你的插件都会加载并运行。点击 X 关闭此窗口只是将其隐藏。");
    bool hotkeyOn = config.GlobalGetOr<bool>("ui", "hotkey_enabled", true);
    if (ImGui::Checkbox("打开 / 关闭快捷键：Ctrl+Shift +", &hotkeyOn)) { config.GlobalSet("ui", "hotkey_enabled", hotkeyOn); config.Save(); GuiManager::ApplyHotkey(); }
    widgets::Tip("在 Windows 中任何位置都有效，包括游戏获得焦点时。如果其他程序已占用该组合键，下方提示会说明；请选择其他按键。");
    ImGui::SameLine();
    {
        int idx = config.GlobalGetOr<int>("ui", "hotkey_vk", VK_F12) - VK_F1;
        if (idx < 0 || idx > 11) idx = 11;
        const char* names[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
        const ImGuiStyle& st = ImGui::GetStyle();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("F12").x + st.FramePadding.x * 2.0f + ImGui::GetFrameHeight() + st.ItemInnerSpacing.x);
        if (ImGui::Combo("##mgrhotkey", &idx, names, 12)) { config.GlobalSet("ui", "hotkey_vk", VK_F1 + idx); config.Save(); GuiManager::ApplyHotkey(); }
    }
    ImGui::TextDisabled("快捷键：%s", GuiManager::HotkeyStatus());

    // ---- Addons
    ImGui::Dummy(ImVec2(0, S(16)));
    eam::ui::SectionLabel("插件");
    ImGui::Dummy(ImVec2(0, S(2)));
    bool autoLoad = config.GlobalGetOr<bool>(nullptr, "auto_load", true);
    if (ImGui::Checkbox("启动 Lossless Scaling 时加载已启用的插件", &autoLoad)) { config.GlobalSet(nullptr, "auto_load", autoLoad); config.Save(); }
    widgets::Tip("关闭 = 启动时什么也不加载，然后对你想要的插件点击“立即加载”。");

    // ---- Logging and support
    ImGui::Dummy(ImVec2(0, S(16)));
    eam::ui::SectionLabel("日志与支持");
    ImGui::Dummy(ImVec2(0, S(2)));
    {
        int logLevel = config.GlobalGetOr<int>(nullptr, "log_level", 2);
        const char* logItems[] = { "跟踪", "调试", "信息", "警告", "错误" };
        ImGui::SetNextItemWidth(S(320));
        if (ImGui::Combo("日志详细程度", &logLevel, logItems, 5)) { config.GlobalSet(nullptr, "log_level", logLevel); config.Save(); Logger::Instance().SetMinLevel(static_cast<LogLevel>(logLevel)); }
        widgets::Tip("低于此级别的消息在记录前就会被丢弃，不会出现在“日志”选项卡和 LSAddonManager.log 中。“信息”是合适的默认值；“调试”和“跟踪”用于排查问题。");
    }
    if (eam::ui::Button("创建诊断文件", eam::ui::icons::kPackage, eam::ui::ButtonKind::Primary)) {
        const DiagResult d = CreateDiagnosticsZip(fs::path(ExeDir()), BuildDiagnosticsSummary(manager), fs::path(DesktopDir()));
        widgets::ToastShow(d.message, d.ok ? widgets::ToastType::Success : widgets::ToastType::Error, 9.0f);
        if (d.ok) ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + d.zip.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    }
    widgets::Tip("将日志、设置和简短的系统摘要打包为一个 zip 文件放在桌面上，用于错误报告。不会上传任何内容：由你决定把文件交给谁。");
    ImGui::SameLine();
    if (eam::ui::Button("日志文件夹", eam::ui::icons::kFolder)) OpenPath(ExeDir() + L"\\logs");
    ImGui::SameLine();
    if (eam::ui::Button("插件文件夹", eam::ui::icons::kFolder)) OpenPath(ExeDir() + L"\\addons");
    ImGui::SameLine();
    if (eam::ui::Button("config.json", eam::ui::icons::kFile)) OpenPath(ExeDir() + L"\\addons\\config.json");

    ImGui::Dummy(ImVec2(0, S(14)));
    ImGui::TextDisabled("更改会在你做出时自动保存。");
}

} // namespace eam
