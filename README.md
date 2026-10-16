# SMC Mixer Tool

Read-only macOS tool for the M-VAVE SMC-Mixer. It reads device identity and the stored settings of every assignable control over USB-C or Bluetooth.

只读。连接调音台后，点旋钮、推子或按钮，查看该控件在快照里的配置。一次读取 6,282 字节，里面是 DAW 和 CC / User 两份存档。右上角两个按钮只切换正在查看的那一份，不改变机器当前模式。机器上的切换是按住 Shift，再按 Left 或 Right。

诊断页可以导出这份原始快照。厂商、型号、电量和信号强度只在蓝牙连接时提供。

写入配置还没做，设计记在 [PLAN.md](PLAN.md)。

## 构建

需要 macOS 12、CMake 3.22 和 Xcode 命令行工具。JUCE 9.0.4 由 CMake 拉取，使用它自己的许可证。

```
cmake -S . -B build
cmake --build build --target SMCMixerGUI
open "build/SMCMixerGUI_artefacts/Debug/SMC Mixer Tool.app"
```

## 许可

本仓库源码以 Apache License 2.0 授权。见 [LICENSE](LICENSE)。
