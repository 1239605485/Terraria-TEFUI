# TEFUI v0.1.9

本版修复 Android UI 坐标与拖动。

- 屏幕尺寸优先读取 `XNAUnityRunner.ScreenWidth/ScreenHeight`。
- 按 `UIScale` 转成 Terraria UI 坐标。
- 菜单强制限制在屏幕安全区域，避免左右裁切。
- 手指持续按住 TEFUI 即可开始拖动，不再要求恰好捕获触摸按下第一帧。
- 保留原生 `GUIStringButton` 点击与 `GUISlider` 滑块输入。

上传整个工程到 GitHub 后运行 `Build TEFUI Android ARM64` 即可编译。
