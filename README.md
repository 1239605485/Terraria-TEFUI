# TEFUI 0.1.0

TEFUI is a two-component in-game menu foundation for Terraria mobile on
TEFKernel:

- `eternal.future.tefui.api` is a TEF Plugin. It owns the option registry and
  injects a versioned C API into Modules and KernelLoader Mods.
- `eternal.future.tefui.runtime` is a TEF Module. It hooks Terraria's
  `Main.DrawVirtualControls()` with a postfix and draws native-styled controls.

This first milestone intentionally contains no combat or gameplay hooks. It
registers one test toggle and one test slider so input can be validated before
feature Mods are connected.

## Safety properties

- It never skips or replaces `Main.DrawVirtualControls()`.
- It does not hook `PlayerInput.UpdateInput()`.
- It does not modify Terraria's fixed virtual-control enums or arrays.
- Hooks are installed once and removed during module cleanup.
- Feature state is stored by the API Plugin and read by stable owner/option IDs.

## Build

Host-side syntax and Linux test build:

```sh
git clone --depth 1 https://github.com/eternalfuture-e38299/TEFKernel.git vendor/tefkernel
make check
make
make test
```

Android builds must use the Android NDK compiler and produce these exact names:

```text
libplugin.android.arm64.so
libmodule.android.arm64.so
```

Both components include `tef_api_imp.c`: the API Plugin consumes TPF's injected
`tpf_register_symbol` pointer, while the Runtime Module consumes PatchLib.

## Install layout

Package and install the components separately:

```text
plugin/pkg/eternal.future.tefui.api.tefpkg
module/pkg/eternal.future.tefui.runtime.tefpkg
```

Enable the Runtime Module. It declares `eternal.future.tefui.api` as a plugin
dependency, allowing TEFKernel to load the API first.

## Connecting a future KernelLoader Mod

Include `include/tefui_api.h` once with `TEFUI_DEFINE_IMPORTS` in the consumer's
implementation file. KernelLoader registers each loaded Mod library with TPF,
so the API Plugin fills the exported function pointers before `init_mod()`.

```c
#define TEFUI_DEFINE_IMPORTS
#include "tefui_api.h"

static void init_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    if (!tefui_get_api_version || tefui_get_api_version() != TEFUI_API_VERSION)
        return;

    tefui_register_toggle(
        "example.mod",
        "enabled",
        "Example feature",
        true
    );
}

static void cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    if (tefui_unregister_owner) tefui_unregister_owner("example.mod");
}
```

Feature hooks should query the stored value and return normally when disabled;
they must not install/uninstall gameplay hooks in response to clicks.

## Current limitations

- The exact visual result can only be verified in the target Android game.
- Labels are converted to managed strings while drawing; a later milestone
  should add safe managed-string rooting/caching.
- Persistence, scrolling, categories, and localization are deferred until the
  minimal touch test passes.
