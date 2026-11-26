// The addon's settings panel, drawn by the manager in its window (the window's thread). It works on a copy of the settings and hands the copy
// back (Commit) when something changed. The sections start closed; the person opens what they need.
#include "addon/framegen11.h"
#include "engine/sr_engine.h"
#include "addon/state.h"
#include "addon/present_hook.h"
#include "addon/screenshot.h"
#include "imgui.h"
#include <eam/widgets.h>
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace nr {

// Wrapped tooltip for the widget just submitted, after a short hover delay.
static void Tip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    const char* hint = eam::ui::SliderHint();   // sliders add "default, double-click resets, Ctrl+scroll fine-tunes"
    ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f); ImGui::TextUnformatted(text);
    if (hint) { ImGui::Dummy(ImVec2(0, 2)); ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); ImGui::TextUnformatted(hint); ImGui::PopStyleColor(); }
    ImGui::PopTextWrapPos(); ImGui::EndTooltip();
}
// Wrapped help text in the disabled colour (ImGui::TextDisabled does not wrap, so long lines ran off the panel).
static void Note(const char* fmt, ...) {
    char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", b);
    ImGui::PopStyleColor();
}
// Every collapsible section of the panel starts closed (eam::ui::SectionHeader with no second argument); the person opens what they need.
// A titled block of the panel: some room, a thin line, the title in the small capitals of the other apps, and a little room under it.
static void Block(const char* title, bool first = false) {
    const float u = ImGui::GetFontSize();
    if (!first) { ImGui::Dummy(ImVec2(0, u * 0.7f)); ImGui::Separator(); ImGui::Dummy(ImVec2(0, u * 0.5f)); }
    eam::ui::SectionLabel(title);
    ImGui::Dummy(ImVec2(0, u * 0.35f));
}

void DrawPanel() {
    Config c; { std::lock_guard<std::mutex> lk(g_settingsMutex); c = g_config; }
    bool changed = false, createChanged = false, tapChanged = false;
    // Every slider bound to a field of the params gets that field's default (a fresh PictureParams): a tick on the groove, a ring on the
    // knob while it differs, double-click to reset. Sliders bound to anything else simply have no default.
    static const PictureParams kDefaults;
    auto SL = [&](const char* label, float* v, float lo, float hi, const char* fmt = "%.2f") {
        const char* base = (const char*)&c.p; const char* pv = (const char*)v; const float* def = nullptr;
        if (pv >= base && pv < base + sizeof(PictureParams)) def = (const float*)((const char*)&kDefaults + (pv - base));
        return eam::ui::SliderFloat(label, v, lo, hi, fmt, 0, def);
    };

    ImGui::TextWrapped("%s", Status().c_str());
    if (g_off && ImGui::Button("重试")) SwitchOn();
    auto choices = RuntimeChoices();
    const auto chosen = ChosenRuntimeFile(); int current = 0;
    for (size_t i=0;i<choices.size();++i) if (choices[i].path==chosen) current=int(i);
    if (!choices.empty() && ImGui::BeginCombo("运行时", choices[current].name.c_str())) {
        for (size_t i=0;i<choices.size();++i) if (ImGui::Selectable(choices[i].name.c_str(),current==int(i))) ChooseRuntimeFile(choices[i].path);
        ImGui::EndCombo();
    }
    if (!kAnyCardScaler) {
        const char* names[]={"自动", "默认", "E", "J", "L", "M"}; const unsigned values[]={16,0,5,10,12,13}; int p=0;
        for(int i=0;i<6;++i) if(c.dlaaPreset==values[i]) p=i;
        if(ImGui::Combo("DLSS 预设",&p,names,6)) { c.dlaaPreset=values[p]; changed=true; }
    }
    const char* motions[]={"从帧测量", "LSFG 流", "无"};
    changed |= ImGui::Combo("运动",&c.motionSource,motions,3);
    if (kScalerAddon && eam::ui::SectionHeader("超分")) {
        const ScalerView v = GetScalerView();
        const char* const U = kUpscalerName;
        if (v.starting) ImGui::TextDisabled(kXessScaler ? "正在加载 Intel 的 XeSS 运行时..." : kFsrScaler ? "正在加载 AMD 的 FSR 运行时..." : "正在加载 NVIDIA 的 DLSS 运行时...");
        else if (v.failed) ImGui::TextColored(eam::ui::theme::V(eam::ui::theme::kDanger), "%s 无法运行：%s。Lossless Scaling 的 NIS 照常运行。", U, v.error.c_str());
        else if (v.preparing) ImGui::TextColored(eam::ui::theme::V(eam::ui::theme::kWarn), "%s 正在为这种帧格式做准备（首帧可能需要一些时间）；期间由 NIS 运行。", U);
        else if (!v.blocked.empty()) {   // the NIS pass is there but the upscaler cannot take it: say why, rather than look like it runs
            ImGui::PushStyleColor(ImGuiCol_Text, eam::ui::theme::V(eam::ui::theme::kWarn));
            if (v.nisSeen) ImGui::TextWrapped("找到 NIS 通道（%ux%u -> %ux%u），但 %s 没有替换它：%s", v.inW, v.inH, v.outW, v.outH, U, v.blocked.c_str());
            else ImGui::TextWrapped("%s 没有替换 NIS：%s", U, v.blocked.c_str());
            ImGui::PopStyleColor();
        }
        else if (!v.nisSeen) ImGui::TextWrapped("正在等待 Lossless Scaling 的 NIS 通道。请将缩放类型选为 NIS，并缩放以小于屏幕的窗口运行的游戏。");
        else if (!v.ready) ImGui::TextDisabled("找到 NIS 通道（%ux%u -> %ux%u）；%s 尚未运行。", v.inW, v.inH, v.outW, v.outH, U);
        else {
            const std::string running = v.provider.empty() ? std::string(U) : std::string(U) + " " + v.provider;   // "FSR 4.1.1b"
            ImGui::TextWrapped("%s 将 %ux%u -> %ux%u 超分（x%.2f）以替代 NIS：每帧在 GPU 上耗时 %.2f 毫秒（其中运动 %.2f 毫秒），迄今 %llu 帧%s。", running.c_str(), v.inW, v.inH,
                               v.outW, v.outH, v.inW ? (float)v.outW / v.inW : 0.0f, v.gpuMs, v.motionMs, (unsigned long long)v.runs,
                               v.perFrame > 1 ? "（真实帧和生成帧均计入）" : "");
            if (v.second.valid) {
                ImGui::TextDisabled("上一秒：每秒 %.0f 幅画面（%.0f%% 为相邻帧对）；%.1f%% 重复显示，%.1f%% 的帧未移交，%.1f%% 在 GPU 上等待。",
                                    v.second.fps, v.second.closePct, v.second.repeatPct, v.second.skipPct, v.second.waitPct);
                Tip("相邻帧对：两帧相隔 6 毫秒以内，由自适应帧生成产生。重复显示：Lossless Scaling 需要画面时超分器尚未完成更新的画面，因此再次显示了之前的一帧（轻微卡顿）。"
                    "在 GPU 上等待：与此相反，Lossless Scaling 的帧在 GPU 上等待下一幅画面（见下方设置）。未移交：超分器还有两帧要处理，因此省略了此帧。"
                    "GPU 满载会使这些数值上升：留有余量的帧率上限最有帮助。");
            }
            if (g_compare.load() == 2) ImGui::TextColored(eam::ui::theme::V(eam::ui::theme::kWarn), "正在显示 Lossless Scaling 的 NIS 以作对比（前/后对比快捷键）。");
        }
        changed |= SL("锐化", &c.p.sharpen, 0.0f, 1.0f, c.p.sharpen <= 0.001f ? "关闭" : "%.2f");
        if (kFsrScaler) Tip("FSR 自带的锐化（AMD 的 RCAS），属于其超分通道的一部分；超过约 0.6 时，额外的通道带来的提升超过 RCAS 本身。Lossless Scaling 的 "
                            "NIS 也有锐化（其锐度设置），因此关闭它时 FSR 相比 NIS 可能显得更柔和。0.5 是一个不错的起点（游戏内 Ctrl+Shift+F2 / F3）。");
        else Tip("对 DLSS 画面进行对比度自适应锐化（FidelityFX CAS 公式），耗时不到一毫秒；超过约 0.6 时其效果会被放大到超过 CAS 自身的上限。DLSS 本身没有锐化，而 Lossless Scaling 的 NIS 有"
                 "（其锐度设置），因此关闭它时 DLSS 相比 NIS 可能显得更柔和。0.5 是一个不错的起点（游戏内 Ctrl+Shift+F2 / F3）。");
        // FSR 4 keeps its history its own way and takes none of FSR 3.1's tuning (nor the mask the slider widens): the slider would do nothing
        const bool fsr4 = kFsrScaler && GetScalerView().provider.rfind("4", 0) == 0;
        if (fsr4) ImGui::BeginDisabled();
        { const float off = 0.0f; changed |= eam::ui::SliderFloat("稳定性", &c.scalerStability, 0.0f, 1.0f, c.scalerStability <= 0.001f ? "关闭" : "%.2f", 0, &off); }
        if (fsr4) { ImGui::EndDisabled(); Note("FSR 4 自行处理稳定性：此滑块适用于 FSR 3.1。"); }
        { char tip[768];
          snprintf(tip, sizeof tip, "减少细线、电线和树叶上的闪烁，代价是移动物体后方出现更多拖影。游戏不会向超分器提供亚像素级摄像机位移，因此细于一像素的线条会逐帧闪烁；"
                   "提高此值时，运动检测不再将该闪烁判定为不可信任历史记录的位置，于是 %s 会在之前的帧间将其平均%s。关闭：与之前相同。尝试 0.5，然后在观察栅栏或电线时调整。"
                   "游戏自身的抗锯齿（MSAA）仍然是最初绘制细线的因素。",
                   U, kFsrScaler ? "，且 FSR 会保留更多历史记录，对着色微小变化的反应也更弱" : "");
          Tip(tip); }
        {
            if (ImGui::Checkbox("快速运动时稳定", &c.scalerFastMotion)) changed = true;
            Tip("开启（默认）：当画面快速移动（快速转动摄像机）或无法跟踪运动时，超分器的画面会更多地依赖新帧而非之前的帧。游戏不会向超分器提供亚像素级摄像机位移，"
                "因此其对先前帧的记忆在此几乎不增加细节，快速转动时会出现拖影：树叶和边缘带有柔和的重影轮廓。关闭：与之前相同。转动摄像机时切换此选项以作对比。");
            if (c.scalerFastMotion) {
                if (ImGui::SliderFloat("倾斜起点", &c.scalerLeanFrom, 0.0f, 0.5f, c.scalerLeanFrom <= 0.0f ? "自动" : "%.2f %% 的单帧宽度")) changed = true;
                Tip("倾斜开始所需的运动量（达到其两倍时完全倾斜）。自动（0）：1:1 时每帧为帧宽的 0.1 %（4K 下约 4 像素），超分时 0.05 %。"
                    "更低：移动时拖影和涂抹更少，但在 1:1 时游戏自身的锯齿边缘在运动中更明显。更高：运动中超分器的平滑效果更多，拖影也更多。走动和转身时调整此值。");
            }
        }
        {
            if (ImGui::Checkbox("按形状检测运动（测试）", &c.motionShapes)) changed = true;
            Tip("一项测试功能，默认关闭。提供给超分器的运动信息是通过将前一帧的小块区域与当前帧匹配得出的。关闭：按亮度匹配，与之前相同。"
                "开启：按形状和边缘匹配，亮度本身几乎不计入，因此闪光、法术效果、淡入淡出或掠过的阴影对它的干扰应该更小。"
                "在移动时光线变化的场景切换此选项，并告诉我们哪种看起来更稳定。");
        }
        { const float off = 0.0f; changed |= eam::ui::SliderFloat("边缘平滑", &c.scalerEdges, 0.0f, 1.0f, c.scalerEdges <= 0.001f ? "关闭" : "%.2f", 0, &off); }
        Tip("沿超分画面边缘进行抗锯齿：适用于自身没有抗锯齿的游戏（屋顶、栅栏和电线上的阶梯状锯齿）。它会找出亮度跳变的位置、边缘延伸的方向和长度，"
            "并按真实边缘应覆盖的像素比例在其上进行混合。耗时不到一毫秒。它也会让文字略微变柔和。如果游戏本身有抗锯齿（MSAA），那更好：此功能用于没有抗锯齿的游戏。");
        {   // the picture controls: on the frame before it is upscaled; Neural Rendering's own take over while it is on
            const bool nrOn = false;

            { const float d = 0.0f; changed |= eam::ui::SliderFloat("自然饱和度", &c.p.vibrance, 0.0f, 1.0f, c.p.vibrance <= 0.001f ? "关闭" : "%.2f", 0, &d); }
            Tip("提升柔和色彩的程度远高于鲜艳色彩，因此肤色和本已浓烈的颜色不会被进一步推高：自然的色彩增强。");
            { const float d = 1.0f; changed |= eam::ui::SliderFloat("饱和度", &c.p.saturation, 0.0f, 2.0f, std::abs(c.p.saturation - 1.0f) < 0.005f ? "未更改" : "%.2f", 0, &d); }
            Tip("整体增加色彩（大于 1）或减少（小于 1，0 为黑白）。");
            { const float d = 0.0f; changed |= eam::ui::SliderFloat("阴影", &c.p.shadows, -1.0f, 1.0f, std::abs(c.p.shadows) < 0.005f ? "未更改" : "%+.2f", 0, &d); }
            Tip("仅提亮（向右）或加深（向左）暗部：可以看清昏暗的室内或夜景，而不会让其余部分发灰。");
            { const float d = 0.0f; changed |= eam::ui::SliderFloat("高光", &c.p.highlights, -1.0f, 1.0f, std::abs(c.p.highlights) < 0.005f ? "未更改" : "%+.2f", 0, &d); }
            Tip("仅压暗（向左）或提亮（向右）亮部：过曝的天空或雪地能恢复细节。");
            { const float d = 0.0f; changed |= eam::ui::SliderFloat("亮度", &c.p.brightness, -0.3f, 0.3f, "%+.2f", 0, &d); }
            Tip("整体提升或降低画面，如同显示器的亮度设置。适用于太暗（或太亮）而看不清的游戏。");
            { const float d = 1.0f; changed |= eam::ui::SliderFloat("对比度", &c.p.contrast, 0.5f, 1.5f, "%.2f", 0, &d); }
            Tip("以中灰为中心，将色调拉开（大于 1）或收拢（小于 1）。");
            { const float d = 1.0f; changed |= eam::ui::SliderFloat("伽马", &c.p.gamma, 0.5f, 2.0f, "%.2f", 0, &d); }
            Tip("大于 1 时提亮中间调，黑白保持不变：暗角变得可见，同时天空不会过曝发白。"
                "小于 1 时使中间调变暗。");
        }
        {
            std::string game; { std::lock_guard<std::mutex> lk(g_settingsMutex); game = g_scalerGame; }
            if (ImGui::Checkbox("按游戏分别保存这些设置", &c.scalerPerGame)) changed = true;
            Tip("开启（默认）：锐化、稳定性、边缘平滑、色彩和色调控制、DLSS 模型以及运动设置会按游戏分别保存，并在该游戏获得焦点时恢复。"
                "此处所做的更改计入上次游玩的游戏。");
            if (c.scalerPerGame) {
                ImGui::SameLine();
                ImGui::TextDisabled(game.empty() ? "（尚未检测到游戏）" : "适用于 %s", game.c_str());
                if (!c.scalerGames.empty()) {
                    int forget = -1;
                    if (ImGui::TreeNode("拥有独立设置的游戏")) {
                        for (size_t i = 0; i < c.scalerGames.size(); ++i) {
                            const auto& [exe, p] = c.scalerGames[i];
                            ImGui::PushID(static_cast<int>(i));
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("%s：锐化 %.2f，稳定性 %.2f，边缘 %.2f", exe.c_str(), p.sharpen, p.stability, p.edges);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("忘记")) forget = static_cast<int>(i);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    if (forget >= 0) { c.scalerGames.erase(c.scalerGames.begin() + forget); changed = true; }
                }
            }
        }
        if (kFrameGen) {   // a prototype: only in a build made with it (product.h)
            if (ImGui::Checkbox("自制帧生成（原型）", &c.frameGen)) changed = true;
            Tip("在每两个真实帧之间生成一帧，使帧率翻倍，显示在 Lossless Scaling 自带的窗口中。请先关闭 Lossless Scaling 的帧生成（两者同时开启会导致双重翻倍）。"
                "原型功能：中间的帧由 AMD 的 FSR 3.1 帧生成生成，运动信息从帧中测量（无法测量时，将两帧真实帧各占一半混合）。真实帧的显示时间比不开启时晚半帧。");
            if (c.frameGen) {
                if (ImGui::Checkbox("  转身时保持角色干净", &c.frameGenGuard)) changed = true;
                Tip("开启（默认）：当两帧真实帧内容一致且几乎不动（摄像机跟随的角色、HUD）但生成的中间帧显示出其他内容时，会在该处使用真实帧本身的画面。"
                    "在快速转动摄像机时，FSR 会将扫过的场景贴到角色上（头发上出现树叶）；此选项可消除其中的大部分。关闭：保留 FSR 生成的原始帧。");
                const nr::framegen::Stats st = nr::framegen::GetStats();
                ImGui::TextDisabled("  %llu 真实帧，%llu 生成帧（其中 %llu 由 %s 生成，每帧 %.1f 毫秒）；真实帧每 %.1f 毫秒一帧", (unsigned long long)st.real,
                                    (unsigned long long)st.generated, (unsigned long long)st.byFsr, st.engine.empty() ? "FSR" : st.engine.c_str(), st.fsrMs, st.realIntervalMs);
            }
        }
        if (ImGui::Checkbox("在 GPU 上等待而非重复显示画面", &c.scalerGpuWait)) changed = true;
        Tip("当 Lossless Scaling 需要画面时超分器尚未完成新画面（两帧相隔很近，由自适应帧生成产生），原本会再次显示之前的画面：出现轻微卡顿。"
            "开启（默认）时，Lossless Scaling 的帧会在 GPU 上等待新画面，如同内置 DLSS 的游戏等待 DLSS 一样；CPU 从不等待。"
            "关闭：再次显示之前的画面。可开启和关闭此选项对比上方一行。");
        Note("使用前/后对比快捷键（对比与快捷键）进行对比：游戏过程中它会在 %s 与 Lossless Scaling 自带的 NIS 之间切换。", U);
        {   // HDR games: the frames are upscaled as light in the upscaler's HDR mode and put back in their own encoding (scaler11.h, SetEncoding)
            static const char* const encodings[] = { "自动", "SDR", "HDR" };
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::Combo("帧编码", &c.frameEncoding, encodings, 3)) changed = true;
            Tip("适用于 HDR 游戏。自动（默认）会自行判断：8 位帧为 SDR，16 位浮点帧为 HDR（scRGB），当 Windows 以 HDR 模式运行显示器时 10 位帧为 HDR（HDR10）。"
                "仅当画面发白、过暗或过亮时才手动设置为 SDR 或 HDR。技术状态会显示最终的判定结果。");
        }
    }
    if (kScalerAddon && eam::ui::SectionHeader("对比与快捷键")) {   // the upscalers: the upscaled picture or NIS's, and the sharpening
        int cm = g_compare.load() == 2 ? 1 : 0; const char* cms[] = { "已超分", "Lossless Scaling 的 NIS（之前）" };
        if (ImGui::Combo("对比视图", &cm, cms, 2)) g_compare = cm == 1 ? 2 : 0;
        Tip("已超分 = 正常状态。Lossless Scaling 的 NIS = 如同插件关闭时的效果，用于对比（超分器保持加载，因此切回是即时的）。此设置不会被保存。");
        changed |= ImGui::Checkbox("快捷键：Ctrl+Shift + 按键（在游戏获得焦点且超分器运行时生效）", &c.hotkeys);
        Tip("在游戏内即可在超分器和 NIS 之间切换，以及调整锐化。Ctrl+Shift 组合可避免与游戏自身的按键绑定冲突。");
        auto fkey = [&](const char* label, int* vk) {
            int idx = *vk - VK_F1; if (idx < 0 || idx > 11) idx = 6;
            const char* names[] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
            const ImGuiStyle& st = ImGui::GetStyle();
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("F12").x + st.FramePadding.x * 2.0f + ImGui::GetFrameHeight() + st.ItemInnerSpacing.x);
            if (ImGui::Combo(label, &idx, names, 12)) { *vk = VK_F1 + idx; changed = true; }
        };
        fkey("前 / 后", &c.keyAB); fkey("锐化 -", &c.keySharpDn); fkey("锐化 +", &c.keySharpUp); fkey("保存录制", &c.keyRecord);
        fkey("前后对比截图", &c.keyShot);
        auto fname = [](int vk) { static char b[4][8]; static int n = 0; char* o = b[n++ & 3]; snprintf(o, 8, "F%d", vk - VK_F1 + 1); return (const char*)o; };
        Note("当前：Ctrl+Shift+%s 前/后  |  %s / %s 锐化 - / +  |  %s 保存录制  |  %s 前后对比截图  （%s）", fname(c.keyAB),
             fname(c.keySharpDn), fname(c.keySharpUp), fname(c.keyRecord), fname(c.keyShot), c.hotkeys ? "快捷键已开启" : "快捷键已关闭：请勾选上方复选框");
        if (ImGui::SmallButton("保存前后对比截图")) RequestPair();
        Tip("同一时刻的两张 PNG：超分画面和 Lossless Scaling 的 NIS（截取时屏幕上会显示一帧 NIS）。"
            "Windows 自带的截图显示的是游戏窗口，而非 Lossless Scaling 的画面。HDR 游戏以 SDR 视图保存。");
        ImGui::SameLine();
        if (ImGui::SmallButton("打开截图文件夹")) ShellExecuteW(nullptr, L"open", screenshot::Folder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        { bool ok; const std::string msg = screenshot::LastResult(ok);
          if (!msg.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ok ? eam::ui::theme::V(eam::ui::theme::kAccent) : eam::ui::theme::V(eam::ui::theme::kWarn)); ImGui::TextWrapped("%s", msg.c_str()); ImGui::PopStyleColor(); } }
    }
    if (eam::ui::SectionHeader("录制（用于错误报告和测试）")) {
        Note(kScalerAddon ? "无损保留超分器接收到的最近几秒帧（即送入超分器时的 Lossless Scaling 画面，包含色彩和色调设置），可保存为 .lsrec 文件。"
                            "随错误报告发送该文件，即可在另一台电脑上复现并修复问题。"
                          : "无损保留神经渲染接收到的最近几秒帧（即在模型或其他任何处理更改之前的游戏原始帧），可保存为 .lsrec 文件。"
                            "随错误报告发送该文件，即可在另一台电脑上复现并修复问题。");
        if (ImGui::Checkbox("保留最近几秒随时可保存", &c.recordOn)) {
            changed = true;
            g_recorder.Configure(c.recordOn, c.recordSeconds, static_cast<uint32_t>(c.recordBudgetMb));   // off: its memory goes now, not at the next frame
        }
        Tip("关闭（默认）不产生任何开销。开启：每一帧都会从显卡复制出来，并由几个后台线程压缩，这会占用一些处理器时间和下方所列内存。"
            "没有任何操作会等待它：当所有复制线程都忙碌时到来的帧会被省略。");
        if (!c.recordOn) ImGui::BeginDisabled();
        if (kFrameGen) {
            if (ImGui::Checkbox("录制所显示的内容（使用自制帧生成时）", &c.recordShown)) changed = true;
            Tip("开启自制帧生成时：录制送入屏幕的帧，包括生成的中间帧和真实帧（经过超分后、为屏幕尺寸），并分别标记类型，而非录制送入超分器的帧。"
                "用于展示帧生成和超分器在运动中的效果。这些帧较大（4K），因此可保存的秒数较少：可提高内存分配，或保持 2 到 3 秒。");
        }
        { const float d = 5.0f; changed |= eam::ui::SliderFloat("保留秒数", &c.recordSeconds, 1.0f, 30.0f, "%.0f s", 0, &d); }
        Tip("保存的录制可回溯的时间长度。秒数越多，占用内存越多。");
        { float mb = static_cast<float>(c.recordBudgetMb); const float d = 3072.0f;
          if (eam::ui::SliderFloat("内存分配", &mb, 512.0f, 16384.0f, "%.0f MB", 0, &d)) { c.recordBudgetMb = static_cast<int>(mb / 256.0f + 0.5f) * 256; changed = true; } }
        Tip("保留的帧可占用的最大内存；超出后最先丢弃最旧的帧，因此保留的秒数会更少。一帧 1080p 压缩后约占 3 到 5 MB，"
            "因此每秒 120 帧录制 5 秒约为 2 到 3 GB。");
        const Recorder::Status rs = g_recorder.GetStatus();
        if (rs.on && rs.frames) ImGui::TextDisabled("已保留 %.1f 秒：%u 帧，%ux%u，%.0f MB%s", rs.seconds, rs.frames, rs.w, rs.h, rs.bytes / 1048576.0,
                                                   rs.missed ? "" : "，无遗漏");
        else if (rs.on) ImGui::TextDisabled("正在等待帧（游戏必须正在运行且已缩放）。");
        if (rs.on && rs.missed) ImGui::TextDisabled("迄今已省略 %llu 帧（复制线程全部忙碌）。", (unsigned long long)rs.missed);
        const bool saving = rs.saving;
        if (saving) ImGui::BeginDisabled();
        if (eam::ui::Button(saving ? "保存中..." : "保存最近几秒", eam::ui::icons::kCheck, eam::ui::ButtonKind::Primary)) SaveRecording();
        if (saving) ImGui::EndDisabled();
        Tip("在游戏内按 Ctrl+Shift + 保存录制键（见对比与快捷键）可执行相同操作。文件以游戏名称和时间命名。");
        if (!c.recordOn) ImGui::EndDisabled();
        const std::wstring folder = RecordFolder();
        const int size = WideCharToMultiByte(CP_UTF8, 0, folder.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string folderText(size > 0 ? size - 1 : 0, '\0');
        if (size > 1) WideCharToMultiByte(CP_UTF8, 0, folder.c_str(), -1, folderText.data(), size, nullptr, nullptr);
        ImGui::TextWrapped("文件夹：%s", folderText.c_str());
        if (ImGui::SmallButton("打开文件夹##rec")) { CreateDirectoryW(folder.c_str(), nullptr); ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
        bool ok; const std::string result = g_recorder.LastResult(ok);
        if (!result.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ok ? eam::ui::theme::V(eam::ui::theme::kAccent) : eam::ui::theme::V(eam::ui::theme::kWarn)); ImGui::TextWrapped("%s", result.c_str()); ImGui::PopStyleColor(); }
    }
    if (changed) Commit(c, false, createChanged);
}
} // namespace nr
