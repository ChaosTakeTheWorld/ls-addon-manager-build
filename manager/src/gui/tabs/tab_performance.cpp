#include "tab_performance.h"
#include "../gui_scale.h"
#include "../widgets/empty_state.h"
#include "../widgets/tooltip.h"
#include "../../host/metrics.h"
#include "../../host/gpu_stats.h"
#include "imgui.h"
#include "eam/widgets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace eam {

namespace {

constexpr double kGraphSeconds = 20.0;   // the length of the graphs
constexpr double kStatSeconds = 10.0;    // what the tiles average over

using eam::ui::theme::U;
using eam::ui::theme::V;

struct Stats { float avg = 0, mn = 0, mx = 0, p50 = 0, p95 = 0, p99 = 0; int n = 0; };

Stats Summarise(const Metrics::Series& s, double now, double seconds) {
    Stats st;
    std::vector<float> v;
    for (const auto& x : s.samples) if (x.t >= now - seconds) v.push_back(x.v);
    st.n = (int)v.size();
    if (v.empty()) return st;
    double sum = 0; st.mn = st.mx = v[0];
    for (float x : v) { sum += x; st.mn = (std::min)(st.mn, x); st.mx = (std::max)(st.mx, x); }
    st.avg = (float)(sum / v.size());
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) { return v[(size_t)((std::min)((double)v.size() - 1, std::floor(p * (v.size() - 1) + 0.5)))]; };
    st.p50 = pct(0.50); st.p95 = pct(0.95); st.p99 = pct(0.99);
    return st;
}

std::vector<ImVec2> ToPoints(const Metrics::Series& s, double now, double seconds) {
    std::vector<ImVec2> pts;
    pts.reserve(s.samples.size());
    for (const auto& x : s.samples) {
        const double t = 1.0 - (now - x.t) / seconds;
        if (t >= 0.0 && t <= 1.0) pts.push_back(ImVec2((float)t, x.v));
    }
    return pts;
}

// A rounded tile with a caption, a big number and a small line under it.
void Tile(const char* id, float width, const char* caption, const char* big, const char* sub, ImU32 bigColour) {
    const float h = ImGui::GetFontSize() * 5.4f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::InvisibleButton("##tile", ImVec2(width, h));
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), U(eam::ui::theme::kPanel), 4.0f);
    dl->AddRect(p, ImVec2(p.x + width, p.y + h), U(eam::ui::theme::kBorder), 4.0f, 0, 1.0f);
    const float pad = ImGui::GetFontSize() * 0.7f;
    dl->AddText(ImVec2(p.x + pad, p.y + pad * 0.8f), U(eam::ui::theme::kAccentDim), caption);
    ImFont* font = ImGui::GetFont();
    dl->AddText(font, ImGui::GetFontSize() * 1.9f, ImVec2(p.x + pad, p.y + pad * 0.8f + ImGui::GetFontSize() * 1.15f), bigColour, big);
    dl->AddText(ImVec2(p.x + pad, p.y + h - pad - ImGui::GetTextLineHeight() * 0.9f), U(eam::ui::theme::kMuted), sub);
}

// A horizontal bar with a caption on the left and the value on the right.
void Bar(const char* caption, float fraction, const char* value, ImU32 colour) {
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFontSize() * 0.55f;
    const float capW = ImGui::GetFontSize() * 6.5f, valW = ImGui::GetFontSize() * 9.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    dl->AddText(ImVec2(p.x, p.y), U(eam::ui::theme::kMuted), caption);
    const float x0 = p.x + capW, x1 = p.x + w - valW, cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
    dl->AddRectFilled(ImVec2(x0, cy - h * 0.5f), ImVec2(x1, cy + h * 0.5f), U(eam::ui::theme::kBorderBright), h * 0.5f);
    const float f = fraction < 0 ? 0 : (fraction > 1 ? 1 : fraction);
    if (f > 0.001f) dl->AddRectFilled(ImVec2(x0, cy - h * 0.5f), ImVec2(x0 + (x1 - x0) * f, cy + h * 0.5f), colour, h * 0.5f);
    dl->AddText(ImVec2(x1 + ImGui::GetFontSize() * 0.6f, p.y), U(eam::ui::theme::kText), value);
    ImGui::Dummy(ImVec2(w, rowH));
}

void Reading(ImU32 dot, const std::string& text) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = ImGui::GetFontSize() * 0.22f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r + 2, p.y + ImGui::GetTextLineHeight() * 0.5f), r, dot);
    ImGui::SetCursorScreenPos(ImVec2(p.x + ImGui::GetFontSize() * 1.1f, p.y));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace

void RenderTabPerformance() {
    GpuStats::Instance().Wanted();
    Metrics& M = Metrics::Instance();
    const double now = M.Now();
    const char* source = "DLSSNR02";
    const char* modelKey = "model_ms";
    const auto recent = [&](const char* id) {
        auto s = M.Get(id, "frame_ms", 6);
        return !s.samples.empty() && now - s.samples.back().t < 5;
    };
    if (!recent(source)) {
        for (const auto& candidate : {std::pair<const char*, const char*>{"DLSS4DLAA", "dlss_ms"},
                                     {"FSR3UPSC", "fsr_ms"}, {"XESSUPSC", "xess_ms"}}) {
            if (recent(candidate.first)) {
                source = candidate.first;
                modelKey = candidate.second;
                break;
            }
        }
    }
    const Metrics::Series frame = M.Get(source, "frame_ms", kGraphSeconds + 1);
    const Metrics::Series model = M.Get(source, modelKey, kGraphSeconds + 1);
    const Metrics::Series util = M.Get("system", "gpu_util", kGraphSeconds + 1);
    const Metrics::Series power = M.Get("system", "gpu_power_w", kGraphSeconds + 1);
    const GpuStats::Snapshot gpu = GpuStats::Instance().Get();
    const bool haveFrames = !frame.samples.empty() && now - frame.samples.back().t < 5.0;
    const bool haveGpu = gpu.ok && gpu.ageSeconds < 5.0;

    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::BeginChild("##perf", ImVec2(0, 0), false);

    if (!haveFrames && !haveGpu) {
        widgets::EmptyState(ImGui::GetContentRegionAvail().x, "暂无实时数据",
                            gpu.ok ? "等待显卡..." : "使用神经渲染或独立超分器开始缩放。显卡数据需要 NVIDIA 显卡。");
        if (!gpu.ok && !gpu.why.empty()) { ImGui::Dummy(ImVec2(0, S(6))); ImGui::TextDisabled("显卡：%s", gpu.why.c_str()); }
        ImGui::EndChild();
        return;
    }

    const Stats fs = Summarise(frame, now, kStatSeconds), ms = Summarise(model, now, kStatSeconds);
    const Stats us = Summarise(util, now, kStatSeconds);

    // ---- tiles
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float tileW = (ImGui::GetContentRegionAvail().x - gap * 3.0f) / 4.0f;
    char big[64], sub[96];
    if (haveFrames && fs.avg > 0.1f) snprintf(big, sizeof big, "%.0f FPS", 1000.0f / fs.avg); else snprintf(big, sizeof big, "-");
    snprintf(sub, sizeof sub, haveFrames ? "%.1f ms 平均值" : "无帧", fs.avg);
    Tile("t1", tileW, "观察到的帧率", big, sub, U(eam::ui::theme::kAccent));
    ImGui::SameLine();
    if (haveFrames) snprintf(big, sizeof big, "%.1f ms", fs.p95); else snprintf(big, sizeof big, "-");
    snprintf(sub, sizeof sub, haveFrames ? "最慢帧 %.0f ms" : "", fs.mx);
    Tile("t2", tileW, "最慢的 5% 帧", big, sub, U(fs.p95 > 25.0f ? eam::ui::theme::kWarn : eam::ui::theme::kAccent));
    ImGui::SameLine();
    if (!model.samples.empty()) snprintf(big, sizeof big, "%.1f ms", ms.avg); else snprintf(big, sizeof big, "-");
    snprintf(sub, sizeof sub, "%s", source == std::string("DLSSNR02") ? "每次捕获都需等待神经渲染" : "");
    Tile("t3", tileW, "模型耗时", big, sub, U(eam::ui::theme::kAccent));
    ImGui::SameLine();
    if (haveGpu) snprintf(big, sizeof big, "%u%%", gpu.utilGpu); else snprintf(big, sizeof big, "-");
    snprintf(sub, sizeof sub, haveGpu ? "%.0f / %.0f W" : "", gpu.powerW, gpu.powerLimitW);
    Tile("t4", tileW, "显卡负载", big, sub, U(haveGpu && gpu.utilGpu >= 95 ? eam::ui::theme::kWarn : eam::ui::theme::kAccent));

    // ---- frame time graph
    ImGui::Dummy(ImVec2(0, S(6)));
    eam::ui::SectionLabel("帧时间（最近 20 秒）");
    {
        const std::vector<ImVec2> pts = ToPoints(frame, now, kGraphSeconds);
        const float top = (std::max)(40.0f, fs.mx * 1.1f);
        const float guides[2] = { 16.7f, 33.3f };
        const char* labels[2] = { "60 FPS", "30 FPS" };
        if (pts.size() > 1)
            eam::ui::LineGraph("frames", pts.data(), (int)pts.size(), ImVec2(0, ImGui::GetFontSize() * 7.0f), 0.0f, top, U(eam::ui::theme::kAccent), guides, labels, 2, 33.3f, U(eam::ui::theme::kWarn), "ms");
        else { ImGui::TextDisabled("最近几秒内没有帧数据。"); }
    }
    widgets::Tip("观察到的插件帧之间的时间。神经渲染报告真实捕获；独立超分器也可以观察生成的输出。16.7 ms 的水平线表示稳定的 60 FPS。超过上方虚线（33 ms）的尖峰会变为琥珀色。");

    // ---- model and GPU graphs, side by side
    ImGui::Dummy(ImVec2(0, S(6)));
    const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    ImGui::BeginGroup();
    eam::ui::SectionLabel("模型耗时 (ms)");
    {
        const std::vector<ImVec2> pts = ToPoints(model, now, kGraphSeconds);
        if (pts.size() > 1) eam::ui::LineGraph("model", pts.data(), (int)pts.size(), ImVec2(half, ImGui::GetFontSize() * 5.0f), 0.0f, (std::max)(10.0f, ms.mx * 1.2f), U(eam::ui::theme::kAccent), nullptr, nullptr, 0, 0, 0, "ms");
        else ImGui::TextDisabled("尚未报告。");
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    eam::ui::SectionLabel("显卡负载 (%)");
    {
        const std::vector<ImVec2> pts = ToPoints(util, now, kGraphSeconds);
        const float guides[1] = { 95.0f };
        if (pts.size() > 1) eam::ui::LineGraph("gpu", pts.data(), (int)pts.size(), ImVec2(half, ImGui::GetFontSize() * 5.0f), 0.0f, 100.0f, U(eam::ui::theme::kAccent), guides, nullptr, 1, 95.0f, U(eam::ui::theme::kWarn), "%");
        else ImGui::TextDisabled("等待显卡...");
    }
    ImGui::EndGroup();

    // ---- GPU details
    if (haveGpu) {
        ImGui::Dummy(ImVec2(0, S(8)));
        char t[8][96];
        snprintf(t[7], sizeof t[7], "显卡  %s%s", gpu.name.c_str(), gpu.deviceCount > 1 ? "  （多个中的第一个）" : "");
        eam::ui::SectionLabel(t[7]);
        const bool atCap = gpu.powerLimitW > 0 && gpu.powerW >= gpu.powerLimitW * 0.96;
        snprintf(t[0], sizeof t[0], "%u%%", gpu.utilGpu);
        Bar("负载", gpu.utilGpu / 100.0f, t[0], U(gpu.utilGpu >= 95 ? eam::ui::theme::kWarn : eam::ui::theme::kAccent));
        snprintf(t[1], sizeof t[1], "%.0f / %.0f W", gpu.powerW, gpu.powerLimitW);
        Bar("功耗", gpu.powerLimitW > 0 ? (float)(gpu.powerW / gpu.powerLimitW) : 0.0f, t[1], U(atCap ? eam::ui::theme::kWarn : eam::ui::theme::kAccent));
        snprintf(t[2], sizeof t[2], "%llu / %llu MB", (unsigned long long)gpu.vramUsedMB, (unsigned long long)gpu.vramTotalMB);
        Bar("显存", gpu.vramTotalMB ? (float)gpu.vramUsedMB / (float)gpu.vramTotalMB : 0.0f, t[2], U(eam::ui::theme::kAccent));
        snprintf(t[3], sizeof t[3], "%u C", gpu.tempC);
        Bar("温度", gpu.tempC / 100.0f, t[3], U(gpu.tempC >= 85 ? eam::ui::theme::kWarn : eam::ui::theme::kAccent));
        snprintf(t[4], sizeof t[4], "%u MHz 核心，%u MHz 显存", gpu.clockGraphics, gpu.clockMem);
        ImGui::TextDisabled("频率"); ImGui::SameLine(ImGui::GetFontSize() * 6.5f); ImGui::TextUnformatted(t[4]);
        const std::string th = GpuStats::ThrottleText(gpu.throttle);
        ImGui::TextDisabled("受限于"); ImGui::SameLine(ImGui::GetFontSize() * 6.5f);
        if (th.empty()) ImGui::TextUnformatted("无"); else { ImGui::PushStyleColor(ImGuiCol_Text, V(eam::ui::theme::kWarn)); ImGui::TextUnformatted(th.c_str()); ImGui::PopStyleColor(); }
    }

    // ---- what it says
    ImGui::Dummy(ImVec2(0, S(8)));
    eam::ui::SectionLabel("说明");
    bool said = false;
    if (haveGpu && gpu.powerLimitW > 0 && gpu.powerW >= gpu.powerLimitW * 0.96 && gpu.utilGpu >= 90) {
        char b[220]; snprintf(b, sizeof b, "显卡已达到功耗上限（%.0f W）且已满载，无法再提升性能。降低游戏的渲染缩放比例或模型分辨率可释放余量。", gpu.powerLimitW);
        Reading(U(eam::ui::theme::kWarn), b); said = true;
    } else if (haveGpu && (gpu.throttle & 0x60) && gpu.utilGpu >= 80) {
        Reading(U(eam::ui::theme::kWarn), "显卡因温度过高而降频。请检查机箱风道和风扇曲线。"); said = true;
    }
    if (haveFrames && fs.n > 30) {
        if (fs.p95 > 1.5f * fs.p50 && fs.p95 > 22.0f) {
            char b[200]; snprintf(b, sizeof b, "帧间隔不均匀：一半的帧耗时 %.1f ms，但最慢的 5%% 耗时 %.1f ms 或更多。这表现为卡顿。", fs.p50, fs.p95);
            Reading(U(eam::ui::theme::kWarn), b);
        } else {
            char b[200]; snprintf(b, sizeof b, "帧时间稳定：一半的帧耗时 %.1f ms，95%% 的帧耗时 %.1f ms 或更少。", fs.p50, fs.p95);
            Reading(U(eam::ui::theme::kAccent), b);
        }
        said = true;
    }
    if (!said) ImGui::TextDisabled("暂无可报告的内容。");

    // ---- every live value
    ImGui::Dummy(ImVec2(0, S(8)));
    if (eam::ui::SectionHeader("所有实时数值")) {
        if (ImGui::BeginTable("##allmetrics", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("来源"); ImGui::TableSetupColumn("数值"); ImGui::TableSetupColumn("当前"); ImGui::TableSetupColumn("平均"); ImGui::TableSetupColumn("最小"); ImGui::TableSetupColumn("最大");
            ImGui::TableHeadersRow();
            for (const auto& s : M.Snapshot(kStatSeconds)) {
                if (s.samples.empty()) continue;
                const Stats st = Summarise(s, now, kStatSeconds);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(s.addon.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(s.key.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%.2f %s", s.last(), s.unit.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%.2f", st.avg);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", st.mn);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", st.mx);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

} // namespace eam
