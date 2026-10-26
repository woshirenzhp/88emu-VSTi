# 88emuPlayer MIDI 诊断版

这是开启 MIDI 日志的 Windows x64 VST3，使用 Release 优化构建；用于排查宿主的 Program Change / SysEx 接收问题，不是 MSVC Debug CRT 构建。保留四路 MIDI 输入。

## 加载与日志

1. 卸载宿主中已有的 88emuPlayer 实例，退出并重新启动宿主，避免缓存旧 DLL。
2. 加载本包 `88emuPlayer.vst3`。它与普通版使用同一插件 ID，宿主中名称仍为 `88emuPlayer`，请按文件路径选取诊断版。
3. 按原来的设置选择 ROM 和型号。本包不含 ROM。
4. 从 MIDI 文件开头播放，尤其不要跳过开头的 GS Reset 和音色选择消息。
5. 日志自动追加到插件包旁边的 `88emu-midi.log`。例如插件位于 `D:\测试\88emuPlayer.vst3`，日志就在 `D:\测试\88emu-midi.log`。平铺的单文件插件则写在 DLL 所在目录。与宿主工作目录无关。

这是 x64 插件。在 x86 VSTHost 中仍需要宿主的 x64 桥接支持；本包不包含新的 x86 插件或桥接程序。

需要其他日志位置时，在启动宿主前设置环境变量 `EMU88_MIDI_TRACE_PATH` 为完整文件路径。桥接进程也必须继承这个变量。请将诊断插件解压到可写目录；如果放在受保护的 Program Files 下，默认日志可能无法写入。

## 日志含义

| 标记 | 含义 |
| --- | --- |
| `TRACE_START` | 诊断代码已开始写日志，含构建日期与日志路径 |
| `PROCESSOR_START` | 插件处理器创建，含实例地址 |
| `PREPARE` | 宿主启动音频处理，含采样率与块大小 |
| `PC_MAP` | 宿主查询 Program Change 参数映射；仅查询不代表已发送换音色 |
| `PARAM` | 宿主向音频处理提交了参数队列，含参数 ID 与点数 |
| `RAW_DATA` | 活跃有效端口上的原始 VST3 DataEvent，含子类型、总长度及前 256 字节 |
| `RAW_PC` | 兼容转换 DataEvent 内的 Program Change；duplicate=1 表示同端口、同采样位置、相同 PC 已通过参数通道传入，故不重复投递 |
| `EVENT` | 转换前的非音符 VST3 事件类型、端口和采样偏移 |
| `DELIVER` | 消息已送到模拟器入口，包含完整 SysEx 或 Program Change 字节 |

`port=0/1/2/3` 对应 A/B/C/D。`source=3` 表示宿主输入。`PC_MAP channel` 是合并后的通道索引 0–63。每行 `time_ms` 是 Unix 毫秒时间戳。

修复版兼容 DataEvent 中的两字节 `Cn pp` 和补零三字节 `Cn pp 00`，保留时间偏移和端口。仅兼容已确认有问题的 Program Change 格式；不额外转换 DataEvent 中的 CC，以免重复执行宿主同时通过参数通道提交的 CC。正常 F0/F7 SysEx 仍走原来的转换路径。

有 `RAW_DATA` 而没有对应的 `DELIVER`，可重点核对消息类型、F0/F7 封装及转换路径；只有 `PC_MAP` 没有相应参数队列，并不能说明宿主实际发送过 Program Change。完全没有日志时先检查是否加载了本包 DLL，以及日志目录写入权限。多个实例会共用日志，诊断时建议仅保留一个实例。宿主已合并到 bus=0 的消息无法在插件内恢复原始 MIDI 文件端口。

日志会同步写盘并立即刷新，因此诊断版可能增加音频线程延迟。收集到复现日志后换回普通版。日志是追加方式；要清空旧记录，请先退出宿主再删除或改名日志文件。

## 构建与验证

在已配置的构建目录中设置 CMake 变量 `EMU88_MIDI_TRACE_FILE` 为日志完整路径，再构建 `88emuplayer_VST3`。将该变量设为空可关闭日志。

无头测试通过 VST3 ABI 提交 Program Change 参数及带 F0/F7 的 SysEx，再核对 `DELIVER` 的字节和端口。此测试不是 VSTHost 1.57 自身的 VST3 实现；提供的 `vsthost-main` 源码只有旧版 VST2 路径。
