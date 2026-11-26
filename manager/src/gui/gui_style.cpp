#include "gui_style.h"
#include "gui_scale.h"
#include "imgui.h"
#include "eam/widgets.h"
#include <windows.h>
#include <string>

namespace eam {

void SetupModernStyle() {
    eam::ui::theme::ApplyStyle(ImGui::GetStyle());
}

// ---------------------------------------------------------------------------------------
// Display scale and fonts
// ---------------------------------------------------------------------------------------

static float g_uiScale = 1.0f;
static ImFont* g_monoFont = nullptr;
static ImFont* g_titleFont = nullptr;   // Segoe UI Semibold: the header's title and the addon names

float UiScale() { return g_uiScale; }
void SetUiScale(float scale) {
    g_uiScale = scale < 0.5f ? 0.5f : (scale > 4.0f ? 4.0f : scale);
}

void ApplyUiScale(float dpiScale) {
    SetUiScale(dpiScale);
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    SetupModernStyle();
    style.ScaleAllSizes(g_uiScale);
    // Fonts are rasterised at FontSizeBase * FontScaleDpi, so text stays sharp at any scale.
    style.FontSizeBase = kUiFontSize;
    style.FontScaleDpi = g_uiScale;
}

// ---------------------------------------------------------------------------------------
// 中文字形支持：所有字体统一加载简体中文常用字形范围。
// 插件面板与管理器共享 ImGuiContext，字形范围必须只通过 AddFont* 参数传入，
// 不得改动 ImGui 源码，否则跨 DLL 的结构布局会失配。
// ---------------------------------------------------------------------------------------
static const ImWchar* ChineseGlyphRanges() {
    return ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon();
}

static ImFont* TryAddFont(ImGuiIO& io, const std::string& path) {
    if (path.empty()) return nullptr;
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return nullptr;
    return io.Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, nullptr, ChineseGlyphRanges());
}

void LoadUiFonts(const std::string& uiFontPath, const std::string& monoFontPath) {
    ImGuiIO& io = ImGui::GetIO();

    // 简体中文常用字形约 7000 个，默认图集尺寸不足，扩大图集避免字形丢失。
    // ImGui 1.92+ 用 TexMinWidth/TexMaxWidth 取代旧的 TexDesiredWidth。
    io.Fonts->TexMinWidth = 2048;
    io.Fonts->TexMaxWidth = 8192;

    char winDir[MAX_PATH] = {};
    const std::string fontsDir = GetWindowsDirectoryA(winDir, MAX_PATH)
        ? std::string(winDir) + "\\Fonts\\" : std::string("C:\\Windows\\Fonts\\");

    // 微软雅黑包含完整简体中文；放在首位，确保界面中文正常显示。
    ImFont* ui = TryAddFont(io, fontsDir + "msyh.ttc");
    if (!ui) ui = TryAddFont(io, uiFontPath);
    for (const char* name : { "segoeui.ttf", "tahoma.ttf", "arial.ttf" }) {
        if (ui) break;
        ui = TryAddFont(io, fontsDir + name);
    }
    if (!ui) ui = io.Fonts->AddFontDefault();
    io.FontDefault = ui;

    g_titleFont = TryAddFont(io, fontsDir + "msyhbd.ttc");
    if (!g_titleFont) g_titleFont = TryAddFont(io, fontsDir + "seguisb.ttf");
    if (!g_titleFont) g_titleFont = ui;

    g_monoFont = TryAddFont(io, monoFontPath);
    for (const char* name : { "msyh.ttc", "CascadiaMono.ttf", "consola.ttf", "cour.ttf" }) {
        if (g_monoFont) break;
        g_monoFont = TryAddFont(io, fontsDir + name);
    }
}

ImFont* MonoFont() { return g_monoFont; }
ImFont* TitleFont() { return g_titleFont; }

} // namespace eam
