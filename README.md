# TEFUI Plugin（第一版）

这是一个面向 TEFKernel 的 Android Plugin 原型，目标是让其他 KernelLoader Mod 注册功能开关，并在游戏内提供一个入口按钮和控制面板。

> 重要：必须使用本项目 Actions 重新生成的 `TEFUI-plugin` 包。旧版把裸 `.so` 放在外层 ZIP 中，TEFManager 可以接收但 TEFKernel 无法加载。

## 当前功能

- 游戏进程内显示可拖动的 `T` 悬浮按钮；
- 点击按钮展开“TEF 模组控制”面板；
- 其他模组可注册多个功能开关；
- 开关变化时回调模组自己的 C 函数；
- 不需要 `SYSTEM_ALERT_WINDOW` 权限：UI 添加到当前 Activity 的 `decorView`；
- Android arm64-v8a。

## Mod 接入示例

```c
#include "tefui/tefui_api.h"

static void on_changed(const char *id, bool enabled, void *user_data) {
    (void)id;
    (void)user_data;
    /* 在这里启用/禁用你的 Hook 或功能。 */
    my_mod_set_enabled(enabled);
}

void my_mod_init(void) {
    tefui_register_feature(
            "com.example.mymod.feature",
            "我的模组功能",
            true,
            on_changed,
            NULL);
}

void my_mod_cleanup(void) {
    tefui_unregister_feature("com.example.mymod.feature");
}
```

由于 TEFKernel 的跨动态库 API 使用函数指针表，Mod 自己的 `tef_api_imp.c` 也要包含一次这个头文件（必须位于 `#define TEF_API_IMPL 1` 之后）：

```c
#include "tefui/tefui_api.h"
```

构建 Mod 时，把本目录的 `include/` 加入 include path，并把 `include/tefui/tefui_api.h` 复制到 Mod 的 API 目录，或直接将本目录作为额外 include path。

如果你的 Mod 通过 KernelLoader 加载，`tefui_*` 函数会由 Plugin 通过 TPF 符号机制注入。调用前可以用 `tefui_is_ui_available()` 判断当前 Android UI bridge 是否已就绪；即使返回 false，注册表 API 仍然可以使用。

## 构建

先准备与目标 ABI 匹配的 TEFKernel 头文件，然后配置：

```text
cmake -S . -B build -DTEFKERNEL_DIR=/path/to/TEFKernel-main
cmake --build build --config Release
```

构建流程会先生成 `tefui.android.arm64-v8a.so`，再使用 TEFPkg-Tool 将它打包为 Kernel 可以加载的 `tefui.tefpkg`。最终交给 TEFManager 安装的是外层 `TEFUI-plugin.zip`，不是裸 `.so`。

项目已经附带 `.github/workflows/build.yml`。上传整个目录到 GitHub 后，在仓库的 Actions 页面手动运行 `Build TEFUI Plugin`，或推送到 `main` 分支即可自动编译。工作流会分别输出两个 Artifact：

- `tefui-plugin-android-arm64`：下载得到的 ZIP 可直接交给 TEFManager 安装，根目录含 `Manifest.json`；
- `tefloader-dex-with-tefui-bridge`：需要合入 TEFManager/loader 的 `tefloader.dex`。

请不要把两个 Artifact 再合并后交给 TEFManager；`tefloader.dex` 不属于插件包。

`android-bridge/src/.../TefUiBridge.java` 需要复制到 `TEFKernel-main/tefloader/android-java/src/eternal/future/tefkernel/`，再加入 TEFKernel loader 的 `tefloader.dex`。这是因为 Native Plugin 只能加载进程内的共享库，不能自行把 Java 类注入宿主 ClassLoader。加入后重新生成 `tefloader.dex`，再把它替换到 TEFManager 的 Android assets 中；现有 `build.sh` 会自动查找并编译这个 Java 文件，不需要改构建脚本。

## 包结构

```text
TEFUI.tefpkg
├── Manifest.json
├── Info.json
└── Resources/
    └── lib/
        └── tefui.android.arm64-v8a.so
```

这是第一步原型：开关状态目前是进程内状态，暂未持久化；面板暂时显示在所有当前 Activity 上，也暂未加入设置页、搜索、开关分组或自定义面板内容。
