// SPDX-License-Identifier: GPL-3.0-only
#include "service.h"
#include "status_reporter.h"
#include "diagnostic_text.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <commdlg.h>
#include <cstdio>
#include <eam/addon_sdk.h>
#include <eam/real_frame_status.h>
#include <eam/widgets.h>
#include <filesystem>
namespace {
constexpr const char *Id = "DLSSNR02";
IHost *host = nullptr;
std::unique_ptr<neural::Service> service;
neural::PipelineSettings settings;
bool enabled = true, rebuildPending = false;
char modelPath[32768]{};
neural::StatusReporter reporter;
std::atomic<ULONGLONG> lastCaptureAt{0};
std::atomic<double> lastCallbackMs{0};
std::atomic<double> lastIntervalMs{0};
std::string startupError;
// Render-thread-only samples. Report periodically so a gameplay session can be
// diagnosed without a screenshot or a Present hook changing the render path.
struct FrameDiagnostics {
    std::array<double, 512> intervals{}, cpu{};
    size_t count = 0;
    uint64_t epoch = 0;
    std::chrono::steady_clock::time_point report{};
    void Record(uint64_t currentEpoch, double interval, double elapsed) {
        const auto now = std::chrono::steady_clock::now();
        if (epoch != currentEpoch || report.time_since_epoch().count() == 0) {
            epoch = currentEpoch;
            count = 0;
            report = now;
        }
        if (interval <= 0)
            return;
        intervals[count % intervals.size()] = interval;
        cpu[count++ % cpu.size()] = elapsed;
        if (now - report < std::chrono::seconds(5))
            return;
        const size_t n = std::min(count, cpu.size());
        auto stats = [n](std::array<double, 512> values) {
            std::sort(values.begin(), values.begin() + n);
            double mean = 0;
            for (size_t i = 0; i < n; ++i)
                mean += values[i];
            return std::array<double, 3>{mean / n, values[size_t((n - 1) * .95)], values[size_t((n - 1) * .99)]};
        };
        const auto c = stats(intervals), t = stats(cpu);
        const auto snapshot = service->Snapshot();
        const auto &m = snapshot.metrics;
        char text[768];
        snprintf(text, sizeof(text),
                 "NR timing: capture %ux%u, model %ux%u, observed %.1f fps; capture mean/p95/p99 %.2f/%.2f/%.2f ms; "
                 "callback CPU mean/p95/p99 %.2f/%.2f/%.2f ms; last GPU prepare/flow/model/compose "
                 "%.2f/%.2f/%.2f/%.2f ms; epoch processed %llu, cuts/resets %llu/%llu, "
                 "slot/summary CPU wait %.2f/%.2f ms; GPU priority capture/NR %d/%d; %s",
                 m.width, m.height, m.workingWidth, m.workingHeight, c[0] > 0 ? 1000 / c[0] : 0, c[0], c[1], c[2], t[0],
                 t[1], t[2], m.prepareMs, m.flowMs, m.modelMs, m.compositionMs, m.accepted, m.cuts,
                 m.resets, m.slotWaitMs, m.summaryWaitMs, m.captureGpuPriority, m.nrQueuePriority, snapshot.status.c_str());
        host->Log(EAM_LOG_INFO, text);
        count = 0;
        report = now;
    }
};
void ModuleAnchor() {}
std::wstring Wide(const char *p) {
    int n = MultiByteToWideChar(CP_UTF8, 0, p, -1, nullptr, 0);
    std::wstring v(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, p, -1, v.data(), n);
    if (!v.empty())
        v.pop_back();
    return v;
}
std::string Utf8(const std::wstring &p) {
    int n = WideCharToMultiByte(CP_UTF8, 0, p.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string v(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, p.c_str(), -1, v.data(), n, nullptr, nullptr);
    if (!v.empty())
        v.pop_back();
    return v;
}
float Number(const char *key, float value, float low, float high) {
    char def[48];
    snprintf(def, sizeof(def), "%.5g", value);
    const float got = strtof(host->GetConfig(Id, key, def), nullptr);
    return std::isfinite(got) ? std::clamp(got, low, high) : value;
}
void Put(const char *key, double value) {
    char text[64];
    snprintf(text, sizeof(text), "%.8g", value);
    host->SetConfig(Id, key, text);
}
void Save() {
    Put("schema", 1);
    Put("enabled", enabled);
    Put("scalePercent", settings.scalePercent);
    Put("flowGrid", settings.flowGrid);
    Put("flowQuality", settings.flowQuality);
    Put("preset", settings.model.preset);
    Put("passes", settings.passes);
    Put("style", settings.model.style);
    Put("intensity", settings.model.intensity);
    Put("localStructure", settings.model.localStructure);
    Put("localTone", settings.model.localTone);
    Put("skinStructure", settings.model.skinStructure);
    Put("autoMask", settings.model.autoMask);
    Put("detail", settings.composition.detail);
    Put("colour", settings.composition.colour);
    Put("maxRatio", settings.composition.maxRatio);
    Put("whiteNits", settings.composition.whiteNits);
    Put("proxy", settings.composition.reversible);
    Put("reconstruction", settings.composition.reconstruction);
    Put("apply", settings.composition.apply);
    Put("pictureEnabled", settings.picture.enabled);
    for (const auto &c : neural::PictureControls)
        Put(c.key, settings.picture.*(c.value));
    host->SetConfig(Id, "modelPath", modelPath);
    host->SaveConfig();
}
void Configure(bool rebuild) {
    settings.modelPath = Wide(modelPath);
    service->Configure(settings, enabled, rebuild);
    host->RequireRealFrameProcessing(enabled);
}
const char *Support(EamFrameSupport s) {
    return EamSupportText(s);
}
void Status() {
    if (!host)
        return;
    if (!startupError.empty()) {
        host->SetStatus(Id, startupError.c_str(), 3);
        return;
    }
    if (!service) return;
    const auto snapshot = service->Snapshot();
    const auto state = host->GetRealFrameSupport();
    const char *support = Support(state);
    const bool blockedProfile = support && state != EamFrameSupport::Waiting && state != EamFrameSupport::SettingsChanging;
    const std::string failure = snapshot.status.substr(0, snapshot.status.find("; processing paused."));
    std::string message = !snapshot.enabled ? "已禁用；允许原始画面"
                          : blockedProfile ? support
                          : snapshot.failed ? "捕获已暂停：" + failure + ". " + neural::FailureGuidance(snapshot.status) + ". 禁用 NR 以继续"
                          : support ? support : snapshot.status;
    int level = !snapshot.enabled ? 0 : blockedProfile ? 2 : snapshot.failed ? 3 : support ? 0 : snapshot.ready ? 1 : 0;
    const auto captureAt = lastCaptureAt.load();
    const double quiet = captureAt ? double(GetTickCount64() - captureAt) / 1000 : 0;
    if (snapshot.enabled && !snapshot.failed && !support) {
        if (snapshot.stage != neural::Stage::Idle && (!snapshot.ready || snapshot.stageSeconds >= 3)) {
            char age[32];snprintf(age,sizeof age," (%.1f s)",snapshot.stageSeconds);
            message = std::string("捕获等待中：") + neural::StageName(snapshot.stage) + age;
            if (snapshot.stageSeconds >= 3) { message += "；如果一直停留在此，请禁用 NR 或重启 LS"; level=2; }
        } else if (snapshot.ready && quiet >= 3) {
            char age[32];snprintf(age,sizeof age,"%.0f s",quiet);
            message = std::string("无新捕获已持续 ") + age + "；缩放可能已停止、暂停或来源不可用";
            level = 0; // Stopping/minimizing scaling is normal; absence alone is not a failure.
        }
    }
    const auto &m = snapshot.metrics;
    const double interval = lastIntervalMs.load();
    const bool liveFrames = snapshot.enabled && snapshot.ready && !snapshot.failed && !support && quiet < 3;
    char stats[512];
    // TODO(zh): buffer size
    snprintf(stats,sizeof stats,"捕获 %ux%u -> NR %ux%u | %llu 已处理 | %.1f 捕获 fps%s | 最近 NR GPU %.2f ms | 回调 CPU %.2f ms | 槽/摘要等待 %.2f/%.2f ms | %u 次处理 | %llu 剪切/%llu 重置",
        m.width,m.height,m.workingWidth,m.workingHeight,m.accepted,liveFrames&&interval>0?1000/interval:0,
        liveFrames?"":" (等待中/关闭)",m.prepareMs+m.flowMs+m.modelMs+m.compositionMs,lastCallbackMs.load(),m.slotWaitMs,m.summaryWaitMs,m.passes,m.cuts,m.resets);
    // Log transitions, including failures before the first callback returns.
    static std::string previous;
    std::string transition = !snapshot.enabled ? "Disabled" : blockedProfile ? support : snapshot.failed ? snapshot.status : support ? support : snapshot.status;
    if (snapshot.enabled && !snapshot.failed && !support) {
        if (snapshot.stage != neural::Stage::Idle && snapshot.stageSeconds >= 3) transition += neural::StageName(snapshot.stage);
        else if (snapshot.ready && quiet >= 3) transition += " (no new captures)";
    }
    if (transition != previous) { host->Log(level>=2?EAM_LOG_WARN:EAM_LOG_INFO, message.c_str()); previous=transition; }
    message += "\n";message += stats;
    if (blockedProfile && snapshot.failed) message += "\n上一个管线错误：" + snapshot.status;
    host->SetStatus(Id, message.c_str(), level);
    host->PublishMetric(Id, "prepare_ms", m.prepareMs, "ms");
    host->PublishMetric(Id, "flow_ms", m.flowMs, "ms");
    host->PublishMetric(Id, "model_ms", m.modelMs, "ms");
    host->PublishMetric(Id, "composition_ms", m.compositionMs, "ms");
    host->PublishMetric(Id, "summary_wait_ms", m.summaryWaitMs, "ms");
    host->PublishMetric(Id, "nr_total_ms", m.prepareMs + m.flowMs + m.modelMs + m.compositionMs, "ms");
    host->PublishMetric(Id, "callback_cpu_ms", lastCallbackMs.load(), "ms");
    host->PublishMetric(Id, "slot_wait_ms", m.slotWaitMs, "ms");
    host->PublishMetric(Id, "accepted", double(m.accepted), "frames");
    host->PublishMetric(Id, "model_passes", m.passes, "passes");
}
bool Frame(const EamRealFrame &frame, void *) {
    static std::chrono::steady_clock::time_point last;
    static uint64_t epoch = 0;
    static FrameDiagnostics diagnostics;
    const auto now = std::chrono::steady_clock::now();
    const double interval = epoch == frame.epoch && last.time_since_epoch().count()
                                ? std::chrono::duration<double, std::milli>(now - last).count()
                                : 0;
    if (host && interval > 0)
        host->PublishMetric(Id, "frame_ms", interval, "ms");
    last = now;
    epoch = frame.epoch;
    lastCaptureAt = GetTickCount64();
    lastIntervalMs = interval;
    const bool result = service && service->Process(frame);
    const double cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count();
    lastCallbackMs = cpuMs;
    if (host && service)
        diagnostics.Record(frame.epoch, interval, cpuMs);
    return result;
}
bool Slider(const char *label, float *value, float low, float high) {
    return ImGui::SliderFloat(label, value, low, high, "%.2f", ImGuiSliderFlags_AlwaysClamp);
}
} // namespace
EAM_EXPORT void AddonInitialize(IHost *supplied, ImGuiContext *context, void *alloc, void *freeFn, void *user) {
    ImGui::SetCurrentContext(context);
    ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(alloc), reinterpret_cast<ImGuiMemFreeFunc>(freeFn),
                                 user);
    eam::ui::InitAddonImGui();
    host = supplied;
    startupError.clear();
    lastCaptureAt=0;lastIntervalMs=0;lastCallbackMs=0;
    if (host->GetHostVersion() < 0x010400) {
        startupError="需要 Manager API 1.4 或更高版本；请安装匹配的管理器和 NR 插件";
        reporter.Start(Status);
        return;
    }
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleAnchor), &module);
    wchar_t path[32768]{};
    GetModuleFileNameW(module, path, 32768);
    auto folder = std::filesystem::path(path).parent_path();
    settings.forwarderPath = (folder / L"NeuralRendering_nvngx.dll").wstring();
    wchar_t local[32768]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    settings.cachePath = (std::filesystem::path(local) / L"LSAddonManager" / L"DLSSNR02" / L"cache").wstring();
    GetModuleFileNameW(nullptr, path, 32768);
    auto model = std::filesystem::path(path).parent_path() / L"nvngx_dlssnr.dll";
    const std::string defaultPath = Utf8(model.wstring());
    snprintf(modelPath, sizeof(modelPath), "%s", host->GetConfig(Id, "modelPath", defaultPath.c_str()));
    enabled = Number("enabled", 1, 0, 1) != 0;
    settings.scalePercent = unsigned(Number("scalePercent", 100, 25, 100));
    settings.flowGrid = unsigned(Number("flowGrid", 4, 1, 4));
    if (settings.flowGrid == 3)
        settings.flowGrid = 4;
    settings.flowQuality = unsigned(Number("flowQuality", 2, 1, 3));
    settings.model.preset = unsigned(Number("preset", 0, 0, 3));
    settings.passes = unsigned(Number("passes", 1, 1, neural::PipelineSettings::MaxPasses));
    settings.model.style = unsigned(Number("style", 0, 0, 2));
    const neural::ModelSettings modelDefaults;
    settings.model.intensity = Number("intensity", modelDefaults.intensity, 0, 2);
    settings.model.localStructure = Number("localStructure", modelDefaults.localStructure, 0, 2);
    settings.model.localTone = Number("localTone", modelDefaults.localTone, 0, 2);
    settings.model.skinStructure = Number("skinStructure", modelDefaults.skinStructure, -1, 2);
    // Convert legacy follow-local settings once; the model now receives an explicit strength.
    if (settings.model.skinStructure < 0) {
        settings.model.skinStructure = settings.model.localStructure;
        Put("skinStructure", settings.model.skinStructure);
        host->SaveConfig();
    }
    settings.model.autoMask = Number("autoMask", 1, 0, 1) != 0;
    settings.composition.detail = Number("detail", 1, 0, 4);
    settings.composition.colour = Number("colour", 1, 0, 4);
    settings.composition.maxRatio = Number("maxRatio", 2, 1, 8);
    settings.composition.whiteNits = Number("whiteNits", 203, 80, 1000);
    settings.composition.reversible = unsigned(Number("proxy", 0, 0, 4));
    settings.composition.reconstruction = unsigned(Number("reconstruction", 1, 0, 1));
    settings.composition.apply = Number("apply", 1, 0, 1) != 0;
    settings.picture.enabled = Number("pictureEnabled", 0, 0, 1) != 0;
    for (const auto &c : neural::PictureControls)
        settings.picture.*(c.value) = Number(c.key, c.neutral, c.low, c.high);
    service = std::make_unique<neural::Service>();
    if (!host->SetRealFrameCallback(Frame)) {
        service.reset();
        startupError="另一个插件占用了真实帧处理；请禁用冲突的插件并重新加载 NR";
        reporter.Start(Status);
        host->Log(EAM_LOG_ERROR, "NR 无法获取真实帧阶段");
        return;
    }
    Configure(true);
    host->Log(EAM_LOG_INFO, "DLSS NR 已启动：原始捕获 -> NVIDIA 光流 -> NR -> 合成 -> LSFG");
    reporter.Start(Status);
}
EAM_EXPORT void AddonShutdown() {
    reporter.Stop();
    if (host) {
        if (service) {
            service->Configure(settings, false, false);
            host->RequireRealFrameProcessing(false);
            host->SetRealFrameCallback(nullptr);
            service.reset();
        }
        host->SetStatus(Id, "", 0);
    }
    host = nullptr;
}
EAM_EXPORT uint32_t GetAddonCapabilities() {
    return EAM_CAP_HAS_SETTINGS | EAM_CAP_D3D11_DEVICE_ACCESS | EAM_CAP_DISPATCH_HOOK;
}
EAM_EXPORT const char *GetAddonName() {
    return "DLSS 神经渲染";
}
EAM_EXPORT const char *GetAddonVersion() {
    return "1.2.2";
}
EAM_EXPORT const char *GetAddonAuthor() {
    return "FriedSnowman";
}
EAM_EXPORT const char *GetAddonDescription() {
    return "在 LSFG 之前对原始捕获画面进行 NVIDIA 神经渲染，并采用独立的 NVIDIA 光流。";
}
EAM_EXPORT void AddonRenderSettings() {
    if (!host || !service)
        return;
    bool changed = false, rebuild = false;
    if (ImGui::Checkbox("神经渲染", &enabled)) {
        changed = true;
        rebuild = true;
    }
    ImGui::TextWrapped("在 LSFG 之前处理真实捕获画面。需要 NVIDIA、WGC 和用户提供的模型。"
                       "每次捕获都会等待处理；GPU 负载可能会降低帧率。"
                       "捕获画面不包含游戏深度或运动数据。");
    eam::ui::SectionLabel("模型文件");
    ImGui::SetNextItemWidth(-90);
    if (ImGui::InputText("##model", modelPath, sizeof(modelPath), ImGuiInputTextFlags_EnterReturnsTrue)) {
        changed = true;
        rebuild = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("浏览")) {
        wchar_t chosen[32768]{};
        auto current = Wide(modelPath);
        wcsncpy_s(chosen, current.c_str(), _TRUNCATE);
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.lpstrFile = chosen;
        dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"NVIDIA NR 运行库\0nvngx_dlssnr.dll\0DLL 文件\0*.dll\0";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) {
            snprintf(modelPath, sizeof(modelPath), "%s", Utf8(chosen).c_str());
            changed = true;
            rebuild = true;
        }
    }
    ImGui::TextDisabled("模型 DLL 未包含在内。");
    eam::ui::SectionLabel("质量与性能");
    int scale = int(settings.scalePercent);
    if (ImGui::SliderInt("工作分辨率", &scale, 25, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
        settings.scalePercent = unsigned(scale);
        rebuild = true;
    }
    ImGui::TextDisabled("每个轴的百分比；没有隐藏的分辨率上限。");
    int grid = settings.flowGrid == 1 ? 0 : settings.flowGrid == 2 ? 1 : 2;
    if (ImGui::Combo("光流网格", &grid, "1 像素\0 2 像素\0 4 像素（默认）\0")) {
        settings.flowGrid = 1u << grid;
        rebuild = true;
    }
    int quality = int(settings.flowQuality) - 1;
    if (ImGui::Combo("光流质量", &quality, "慢速 / 更高质量\0中等（默认）\0快速\0")) {
        settings.flowQuality = quality + 1;
        rebuild = true;
    }
    eam::ui::SectionLabel("模型控制");
    if (ImGui::Button("恢复默认")) {
        settings.model = neural::ModelSettings{};
        settings.composition = neural::CompositionSettings{};
        settings.passes = 1;
        changed = true;
        rebuild = true;
    }
    int preset = int(settings.model.preset);
    if (ImGui::Combo("模型", &preset, "默认\0原始 (A)\0快速 (B)\0预设 3\0")) {
        settings.model.preset = preset;
        rebuild = true;
    }
    int style = int(settings.model.style);
    if (ImGui::Combo("风格", &style, "默认\0自然\0电影\0")) {
        settings.model.style = style;
        rebuild = true;
    }
    rebuild |= Slider("强度", &settings.model.intensity, 0, 2);
    rebuild |= Slider("局部结构", &settings.model.localStructure, 0, 2);
    rebuild |= Slider("局部色调", &settings.model.localTone, 0, 2);
    rebuild |= ImGui::Checkbox("自动皮肤检测", &settings.model.autoMask);
    ImGui::TextWrapped("启用后可单独调整检测到的皮肤细节。");
    ImGui::BeginDisabled(!settings.model.autoMask);
    rebuild |= Slider("皮肤结构", &settings.model.skinStructure, 0, 2);
    ImGui::EndDisabled();
    int passes = int(settings.passes);
    if (ImGui::SliderInt("多通道", &passes, 1, neural::PipelineSettings::MaxPasses,
                         passes == 1 ? "关闭" : "%d", ImGuiSliderFlags_AlwaysClamp)) {
        settings.passes = unsigned(passes);
        rebuild = true;
    }
    ImGui::TextWrapped("除了制作烂梗，否则不要使用多通道。");
    if (ImGui::CollapsingHeader("画面")) {
        changed |= ImGui::Checkbox("启用画面调整", &settings.picture.enabled);
        ImGui::TextWrapped("在 DLSS5 之后、Lossless Scaling 之前调整成品画面。"
                           "关闭或中性不会产生 GPU 开销。更改实时生效。");
        if (!settings.picture.enabled) ImGui::BeginDisabled();
        for (const auto &c : neural::PictureControls) {
            changed |= eam::ui::SliderFloat(c.label, &(settings.picture.*(c.value)), c.low, c.high,
                                             c.format, ImGuiSliderFlags_AlwaysClamp, &c.neutral);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
                ImGui::TextUnformatted(c.help);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        if (!settings.picture.enabled) ImGui::EndDisabled();
        if (ImGui::Button("重置画面")) {
            const bool wasEnabled = settings.picture.enabled;
            settings.picture = {};
            settings.picture.enabled = wasEnabled;
            changed = true;
        }
        if (!settings.composition.apply)
            ImGui::TextWrapped("当前显示原始画面：画面调整也被绕过。");
    }
    if (ImGui::CollapsingHeader("合成（高级）")) {
        changed |= Slider("细节", &settings.composition.detail, 0, 4);
        changed |= Slider("色彩", &settings.composition.colour, 0, 4);
        changed |= Slider("最大亮度比", &settings.composition.maxRatio, 1, 8);
        int proxy = int(settings.composition.reversible);
        if (ImGui::Combo("HDR 代理", &proxy,
                         "拐点（默认）\0Neutwo 合成\0Neutwo 替换\0混合合成\0混合替换\0")) {
            settings.composition.reversible = proxy;
            rebuild = true;
        }
        rebuild |= ImGui::SliderFloat("scRGB 参考白", &settings.composition.whiteNits, 80, 1000, "%.0f nits",
                                      ImGuiSliderFlags_AlwaysClamp);
        int reconstruction = int(settings.composition.reconstruction);
        if (ImGui::Combo("重建", &reconstruction, "经典\0匹配残差（默认）\0")) {
            settings.composition.reconstruction = reconstruction;
            changed = true;
        }
        changed |= ImGui::Checkbox("应用模型结果", &settings.composition.apply);
        ImGui::TextWrapped(
            "细节和色彩默认为 1。替换代理直接使用模型画面。关闭应用会保持处理运行以便对比。"
            "工作分辨率和模型更改会在松开后重建管线；重建期间捕获会等待。");
    }
    if (ImGui::Button("重试初始化")) {
        rebuild = true;
        changed = true;
    }
    rebuildPending |= rebuild;
    if (changed || rebuild)
        Save();
    if (changed && !rebuildPending)
        Configure(false);
    if (rebuildPending && !ImGui::IsAnyItemActive()) {
        Configure(true);
        rebuildPending = false;
    }
    const auto snapshot = service->Snapshot();
    const auto &m = snapshot.metrics;
    eam::ui::SectionLabel("管线状态");
    const char *support = Support(host->GetRealFrameSupport());
    ImGui::TextWrapped("%s", !snapshot.enabled ? "已禁用；允许原始画面" : support ? support : snapshot.status.c_str());
    if (snapshot.enabled && support && snapshot.failed)
        ImGui::TextWrapped("%s", snapshot.status.c_str());
    if (snapshot.enabled && !snapshot.failed && snapshot.stage != neural::Stage::Idle)
        ImGui::TextWrapped("%s (%.1f s)", neural::StageName(snapshot.stage), snapshot.stageSeconds);
    ImGui::Text("捕获 %u x %u；模型 %u x %u", m.width, m.height, m.workingWidth, m.workingHeight);
    if (snapshot.ready && m.passes > 1)
        ImGui::Text("多通道：%u 次处理；模型计时包含完整链路", m.passes);
    ImGui::Text("NR GPU 时间 %.2f ms；回调 CPU %.2f ms", m.prepareMs + m.flowMs + m.modelMs + m.compositionMs,
                lastCallbackMs.load());
    ImGui::TextWrapped("GPU：准备 %.2f / 光流等待+转换 %.2f / 模型 %.2f / 合成 %.2f ms", m.prepareMs,
                       m.flowMs, m.modelMs, m.compositionMs);
    ImGui::TextWrapped("CPU 等待：帧槽 %.2f ms / 场景摘要 %.2f ms", m.slotWaitMs, m.summaryWaitMs);
    ImGui::Text("Epoch：%llu 已处理 / %llu 剪切 / %llu 重置", m.accepted, m.cuts, m.resets);
}
