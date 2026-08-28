#include "tefplugin/tpf_core.h"
#include "tefui_api.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef const tpf_plugin_ops_t *(*create_plugin_fn)(void);

static tefui_get_api_version_fn api_version;
static tefui_register_toggle_fn register_toggle;
static tefui_register_slider_fn register_slider;
static tefui_unregister_owner_fn unregister_owner;
static tefui_get_option_count_fn option_count;
static tefui_get_option_snapshot_fn option_snapshot;
static tefui_get_bool_fn get_bool;
static tefui_set_bool_fn set_bool;
static tefui_get_float_fn get_float;
static tefui_set_float_fn set_float;

static bool capture_symbol(plugin_handle_t *handle, const char *name, const void *address) {
    (void)handle;
    if (strcmp(name, "tefui_get_api_version") == 0) api_version = (tefui_get_api_version_fn)address;
    else if (strcmp(name, "tefui_register_toggle") == 0) register_toggle = (tefui_register_toggle_fn)address;
    else if (strcmp(name, "tefui_register_slider") == 0) register_slider = (tefui_register_slider_fn)address;
    else if (strcmp(name, "tefui_unregister_owner") == 0) unregister_owner = (tefui_unregister_owner_fn)address;
    else if (strcmp(name, "tefui_get_option_count") == 0) option_count = (tefui_get_option_count_fn)address;
    else if (strcmp(name, "tefui_get_option_snapshot") == 0) option_snapshot = (tefui_get_option_snapshot_fn)address;
    else if (strcmp(name, "tefui_get_bool") == 0) get_bool = (tefui_get_bool_fn)address;
    else if (strcmp(name, "tefui_set_bool") == 0) set_bool = (tefui_set_bool_fn)address;
    else if (strcmp(name, "tefui_get_float") == 0) get_float = (tefui_get_float_fn)address;
    else if (strcmp(name, "tefui_set_float") == 0) set_float = (tefui_set_float_fn)address;
    else return false;
    return true;
}

int main(void) {
    tpf_register_symbol = capture_symbol;
    const tpf_plugin_ops_t *ops = tpf_create_plugin();
    assert(ops && ops->initialize && ops->cleanup && ops->get_info);

    plugin_handle_t *fake_handle = (plugin_handle_t *)(uintptr_t)1;
    assert(ops->initialize(fake_handle));
    assert(api_version && api_version() == TEFUI_API_VERSION);
    assert(register_toggle && register_slider && unregister_owner && option_count);
    assert(option_snapshot && get_bool && set_bool && get_float && set_float);

    assert(register_toggle("test.owner", "toggle", "Toggle", true) > 0);
    assert(register_slider("test.owner", "slider", "Slider", 0.0f, 10.0f, 0.5f, 4.2f) > 0);
    assert(option_count() == 2);

    bool toggle = false;
    assert(get_bool("test.owner", "toggle", &toggle) && toggle);
    assert(set_bool("test.owner", "toggle", false));
    assert(get_bool("test.owner", "toggle", &toggle) && !toggle);

    float slider = 0.0f;
    assert(get_float("test.owner", "slider", &slider) && slider == 4.0f);
    assert(set_float("test.owner", "slider", 8.26f));
    assert(get_float("test.owner", "slider", &slider) && slider == 8.5f);

    tefui_option_snapshot_t snapshot;
    assert(option_snapshot(0, &snapshot));
    assert(strcmp(snapshot.owner_id, "test.owner") == 0);
    assert(unregister_owner("test.owner"));
    assert(option_count() == 0);

    ops->cleanup(fake_handle);
    puts("TEFUI registry tests passed");
    return 0;
}
