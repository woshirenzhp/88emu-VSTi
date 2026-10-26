# MIDI Test Host

Windows x64 独立 VST3 乐器/效果器测试宿主。音频使用 Windows Audio (WASAPI) 或 DirectSound，编译时关闭 ASIO，不需要 ASIO 驱动或 SDK。

## 使用

1. 双击 `MIDI Test Host.exe`。
2. `Load VST3`：选择 `.vst3` 文件或完整 `.vst3` 目录。也支持拖入插件。
3. `Load MIDI`：载入 `.mid` / `.midi`，支持标准 MIDI 文件格式 0、1、变速与 SMPTE 时间。格式 2 会提示先转换。
4. 每条路由按“轨道 + 文件中的 MIDI Port 标记 + 源通道”拆分。`Destination port` 选择插件真正的 VST3 MIDI 输入总线，或系统 MIDI 输出；`Channel` 可保留原通道或改为 1–16；`Mute` 单独静音。
5. `Play / Resume` 播放或继续，`Pause` 暂停并停止当前发声，`Stop / Rewind` 停止并回到开头。暂停继续从后续事件开始，不重建暂停时已经按住的音符。
6. `Plugin editor` 打开插件原生界面，无原生界面的插件显示通用参数界面。`Audio settings` 更换输出设备、采样率、缓冲区。
7. `Save routes` / `Load routes` 保存和读取 JSON 路由预设。先载入相同 MIDI 文件，再读取预设。外部 MIDI 设备使用设备标识匹配，找不到时跳过该项。

界面端口从 P1 开始显示；SMF MIDI Port 元事件中的 0 对应 P1。没有端口元事件时默认 P1。插件没有相应端口时，目标栏提示 `Unavailable port`，需要手动选择，不会悄悄转到 P1。

`four-ports-demo.mid` 包含四条测试轨道，分别发往 P1/Ch1、P2/Ch2、P3/Ch3、P4/Ch4，依次播放音阶。单端口乐器可将四条轨道都改为 Plugin P1。

## 范围

- 每次加载一个 64 位 VST3 插件，可接收多个独立 MIDI 输入端口。VSTi 指乐器类插件；本版本支持 VST3 乐器，不包含旧版 VST2 `.dll` 加载器。
- 音频播放插件的主输出总线，附加音频输出总线不混入主输出。没有内置合成器，测试发声需要能工作的乐器插件及其所需音色/ROM。
- 路由保留音符、力度、CC、弯音、通道/复音触后、Program Change、SysEx 及事件的块内时间。CC/弯音/Program Change 优先使用插件公开的各总线 IMidiMapping；插件的实现决定其支持范围。
- SysEx 的设备 ID 与消息体保持原样，通道改写不会修改 SysEx 内部的厂商数据。
- 改路由会停止并回到开头；停止、暂停、切换文件或设备时会发送延音释放、All Notes Off、All Sound Off。插件端另发送已按下音符的 Note Off。
- 不支持实时 MIDI 输入、插件链、多实例、音频录制或插件状态保存。外部 MIDI 输出设备在启动时枚举，热插拔后重新启动宿主。
- 测试插件在宿主进程内运行，插件崩溃会影响宿主。

## 构建及验证

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_midi_test_host.ps1
```

需要 Visual Studio C++ Build Tools 和 Windows SDK。直接使用仓库中已有的 JUCE / VST3 SDK，不下载 ASIO。

构建产物：`temp/midiTestHost/MidiTestHost_artefacts/Release/MIDI Test Host.exe`。使用静态 MSVC 运行库。

独立 CMake 工程位于 `source/framework/tools/midiTestHost`，不需要构建整个模拟器。

```powershell
& '.\MIDI Test Host.exe' --self-test
& '.\MIDI Test Host.exe' --probe 'E:\path\instrument.vst3'
```

GUI 程序从命令行运行时，请等待进程退出后读取同目录的 `self-test.txt` 或 `plugin-probe.txt`。退出码 0 表示通过。回归测试检查 MIDI 路由、静音、块边界、SysEx、SMF 端口元事件、变速，以及实际 VST3 事件的 busIndex、各端口控制器映射及样本偏移。Probe 验证加载、端口枚举及 64 个处理块；输出峰值为零时不代表已经验证发声。

JUCE 多端口扩展通过 `JUCE_PLUGINHOST_MULTI_BUS_MIDI=1` 仅为本宿主启用，包含每总线 MIDI 缓冲、每总线控制器映射，以及同块内多个参数事件的样本时间；其他项目默认保持原有处理路径。相关修改位于 JUCE 的 `juce_AudioPluginInstance.h`、`juce_VST3Common.h`、`juce_VST3PluginFormat.cpp`。

本程序及依赖的再分发须遵守仓库及 JUCE 的许可证；源码位于上述工程目录，随附 `SOURCE.txt` 说明重建方式。
