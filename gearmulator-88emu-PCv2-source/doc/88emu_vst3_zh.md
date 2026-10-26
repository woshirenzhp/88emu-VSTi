# 88emuPlayer VST3 四路 MIDI 版

Windows x64 / Release，基于本项目 2.2.28。这是 VST3 乐器（Instrument / Synth），具有立体声音频输出和四个真正的 VST3 MIDI 事件输入总线。

## 安装与使用

1. 将整个 `88emuPlayer.vst3` **文件夹**复制到 `C:\Program Files\Common Files\VST3`，或宿主支持扫描的自定义 VST3 目录。不要只复制里面的同名 DLL。
2. 将自己的 ROM 放到 Windows「文档」中的 `The Usual Suspects\88emuPlayer\roms`，允许按机型分子文件夹。本项目提供的 `sc_backup\sc8850` 已在测试中成功识别，无需改名。ROM 不包含在发行包内。
3. 重新扫描插件，在 DAW 中加载 **88emuPlayer**，选择 **SC-8850**，等待固件启动。也可在设置中启用 Fast Boot。
4. 在支持多个 VST3 事件输入总线的宿主中，将 MIDI 轨分别路由到 **MIDI Input A / B / C / D**。每路都有 MIDI 通道 1–16，合计 64 个独立寻址声部。音频混合到同一组立体声输出。

| 输入总线 | SC-8850 声部组 | MIDI 通道 |
|---|---|---|
| A | A | 1–16 |
| B | B | 1–16 |
| C | C | 1–16 |
| D | D | 1–16 |

四路指的是 **DAW 内的 VST3 事件总线**，不是 Windows 系统中新建的四个虚拟 MIDI 设备。只暴露第一个事件输入总线的宿主只能访问 A；将一条轨的 MIDI 通道改成 2，并不会切换到 B 端口。

SC-88 / SC-88Pro 等两组机型只处理 A、B；其余总线的事件被丢弃。要使用完整四组，请选 SC-8850。

支持按端口传递音符、力度、复音压力、SysEx、CC、弯音、通道压力和 Program Change。保留宿主事件的块内采样偏移。Bank Select 可通过 CC0 / CC32 发送。插件不自行占用外部 MIDI 设备；音频设备、采样率、缓冲区由 DAW 管理。

## 工程保存的范围

保存／恢复机型、输出增益、限幅器、重采样模式和模拟输出模式。每个插件实例的设置独立，不回写独立播放器的配置文件。

原 `88lib::HardwareDevice` 尚未实现完整的硬件状态序列化，因此本版**不保存完整硬件 RAM、正在发声的音符、面板编辑后的音色或全部控制器状态**。工程重新打开时会重新启动所选机型；请在 MIDI 轨开头保存并发送所需的 Bank Select、Program Change、CC 和 SysEx 初始化数据。ROM 仍需在本机数据目录中。

## 验证

使用原生 VST3 ABI 测试宿主直接加载最终 DLL，并使用 `sc_backup/sc8850` 的测试 ROM：

- 枚举四个输入总线，每个总线 16 通道。
- 检查四路所有通道共 8,384 个 CC／压力／弯音／Program Change 参数映射无冲突。
- 分别对 A–D 输入音符，均输出有限且非零的音频。
- 通过每路独立的 CC7 音量设置，确认另一端口的音符保持静音。
- VST3 工程状态保存／回读、编辑器视图和尺寸查询，以及非法端口／通道／控制器编号检查通过。

未逐一验证具体商业 DAW 对多个 VST3 事件输入总线的界面支持。

## 源码与构建

`88lib` 实现硬件模拟，`88emuplayer/Emu88Processor` 负责宿主音频与 MIDI。`HostMidiPorts.h` 在 JUCE 的单一 MidiBuffer 接口之外传递四组输入；`vst3Ports.cmake` 在构建目录生成仅用于本产品的 VST3 包装代码，不修改 JUCE 子模块源码或其他插件。

安装 Visual Studio C++ Build Tools 和其 CMake 组件，准备 `doc/88emu_vst3_dependencies.json` 记录的依赖版本（DSP56300、FreeType 的嵌套子模块也必须初始化），然后在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_88vst3.ps1 -BuildTests
```

产物：`bin/plugins/Release/VST3/88emuPlayer.vst3`。

测试程序：`temp/cmake_88vst3/source/ronaldo/88emu/88emuplayer/Release/88emuVst3_test.exe`，参数为包内 `Contents/x86_64-win/88emuPlayer.vst3` 的绝对路径。测试要求数据目录中有 SC-8850 ROM，并在 `config/88emuPlayer.xml` 中设置 `deviceModel=3`。可通过进程环境变量 `TUS_DATA_FOLDER` 指向测试根目录；其下应有 `The Usual Suspects/88emuPlayer/roms` 和 `config`。

本次构建使用 VS 2026 v145 工具集，未验证 Windows 7；建议在 Windows 10/11 x64 使用。许可证见随附 LICENSE.md。
