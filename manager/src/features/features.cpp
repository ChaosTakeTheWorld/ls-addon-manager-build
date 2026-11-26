#include "features.h"
#include "reshade_passthrough.h"
#include "windowed_mode.h"
#include "scaling_overrides.h"
#include "../config/config_manager.h"
#include "../log/logger.h"
#include <cstring>
#include <iterator>

namespace eam {
namespace features {

namespace {

enum Which { kReShade = 0, kWindowed = 1, kScaling = 2, kCount = 3 };

const Info kInfo[kCount] = {
    { reshade::kId,
      "ReShade 输入穿透",
      "在 Lossless Scaling 缩放游戏时，仍然可以在 ReShade 悬浮窗上使用鼠标和键盘。",
      "Lossless Scaling 的窗口通常会吞掉鼠标和键盘输入，导致 ReShade 菜单无法点击或输入。开启此功能后，"
      "可用下方快捷键切换穿透状态：开启时输入会传递给 ReShade 悬浮窗。当游戏或 Lossless Scaling 失去焦点时会自动关闭。\n"
      "此功能可随时开关。" },
    { windowed::kId,
      "窗口模式与第二显示器",
      "让 Lossless Scaling 可以处理窗口化游戏，或配合第二显示器使用。",
      "添加一个与游戏窗口等大的虚拟显示器，这样 Lossless Scaling 就能缩放窗口而无需全屏。它也可以只覆盖窗口的一半（分屏），"
      "或位于窗口旁边（作为第二显示器）。\n虚拟显示器必须在 Lossless Scaling 启动前就位，因此开启此功能需要重启 "
      "Lossless Scaling。关闭则立即生效。" },
    { scaling::kId,
      "Lossless Scaling 覆盖项",
      "选择显示 Lossless Scaling 帧率计数的角落。",
      "移动现有计数器的位置，不改变其读数。需在 Lossless Scaling 中启用 Draw FPS 才会显示。\n"
      "缩放过程中也可更改位置。关闭此功能会恢复 Lossless Scaling 自身的默认位置。" },
};

ConfigManager& Cfg() { return ConfigManager::Instance(); }

} // namespace

int Count() { return kCount; }

const Info& At(int index) { return kInfo[index]; }

bool IsOn(int index) { return Cfg().IsAddonEnabled(kInfo[index].id, false); }

void SetOn(int index, bool on) {
    Cfg().SetAddonEnabled(kInfo[index].id, on);
    Cfg().Save();
    LOG_INFO("Features", "%s is now %s", kInfo[index].title, on ? "on" : "off");

    switch (index) {
        case kReShade:
            if (on) reshade::Start(); else reshade::Stop();
            break;
        case kWindowed:
            if (windowed::Started()) windowed::SetActive(on);   // hooks are in: the switch is live. Otherwise it waits for a restart.
            break;
        case kScaling:
            if (on) scaling::Start(); else scaling::Stop();
            break;
    }
}

bool NeedsRestart(int index) { return index == kWindowed && IsOn(index) && !windowed::Started(); }

std::string Status(int index) {
    switch (index) {
        case kReShade: return IsOn(index) ? reshade::Status() : std::string();
        case kWindowed: return IsOn(index) ? windowed::Status() : std::string();
        case kScaling: return IsOn(index) ? scaling::Status() : std::string();
    }
    return {};
}

void RenderOptions(int index) {
    switch (index) {
        case kReShade: reshade::RenderOptions(); break;
        case kWindowed: windowed::RenderOptions(); break;
        case kScaling: scaling::RenderOptions(); break;
    }
}

// Up to 0.7.4 the features kept their settings under the ids of the standalone addons they came from; move them once.
void MoveOldSettings() {
    const char* const oldIds[] = { reshade::kOldId, windowed::kOldId };
    bool moved = false;
    for (size_t i = 0; i < std::size(oldIds); ++i)
        if (Cfg().RenameAddonSection(oldIds[i], kInfo[i].id)) { moved = true; LOG_INFO("Features", "%s: settings moved from %s to %s", kInfo[i].title, oldIds[i], kInfo[i].id); }
    if (moved) Cfg().Save();
}

void Start() {
    MoveOldSettings();
    if (IsOn(kReShade)) reshade::Start();
    if (IsOn(kWindowed)) windowed::Start();
    if (IsOn(kScaling)) scaling::Start();
}

void Stop() {
    reshade::Stop();
    windowed::Stop();
    scaling::Stop();
}

bool IsRetiredAddonId(const std::string& folderName) {
    return folderName == reshade::kOldId || folderName == windowed::kOldId;
}

} // namespace features
} // namespace eam
