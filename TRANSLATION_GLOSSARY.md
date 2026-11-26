# LS Addon Manager 汉化术语表

本文件用于保证 `manager/src/gui/` 下所有翻译的一致性。翻译时严格遵循下表。

## 通用规则

1. 只翻译**界面可见文本**：`ImGui::Text*` / `ImGui::Button` / `SectionLabel` / `widgets::Tip` / `ToastShow` / 弹窗标题 / 托盘菜单 / 文件对话框标题等。
2. **不要翻译**：配置键名（如 `"auto_load"`、`"ui"`、`"font"`）、文件路径、日志字段名、API 名、产品名、技术缩写。
3. 保留所有 `printf` 占位符（`%s`、`%d`、`%lu`、`%.1f` 等）及其顺序。
4. 保留字符串中的换行符 `\n` 和转义序列。
5. 中文字符串是 UTF-8：每个汉字 3 字节。若原文本用于固定大小 `char buf[N]` / `snprintf`，翻译后可能截断——遇到此类情况在注释中标注 `// TODO(zh): buffer size`，不要擅自扩大缓冲区。
6. 保留原有代码风格与缩进；逐功能分段注释。
7. 宽字符串 `L"..."` 同样翻译，保持 `L` 前缀。

## 术语对照

| English | 中文 |
| --- | --- |
| Addon | 插件 |
| Addon Manager | 插件管理器 |
| Manager | 管理器 |
| Settings | 设置 |
| General | 常规 |
| About | 关于 |
| Logs | 日志 |
| Performance | 性能 |
| Features | 功能 |
| Load / Loaded | 加载 / 已加载 |
| Unload / Unloaded | 卸载 / 已卸载 |
| Auto-load | 自动加载 |
| Enable / Enabled | 启用 / 已启用 |
| Disable / Disabled | 禁用 / 已禁用 |
| Restart / Restart needed | 重启 / 需要重启 |
| Update / Updates | 更新 |
| Backup | 备份 |
| Restore | 恢复 |
| Save | 保存 |
| Browse | 浏览 |
| Open | 打开 |
| Close | 关闭 |
| Apply | 应用 |
| Cancel | 取消 |
| Confirm | 确认 |
| Delete | 删除 |
| Install | 安装 |
| Remove | 移除 |
| Refresh | 刷新 |
| Scan | 扫描 |
| GPU | 显卡 |
| FPS | 帧率（叙述性文字）；但作为数值单位时保留缩写，如 "60 FPS"、"%.0f FPS" |
| Frame / Framerate | 帧 / 帧率 |
| Latency | 延迟 |
| Scale / Scaling | 缩放 |
| Upscale / Upscaling | 超分 |
| Neural Rendering | 神经渲染 |
| Driver | 驱动 |
| VRAM | 显存 |
| Overlay | 悬浮窗 |
| Hotkey | 快捷键 |
| Tray / Notification area | 托盘 / 通知区域 |
| Window | 窗口 |
| Theme | 主题 |
| Font | 字体 |
| Log level | 日志级别 |
| Interface size | 界面大小 |
| Diagnostics | 诊断 |
| Support | 支持 |
| Website | 网站 |
| License | 许可协议 |
| Version | 版本 |
| Build | 构建 |
| Development | 开发版 |
| Ready | 就绪 |
| Idle | 空闲 |
| Active | 活动中 |
| Failed / Faulted | 故障 |
| Error | 错误 |
| Warning | 警告 |
| Info | 信息 |
| Success | 成功 |
| Copy | 复制 |
| Search | 搜索 |
| Filter | 筛选 |
| Category | 分类 |
| Status | 状态 |
| Option | 选项 |
| Advanced | 高级 |
| Experimental | 实验性 |
| Required | 必需 |
| Optional | 可选 |
| Unknown | 未知 |
| Not available | 不可用 |
| None | 无 |
| Yes / No | 是 / 否 |
| On / Off | 开 / 关 |
| Click | 点击 |
| Folder / File | 文件夹 / 文件 |
| Force / Forced | 强制 |
| Check for updates | 检查更新 |
| Download | 下载 |
| Upload | 上传 |
| Export | 导出 |
| Import | 导入 |
| Reset | 重置 |
| Default | 默认 |
| Custom | 自定义 |
| Enabled addons | 已启用的插件 |
| Addon folder | 插件文件夹 |
| Startup | 启动时 |
| Compatibility | 兼容性 |
| Background | 后台 |
| Foreground | 前台 |
| Fullscreen | 全屏 |
| Borderless | 无边框 |
| Windowed | 窗口化 |
| Capture | 捕获 |
| Render | 渲染 |
| Shader | 着色器 |
| Hook | 钩子 |
| Resolution | 分辨率 |
| Refresh rate | 刷新率 |
| Monitor / Display | 显示器 |
| System | 系统 |
| CPU | 处理器 |
| Memory | 内存 |
| Power limit | 功耗上限 |
| Temperature | 温度 |
| Usage | 占用率 |
| Graph / Chart | 图表 |

## 保留原文（不翻译）

- Lossless Scaling
- LS Addon Manager
- Dear ImGui
- DLSS / FSR / XeSS / NIS / ReShade
- NVIDIA / AMD / Intel
- GitHub
- Windows
- JSON / DLL / EXE / PNG / BMP
- 版本号、哈希值、文件名、URL
