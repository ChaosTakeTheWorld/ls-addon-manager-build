#include "status_text.h"
#include "../../../sdk/include/eam/version.h"
#include <cstdio>

namespace eam {
namespace window {

std::string StatusCounts(int total, int on) {
    return std::string(EAM_PRODUCT_SHORT "   |   " EAM_VERSION_STRING "   |   ") + std::to_string(total) + (total == 1 ? " 个插件，" : " 个插件，") +
           std::to_string(on) + " 个已启用";
}

std::string WithLiveStatus(std::string counts, const std::string& who, const std::string& text) {
    if (!text.empty()) counts = who + ": " + text + "   |   " + counts;
    return counts;
}
std::string CaptureSummary(const RealFrameHook::Diagnostics& d) {
    std::string text=EAM_VERSION_STRING " | LS " + d.runtimeVersion;
    text+=d.runtimeChecked?(d.runtimeVerified?" （已验证）":" （未验证）"):" （未检查）";
    if(!d.configured)return text+" | 捕获设置尚未应用";
    const auto capture=d.capture==1?"WGC":d.capture==0?"DXGI":"其他";
    const auto fg=d.fg==0?"关闭":d.fg==1?"LSFG 3":"不支持";
    const auto scaler=d.scaler==0?"关闭":d.scaler==3?"NIS":"其他";
    // TODO(zh): buffer size
    char profile[160];snprintf(profile,sizeof profile," | %s（%d）| 帧生成 %s（%d）| 缩放 %s（%d）| %s",capture,d.capture,fg,d.fg,scaler,d.scaler,d.hdr?"HDR":"SDR");
    return text+profile;
}
std::string MachineSummary(const RealFrameHook::Diagnostics& d,const SystemStats::Snapshot& s,const GpuStats::Snapshot& g) {
    std::string text="LS 显卡："+(d.adapter.empty()?std::string("尚未观察到"):d.adapter);
    if(!d.driver.empty())text+=" | 显卡驱动 "+d.driver;
    if(!g.driver.empty())text+=" | NVIDIA 驱动 "+g.driver;
    text+=" | 处理器："+(s.cpuName.empty()?std::string("未知"):s.cpuName);
    // TODO(zh): buffer size
    char reading[192];
    if(s.ok) { snprintf(reading,sizeof reading," | 处理器 %.0f%% | 内存 %.1f/%.1f GB",s.cpuPercent,s.ramUsedMB/1024.0,s.ramTotalMB/1024.0);text+=reading; }
    if(g.ok&&g.ageSeconds<4) {
        snprintf(reading,sizeof reading," | 显卡 0 占用率 %u%% | 显存 %.1f/%.1f GB | %u 摄氏度",g.utilGpu,g.vramUsedMB/1024.0,g.vramTotalMB/1024.0,g.tempC);text+=reading;
        text+=" | 采样的显卡 0："+g.name;
        if(g.deviceCount>1)text+="（多个显卡；占用率未必是 LS 所用显卡的）";
    } else text+=" | 显卡占用率不可用"+(g.why.empty()?std::string():"（"+g.why+"）");
    return text;
}
std::string CaptureObservation(const RealFrameHook::Diagnostics& d,uint64_t now) {
    if(!d.hookError.empty())return d.hookError;
    if(!d.captureError.empty())return d.captureError;
    if(!d.configured)return "未观察到已应用的捕获配置；请开始对游戏进行缩放";
    if(!d.dispatches)return "未观察到 LS 计算调度；缩放可能处于空闲状态或捕获尚未开始";
    if(!d.captures)return std::to_string(d.dispatches)+" 次 LS 计算调度 | 未记录到神经捕获回调";
    // TODO(zh): buffer size
    char text[200];snprintf(text,sizeof text,"%llu 次 LS 调度，%llu 次捕获回调 | 上次捕获在 %.1f 秒前 | 纹理 %ux%u，DXGI 格式 %u",
        d.dispatches,d.captures,d.lastCapture&&now>=d.lastCapture?double(now-d.lastCapture)/1000:0,d.width,d.height,d.format);
    return text;
}

} // namespace window
} // namespace eam
