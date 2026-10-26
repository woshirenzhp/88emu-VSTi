# 88emuPlayer VSTHost Program Change 兼容修复

Windows x64 VST3，四路 MIDI 输入，每路 16 通道。此目录中的普通版不写诊断日志，ROM 配置方式与之前版本一致，包内不含 ROM。

## 修复内容

实际宿主日志表明，VSTHost 将 Program Change 作为 `DataEvent/kMidiSysEx` 发送，内容是 `Cn pp 00`。旧转换路径按 SysEx 检查 F0/F7，导致 PC 被丢弃。

新版本兼容 `Cn pp` 与 `Cn pp 00`，校验长度及数据字节范围，保留端口与采样偏移。当相同 PC 在同端口、同采样位置通过参数通道和 DataEvent 同时发送时，只投递一次。正常 SysEx 路径保持原样；DataEvent 内的 CC 不额外转换，避免与宿主参数通道重复。

宿主日志中 34 条 SysEx 本来就已进入模拟器。另一个现象是宿主将原文件两路端口合并到 bus=0；输入中已经丢失的端口标记无法由此修复恢复。需要宿主正确路由至 A/B/C/D。

## 使用

退出宿主，重新加载本包的 `88emuPlayer.vst3`。插件 ID 与之前版本相同，请确认宿主实际选用了新文件路径。x86 VSTHost 仍需要 x64 桥接支持。

另提供 `88emuPlayer-VST3-Win64-Fixed-Debug.zip` 日志诊断版。将其放在可写目录，日志 `88emu-midi.log` 会出现在 `.vst3` 插件包旁边，与宿主工作目录无关。诊断版启用同步写盘，不用于性能测试。

## 复现构建

普通版：`scripts/build_88vst3.ps1 -BuildTests`

日志诊断版：`scripts/build_88vst3.ps1 -BuildTests -MidiTraceFile 88emu-midi.log`

诊断版是启用日志的优化构建，不依赖 MSVC Debug CRT。两种构建使用相同的 MIDI 修复代码。
