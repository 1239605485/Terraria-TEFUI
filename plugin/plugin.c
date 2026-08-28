#include "tefplugin/tpf_core.h"
#include "tefui_api.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct tefui_option_entry_t {
    bool used;
    tefui_option_snapshot_t value;
} tefui_option_entry_t;

static tefui_option_entry_t g_options[TEFUI_MAX_OPTIONS];
static int g_next_handle = 1;

static void copy_text(char *destination, size_t capacity, const char *source) {
    if (!destination || capacity == 0) return;
    if (!source) source = "";
    snprintf(destination, capacity, "%s", source);
}

static bool valid_id(const char *text) {
    return text && text[0] != '\0';
}

static tefui_option_entry_t *find_option(const char *owner_id, const char *option_id) {
    if (!valid_id(owner_id) || !valid_id(option_id)) return NULL;
    for (int i = 0; i < TEFUI_MAX_OPTIONS; ++i) {
        tefui_option_entry_t *entry = &g_options[i];
        if (entry->used && strcmp(entry->value.owner_id, owner_id) == 0 &&
            strcmp(entry->value.option_id, option_id) == 0) {
            return entry;
        }
    }
    return NULL;
}

static tefui_option_entry_t *allocate_option(void) {
    for (int i = 0; i < TEFUI_MAX_OPTIONS; ++i) {
        if (!g_options[i].used) {
            memset(&g_options[i], 0, sizeof(g_options[i]));
            g_options[i].used = true;
            g_options[i].value.handle = g_next_handle++;
            return &g_options[i];
        }
    }
    return NULL;
}

static float clamp_float(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float quantize_float(float value, float minimum, float maximum, float step) {
    value = clamp_float(value, minimum, maximum);
    if (step <= 0.0f) return value;
    const float ticks = roundf((value - minimum) / step);
    return clamp_float(minimum + ticks * step, minimum, maximum);
}

int tefui_get_api_version_impl(void) {
    return TEFUI_API_VERSION;
}

int tefui_register_toggle_impl(const char *owner_id, const char *option_id,
                               const char *label, bool default_value) {
    if (!valid_id(owner_id) || !valid_id(option_id) || !valid_id(label)) return -1;
    tefui_option_entry_t *entry = find_option(owner_id, option_id);
    const bool is_new = entry == NULL;
    if (is_new) entry = allocate_option();
    if (!entry) return -1;

    entry->value.type = TEFUI_OPTION_TOGGLE;
    copy_text(entry->value.owner_id, sizeof(entry->value.owner_id), owner_id);
    copy_text(entry->value.option_id, sizeof(entry->value.option_id), option_id);
    copy_text(entry->value.label, sizeof(entry->value.label), label);
    if (is_new) entry->value.bool_value = default_value;
    return entry->value.handle;
}

int tefui_register_slider_impl(const char *owner_id, const char *option_id,
                               const char *label, float min_value, float max_value,
                               float step, float default_value) {
    if (!valid_id(owner_id) || !valid_id(option_id) || !valid_id(label) ||
        !isfinite(min_value) || !isfinite(max_value) || !isfinite(step) ||
        !isfinite(default_value) || max_value <= min_value || step <= 0.0f) {
        return -1;
    }

    tefui_option_entry_t *entry = find_option(owner_id, option_id);
    const bool is_new = entry == NULL;
    if (is_new) entry = allocate_option();
    if (!entry) return -1;

    entry->value.type = TEFUI_OPTION_SLIDER;
    copy_text(entry->value.owner_id, sizeof(entry->value.owner_id), owner_id);
    copy_text(entry->value.option_id, sizeof(entry->value.option_id), option_id);
    copy_text(entry->value.label, sizeof(entry->value.label), label);
    entry->value.min_value = min_value;
    entry->value.max_value = max_value;
    entry->value.step = step;
    if (is_new) {
        entry->value.float_value = quantize_float(default_value, min_value, max_value, step);
    } else {
        entry->value.float_value = quantize_float(entry->value.float_value,
                                                  min_value, max_value, step);
    }
    return entry->value.handle;
}

bool tefui_unregister_owner_impl(const char *owner_id) {
    if (!valid_id(owner_id)) return false;
    bool removed = false;
    for (int i = 0; i < TEFUI_MAX_OPTIONS; ++i) {
        if (g_options[i].used && strcmp(g_options[i].value.owner_id, owner_id) == 0) {
            memset(&g_options[i], 0, sizeof(g_options[i]));
            removed = true;
        }
    }
    return removed;
}

int tefui_get_option_count_impl(void) {
    int count = 0;
    for (int i = 0; i < TEFUI_MAX_OPTIONS; ++i) {
        if (g_options[i].used) ++count;
    }
    return count;
}

bool tefui_get_option_snapshot_impl(int visible_index, tefui_option_snapshot_t *output) {
    if (visible_index < 0 || !output) return false;
    int current = 0;
    for (int i = 0; i < TEFUI_MAX_OPTIONS; ++i) {
        if (!g_options[i].used) continue;
        if (current++ == visible_index) {
            *output = g_options[i].value;
            return true;
        }
    }
    return false;
}

bool tefui_get_bool_impl(const char *owner_id, const char *option_id, bool *output) {
    tefui_option_entry_t *entry = find_option(owner_id, option_id);
    if (!entry || entry->value.type != TEFUI_OPTION_TOGGLE || !output) return false;
    *output = entry->value.bool_value;
    return true;
}

bool tefui_set_bool_impl(const char *owner_id, const char *option_id, bool value) {
    tefui_option_entry_t *entry = find_option(owner_id, option_id);
    if (!entry || entry->value.type != TEFUI_OPTION_TOGGLE) return false;
    entry->value.bool_value = value;
    return true;
}

bool tefui_get_float_impl(const char *owner_id, const char *option_id, float *output) {
    tefui_option_entry_t *entry = find_option(owner_id, option_id);
    if (!entry || entry->value.type != TEFUI_OPTION_SLIDER || !output) return false;
    *output = entry->value.float_value;
    return true;
}

bool tefui_set_float_impl(const char *owner_id, const char *option_id, float value) {
    tefui_option_entry_t *entry = find_option(owner_id, option_id);
    if (!entry || entry->value.type != TEFUI_OPTION_SLIDER || !isfinite(value)) return false;
    entry->value.float_value = quantize_float(value, entry->value.min_value,
                                              entry->value.max_value, entry->value.step);
    return true;
}

static const tpf_plugin_info_t g_info = {
    .pkg_id = "eternal.future.tefui.api",
    .name = "TEFUI API",
    .author = "TEFUI Contributors",
    .version = "0.1.0",
    .version_code = 100
};

static bool plugin_initialize(plugin_handle_t *this_handle) {
    memset(g_options, 0, sizeof(g_options));
    g_next_handle = 1;

    bool ok = true;
    ok &= tpf_register_symbol(this_handle, "tefui_get_api_version",
                              (const void *)tefui_get_api_version_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_register_toggle",
                              (const void *)tefui_register_toggle_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_register_slider",
                              (const void *)tefui_register_slider_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_unregister_owner",
                              (const void *)tefui_unregister_owner_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_get_option_count",
                              (const void *)tefui_get_option_count_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_get_option_snapshot",
                              (const void *)tefui_get_option_snapshot_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_get_bool",
                              (const void *)tefui_get_bool_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_set_bool",
                              (const void *)tefui_set_bool_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_get_float",
                              (const void *)tefui_get_float_impl);
    ok &= tpf_register_symbol(this_handle, "tefui_set_float",
                              (const void *)tefui_set_float_impl);
    return ok;
}

static void plugin_cleanup(plugin_handle_t *this_handle) {
    (void)this_handle;
    memset(g_options, 0, sizeof(g_options));
}

static const tpf_plugin_info_t *plugin_get_info(void) {
    return &g_info;
}

static const tpf_plugin_ops_t g_ops = {
    .initialize = plugin_initialize,
    .cleanup = plugin_cleanup,
    .get_info = plugin_get_info
};

API_EXPORT const tpf_plugin_ops_t *API_CALL tpf_create_plugin(void) {
    return &g_ops;
}
