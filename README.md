# TEFUI v0.1.3

这是与 Resource Saver 相同结构的 KernelLoader Mod 源码工程。

## GitHub 自动编译

1. 将压缩包内容上传到 GitHub 仓库根目录。
2. 打开 Actions，运行 `Build TEFUI Android ARM64`。
3. 下载 `TEFUI-v0.1.0-android-arm64` artifact。
4. 得到的 ZIP 根目录会直接包含 `Info.json`、`Manifest.json`、`mymod.json` 和 `Resources/lib/libTEFUI.android.arm64.so`，可导入 TEFManager。

首次真机测试应确认：进入世界后出现 `TEFUI` 按钮；点击后显示测试开关、测试滑块和关闭按钮。
