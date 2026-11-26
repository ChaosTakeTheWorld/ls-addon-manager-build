#include "tab_about.h"
#include "../gui_scale.h"
#include "../gui_style.h"
#include "../../../sdk/include/eam/version.h"
#include "imgui.h"
#include "eam/widgets.h"
#include <windows.h>
#include <shellapi.h>

namespace eam {

static void OpenUrl(const char* url) { ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL); }

static void CenteredText(const char* text, bool muted = false) {
    const float w = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - w) * 0.5f + ImGui::GetCursorPosX());
    if (muted) ImGui::TextDisabled("%s", text); else ImGui::TextUnformatted(text);
}

void RenderTabAbout() {
    ImGui::Dummy(ImVec2(0, S(12)));

    // logo: the Echo mark on a dark tile, drawn as vectors (crisp at any scale)
    {
        const float s = ImGui::GetFontSize() * 4.2f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - s) * 0.5f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), eam::ui::theme::U(eam::ui::theme::kPanel), s * 0.22f);
        dl->AddRect(p, ImVec2(p.x + s, p.y + s), eam::ui::theme::U(eam::ui::theme::kAccentDim), s * 0.22f, 0, 1.5f);
        const float m = s * 0.06f;
        const float in = s - 2 * m;
        eam::ui::svg::Draw(dl, eam::ui::icons::kEchoBack, ImVec2(p.x + m, p.y + m), in, eam::ui::theme::U(eam::ui::theme::kAccentDim), 1.8f);
        eam::ui::svg::Draw(dl, eam::ui::icons::kEchoMid, ImVec2(p.x + m, p.y + m), in, eam::ui::theme::U(eam::ui::theme::kAccent), 1.8f);
        eam::ui::svg::Draw(dl, eam::ui::icons::kEchoFront, ImVec2(p.x + m, p.y + m), in, eam::ui::theme::U(eam::ui::theme::kAccentHot), 1.8f, eam::ui::theme::U(eam::ui::theme::kAccent));
        ImGui::Dummy(ImVec2(s, s));
        ImGui::Dummy(ImVec2(0, S(6)));
    }

    // title block
    CenteredText(EAM_PRODUCT_NAME);
    ImGui::Dummy(ImVec2(0, S(6)));
    CenteredText("Lossless Scaling 扩展版：神经渲染、DLSS 与 FSR 超分，以及插件带来的一切。", true);

    // what comes with it: the three plugins and what is built into the manager
    ImGui::Dummy(ImVec2(0, S(18)));
    eam::ui::SectionLabel("包含内容");
    {
        struct Item { const char* icon; const char* name; const char* what; };
        const Item items[] = {
            { eam::ui::icons::kSliders, "DLSS 神经渲染",
              "在 LSFG 之前，对原始真实捕获画面使用 NVIDIA 光流与神经渲染。需要 WGC 和用户提供的模型。" },
            { eam::ui::icons::kUpscale, "DLSS 超分器",
              "用 NVIDIA DLSS 取代 Lossless Scaling 的 NIS 缩放器：为原本不支持的游戏提供真正的超分（或按屏幕原生尺寸使用 DLAA），"
              "并根据画面帧测量运动。需要 NVIDIA RTX。" },
            { eam::ui::icons::kUpscaleFast, "FSR 超分器",
              "在同一位置支持 AMD FSR（3.1 或 FSR 4），适用于任何 DirectX 12 显卡。两种超分器同样支持 4:3 游戏，按游戏分别保存设置，"
              "并为没有抗锯齿的老游戏提供“稳定性”和“边缘平滑”选项。" },
            { eam::ui::icons::kSliders, "管理器内置功能",
              "ReShade 输入透传、窗口化游戏与第二显示器、指出帧率瓶颈的“性能”选项卡、托盘图标与快捷键，"
              "以及用于安装、修复和卸载的本地安装程序。" },
        };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float icon = S(30.0f);
        for (const Item& it : items) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(at, ImVec2(at.x + icon, at.y + icon), eam::ui::theme::U(eam::ui::theme::kPanelAlt), S(4.0f));
            dl->AddRect(at, ImVec2(at.x + icon, at.y + icon), eam::ui::theme::U(eam::ui::theme::kAccentDim), S(4.0f));
            eam::ui::svg::Draw(dl, it.icon, ImVec2(at.x + icon * 0.19f, at.y + icon * 0.19f), icon * 0.62f, eam::ui::theme::U(eam::ui::theme::kAccent), 1.7f);
            ImGui::Dummy(ImVec2(icon, icon));
            ImGui::SameLine(0, S(12));
            ImGui::BeginGroup();
            if (ImFont* title = TitleFont()) ImGui::PushFont(title, 0.0f);
            ImGui::TextUnformatted(it.name);
            if (TitleFont()) ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s", it.what);
            ImGui::PopStyleColor();
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(0, S(6)));
        }
    }

    ImGui::Dummy(ImVec2(0, S(14)));
    eam::ui::SectionLabel("使用须知");
    ImGui::TextWrapped("关闭了此窗口？它仍在通知区域（托盘）中运行。点击图标，或使用在“设置”中设定的快捷键，"
                       "即可再次打开。");
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::TextWrapped("按住 Ctrl 并在任意滑块上滚动可微调数值；双击滑块可将其恢复为默认值。");

    ImGui::Dummy(ImVec2(0, S(14)));
    eam::ui::SectionLabel("致谢");
    ImGui::TextWrapped("本项目最初是 FrankBarretta 的 LosslessProxy（MIT），我们深表感谢：带插件的代理 Lossless.dll 这一构想、"
                       "插件接口（这也是 LosslessProxy 的插件仍能在此加载的原因），以及 ReShade 和窗口化功能——"
                       "它们最初是对方的插件，如今已内置。此后代码已被重写。");
    ImGui::Dummy(ImVec2(0, S(4)));
    if (eam::ui::Button("原始项目", eam::ui::icons::kExternal, eam::ui::ButtonKind::Flat)) OpenUrl("https://github.com/FrankBarretta/LosslessProxy");
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("NVIDIA DLSS：独立的 DLSS 超分器包含 NVIDIA 的 NGX SDK 和 DLSS 运行时，遵循 "
                       "NVIDIA RTX SDK 许可协议（其文件夹中的 NVIDIA-LICENSE.txt）。NVIDIA、NVIDIA 标志和 DLSS 是 NVIDIA "
                       "Corporation 的商标。本项目与 NVIDIA 无关联，也未获其背书。");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("AMD FidelityFX：FSR 超分器包含 AMD 的 FidelityFX 运行时（MIT，其文件夹中的 AMD-FidelityFX-LICENSE.txt）。AMD、"
                       "FidelityFX 和 FSR 是 Advanced Micro Devices, Inc. 的商标；本项目与 AMD 无关联，也未获其背书。");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::TextWrapped("神经渲染：GPL-3.0-only。相应的源代码、许可协议和第三方归属声明均包含在其文件夹中。NVIDIA 模型由用户提供。");
    ImGui::TextDisabled("此外：Dear ImGui 和 nlohmann/json（MIT）、stb_image（公有领域）、MinHook（BSD，用于窗口化模式）。");
    ImGui::TextDisabled("图标形状参照 Lucide 图标集（ISC）绘制。");
    ImGui::TextDisabled("Lossless Scaling 归其作者所有；这是一款非官方的附加组件。");

    ImGui::Dummy(ImVec2(0, S(14)));
    eam::ui::SectionLabel("面向插件开发者");
    ImGui::TextDisabled("包含 SDK 头文件（addon_sdk.h）。widgets.h 让插件拥有相同外观：调色板、");
    ImGui::TextDisabled("带默认值的滑块、分节标题、按钮和 SVG 图标。");
}

} // namespace eam
