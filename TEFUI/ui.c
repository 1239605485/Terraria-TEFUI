#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/string.h"
#include "tefkernel/patchlib/type.h"

#define TEFUI_DEFINE_IMPORTS
#include "tefui_api.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int tefui_get_api_version_impl(void);
int tefui_register_toggle_impl(const char *, const char *, const char *, bool);
int tefui_register_slider_impl(const char *, const char *, const char *, float, float, float, float);
bool tefui_unregister_owner_impl(const char *);
int tefui_get_option_count_impl(void);
bool tefui_get_option_snapshot_impl(int, tefui_option_snapshot_t *);
bool tefui_get_bool_impl(const char *, const char *, bool *);
bool tefui_set_bool_impl(const char *, const char *, bool);
bool tefui_get_float_impl(const char *, const char *, float *);
bool tefui_set_float_impl(const char *, const char *, float);

typedef struct vector2_t {
    float x;
    float y;
} vector2_t;

enum {
    CONTROL_ANCHOR_SCREEN = 0,
    ANCHOR_TOP_CENTRE = 10,
    ANCHOR_CENTRE_BOTH = 18
};

static patch_hook_id_t g_draw_hook = PATCH_HOOK_INVALID_ID;
static patch_handle_t g_settings_layout_type = PATCH_NULL;
static patch_handle_t g_settings_instance_field = PATCH_NULL;
static patch_handle_t g_settings_title_field = PATCH_NULL;
static patch_handle_t g_settings_slider_template_field = PATCH_NULL;
static patch_handle_t g_slider_option_field = PATCH_NULL;

static patch_handle_t g_string_button_type = PATCH_NULL;
static patch_handle_t g_string_button_draw = PATCH_NULL;
static patch_handle_t g_string_button_layout_type = PATCH_NULL;
static patch_handle_t g_button_location_field = PATCH_NULL;
static patch_handle_t g_button_size_field = PATCH_NULL;
static patch_handle_t g_button_anchor_control_field = PATCH_NULL;
static patch_handle_t g_button_anchor_field = PATCH_NULL;

static patch_handle_t g_gui_slider_type = PATCH_NULL;
static patch_handle_t g_gui_slider_draw = PATCH_NULL;
static patch_handle_t g_slider_layout_type = PATCH_NULL;
static patch_handle_t g_slider_location_field = PATCH_NULL;
static patch_handle_t g_slider_anchor_control_field = PATCH_NULL;
static patch_handle_t g_slider_anchor_field = PATCH_NULL;
static patch_handle_t g_drag_state_type = PATCH_NULL;
static patch_handle_t g_drag_state = PATCH_NULL;

static bool g_menu_open = false;
static bool g_launcher_down = false;
static bool g_close_down = false;
static bool g_option_down[TEFUI_MAX_OPTIONS];
static bool g_runtime_ready = false;

static bool valid_handle(patch_handle_t handle) {
    return handle && patchlib_is_valid(handle);
}

static bool api_ready(void) {
    return tefui_get_api_version && tefui_register_toggle && tefui_register_slider &&
           tefui_unregister_owner && tefui_get_option_count &&
           tefui_get_option_snapshot && tefui_set_bool && tefui_set_float &&
           tefui_get_api_version() == TEFUI_API_VERSION;
}

static patch_handle_t get_settings_instance(void) {
    if (!valid_handle(g_settings_instance_field)) return PATCH_NULL;
    patch_handle_t instance = PATCH_NULL;
    patchlib_field_get_value(g_settings_instance_field, PATCH_NULL, &instance);
    return instance;
}

static patch_handle_t get_borrowed_button_layout(void) {
    patch_handle_t settings = get_settings_instance();
    if (!settings || !valid_handle(g_settings_title_field)) return PATCH_NULL;
    patch_handle_t layout = PATCH_NULL;
    patchlib_field_get_value(g_settings_title_field, settings, &layout);
    return layout;
}

static patch_handle_t get_borrowed_slider_layout(void) {
    patch_handle_t settings = get_settings_instance();
    if (!settings || !valid_handle(g_settings_slider_template_field) ||
        !valid_handle(g_slider_option_field)) return PATCH_NULL;

    patch_handle_t slider_template = PATCH_NULL;
    patch_handle_t slider_layout = PATCH_NULL;
    patchlib_field_get_value(g_settings_slider_template_field, settings, &slider_template);
    if (!slider_template) return PATCH_NULL;
    patchlib_field_get_value(g_slider_option_field, slider_template, &slider_layout);
    return slider_layout;
}

static bool draw_text_button(patch_handle_t layout, const char *text,
                             float x, float y, float width, float height,
                             int anchor) {
    if (!layout || !text || !valid_handle(g_string_button_draw)) return false;

    vector2_t old_location = {0};
    vector2_t old_size = {0};
    int old_anchor_control = 0;
    int old_anchor = 0;
    patchlib_field_get_value(g_button_location_field, layout, &old_location);
    patchlib_field_get_value(g_button_size_field, layout, &old_size);
    patchlib_field_get_value(g_button_anchor_control_field, layout, &old_anchor_control);
    patchlib_field_get_value(g_button_anchor_field, layout, &old_anchor);

    vector2_t location = {x, y};
    vector2_t size = {width, height};
    int anchor_control = CONTROL_ANCHOR_SCREEN;
    patchlib_field_set_value(g_button_location_field, layout, &location);
    patchlib_field_set_value(g_button_size_field, layout, &size);
    patchlib_field_set_value(g_button_anchor_control_field, layout, &anchor_control);
    patchlib_field_set_value(g_button_anchor_field, layout, &anchor);

    patch_handle_t managed_text = patchlib_string_create(text);
    bool disabled = false;
    bool clicked = false;
    bool forced_pressed = false;
    float stable_scale = 1.0f;
    /* ref float is an actual float* argument. libffi therefore needs the
       address of storage containing that pointer, not the float storage. */
    float *scale_ref = &stable_scale;
    void *args[] = {&layout, &managed_text, &scale_ref, &forced_pressed, &disabled};
    patchlib_method_invoke_args(g_string_button_draw, PATCH_NULL, &clicked, args);

    patchlib_field_set_value(g_button_location_field, layout, &old_location);
    patchlib_field_set_value(g_button_size_field, layout, &old_size);
    patchlib_field_set_value(g_button_anchor_control_field, layout, &old_anchor_control);
    patchlib_field_set_value(g_button_anchor_field, layout, &old_anchor);
    return clicked;
}

static bool draw_slider(patch_handle_t layout, float x, float y, int anchor,
                        float *normalized_value) {
    if (!layout || !normalized_value || !g_drag_state || !valid_handle(g_gui_slider_draw)) {
        return false;
    }

    vector2_t old_location = {0};
    int old_anchor_control = 0;
    int old_anchor = 0;
    patchlib_field_get_value(g_slider_location_field, layout, &old_location);
    patchlib_field_get_value(g_slider_anchor_control_field, layout, &old_anchor_control);
    patchlib_field_get_value(g_slider_anchor_field, layout, &old_anchor);

    vector2_t location = {x, y};
    int anchor_control = CONTROL_ANCHOR_SCREEN;
    patchlib_field_set_value(g_slider_location_field, layout, &location);
    patchlib_field_set_value(g_slider_anchor_control_field, layout, &anchor_control);
    patchlib_field_set_value(g_slider_anchor_field, layout, &anchor);

    bool disable_pick = false;
    patch_handle_t null_backing_handler = PATCH_NULL;
    bool force_over = false;
    int min_value = 0;
    int max_value = 100;
    bool ignore_start_point = false;
    bool changed = false;
    float *value_ref = normalized_value;
    void *args[] = {
        &layout, &disable_pick, &value_ref, &g_drag_state, &null_backing_handler,
        &force_over, &min_value, &max_value, &ignore_start_point
    };
    patchlib_method_invoke_args(g_gui_slider_draw, PATCH_NULL, &changed, args);

    patchlib_field_set_value(g_slider_location_field, layout, &old_location);
    patchlib_field_set_value(g_slider_anchor_control_field, layout, &old_anchor_control);
    patchlib_field_set_value(g_slider_anchor_field, layout, &old_anchor);
    return changed;
}

static bool pressed_once(bool pressed, bool *was_pressed) {
    const bool fire = pressed && !*was_pressed;
    *was_pressed = pressed;
    return fire;
}

static void draw_menu(void) {
    patch_handle_t button_layout = get_borrowed_button_layout();
    if (!button_layout) return;

    const bool launcher_pressed = draw_text_button(
        button_layout, g_menu_open ? "TEFUI [X]" : "TEFUI",
        0.0f, 118.0f, 170.0f, 46.0f, ANCHOR_TOP_CENTRE);
    if (pressed_once(launcher_pressed, &g_launcher_down)) {
        g_menu_open = !g_menu_open;
    }
    if (!g_menu_open || !api_ready()) return;

    patch_handle_t slider_layout = get_borrowed_slider_layout();
    const int option_count = tefui_get_option_count();
    float y = -120.0f;

    for (int i = 0; i < option_count && i < TEFUI_MAX_OPTIONS; ++i) {
        tefui_option_snapshot_t option;
        memset(&option, 0, sizeof(option));
        if (!tefui_get_option_snapshot(i, &option)) continue;

        char line[TEFUI_LABEL_CAPACITY + 48];
        if (option.type == TEFUI_OPTION_TOGGLE) {
            snprintf(line, sizeof(line), "%s: %s", option.label,
                     option.bool_value ? "ON" : "OFF");
            const bool option_pressed = draw_text_button(
                button_layout, line, 0.0f, y, 420.0f, 46.0f, ANCHOR_CENTRE_BOTH);
            if (pressed_once(option_pressed, &g_option_down[i])) {
                tefui_set_bool(option.owner_id, option.option_id, !option.bool_value);
            }
            y += 54.0f;
        } else if (option.type == TEFUI_OPTION_SLIDER) {
            snprintf(line, sizeof(line), "%s: %.1f", option.label, option.float_value);
            (void)draw_text_button(button_layout, line, 0.0f, y, 420.0f, 40.0f,
                                   ANCHOR_CENTRE_BOTH);
            y += 42.0f;
            if (slider_layout) {
                float range = option.max_value - option.min_value;
                float normalized = range > 0.0f
                    ? (option.float_value - option.min_value) / range
                    : 0.0f;
                if (normalized < 0.0f) normalized = 0.0f;
                if (normalized > 1.0f) normalized = 1.0f;
                if (draw_slider(slider_layout, 0.0f, y, ANCHOR_CENTRE_BOTH,
                                &normalized)) {
                    const float value = option.min_value + normalized * range;
                    tefui_set_float(option.owner_id, option.option_id, value);
                }
                y += 52.0f;
            }
        }
    }

    const bool close_pressed = draw_text_button(
        button_layout, "Close", 0.0f, y, 180.0f, 44.0f, ANCHOR_CENTRE_BOTH);
    if (pressed_once(close_pressed, &g_close_down)) {
        g_menu_open = false;
    }
}

static void draw_virtual_controls_postfix(patch_handle_t instance, void **args,
                                          void *result,
                                          const patch_method_signature_t *signature) {
    (void)instance;
    (void)args;
    (void)result;
    (void)signature;
    if (g_runtime_ready) draw_menu();
}

static bool resolve_runtime(void) {
    patch_handle_t main_type = patchlib_type_get_type("Terraria", "Main");
    patch_handle_t draw_virtual_controls = main_type
        ? patchlib_type_get_method_by_param_count(main_type, "DrawVirtualControls", 0)
        : PATCH_NULL;

    g_settings_layout_type = patchlib_type_get_type("", "SettingsOverlay_Layout");
    g_string_button_type = patchlib_type_get_type("", "GUIStringButton");
    g_string_button_layout_type = patchlib_type_get_type("", "StringButton_Layout");
    g_gui_slider_type = patchlib_type_get_type("", "GUISlider");
    g_slider_layout_type = patchlib_type_get_type("", "Slider_Layout");
    g_drag_state_type = g_gui_slider_type
        ? patchlib_type_get_inner_type(g_gui_slider_type, "DragState")
        : PATCH_NULL;
    patch_handle_t settings_slider_layout_type =
        patchlib_type_get_type("", "SettingsOverlaySlider_Layout");

    if (!valid_handle(draw_virtual_controls) || !valid_handle(g_settings_layout_type) ||
        !valid_handle(g_string_button_type) || !valid_handle(g_string_button_layout_type) ||
        !valid_handle(g_gui_slider_type) || !valid_handle(g_slider_layout_type) ||
        !valid_handle(g_drag_state_type) || !valid_handle(settings_slider_layout_type)) {
        return false;
    }

    g_settings_instance_field = patchlib_type_get_field(g_settings_layout_type, "Instance");
    g_settings_title_field = patchlib_type_get_field(g_settings_layout_type, "Titlewide1");
    g_settings_slider_template_field =
        patchlib_type_get_field(g_settings_layout_type, "SliderTemplate");
    g_slider_option_field = patchlib_type_get_field(settings_slider_layout_type, "Option");

    const char *button_args[] = {"layout", "value", "scale", "forcedPressed", "buttonDisabled"};
    g_string_button_draw = patchlib_type_get_method_by_param_names(
        g_string_button_type, "DrawButton", 5, button_args);
    g_button_location_field = patchlib_type_get_field(g_string_button_layout_type, "Location");
    g_button_size_field = patchlib_type_get_field(g_string_button_layout_type, "Size");
    g_button_anchor_control_field =
        patchlib_type_get_field(g_string_button_layout_type, "AnchorControl");
    g_button_anchor_field = patchlib_type_get_field(g_string_button_layout_type, "Anchor");

    const char *slider_args[] = {"layout", "disablePick", "value", "dragState",
                                 "backingHandler", "forceOver", "minValue",
                                 "maxValue", "ignoreStartPoint"};
    g_gui_slider_draw = patchlib_type_get_method_by_param_names(
        g_gui_slider_type, "Draw", 9, slider_args);
    g_slider_location_field = patchlib_type_get_field(g_slider_layout_type, "Location");
    g_slider_anchor_control_field =
        patchlib_type_get_field(g_slider_layout_type, "AnchorControl");
    g_slider_anchor_field = patchlib_type_get_field(g_slider_layout_type, "Anchor");
    g_drag_state = patchlib_type_new_instance(g_drag_state_type);

    if (!valid_handle(g_settings_instance_field) || !valid_handle(g_settings_title_field) ||
        !valid_handle(g_settings_slider_template_field) || !valid_handle(g_slider_option_field) ||
        !valid_handle(g_string_button_draw) || !valid_handle(g_button_location_field) ||
        !valid_handle(g_button_size_field) || !valid_handle(g_button_anchor_control_field) ||
        !valid_handle(g_button_anchor_field) || !valid_handle(g_gui_slider_draw) ||
        !valid_handle(g_slider_location_field) ||
        !valid_handle(g_slider_anchor_control_field) ||
        !valid_handle(g_slider_anchor_field) || !g_drag_state) {
        return false;
    }

    g_draw_hook = patchlib_install_prepost_hook(draw_virtual_controls, NULL,
                                                draw_virtual_controls_postfix);
    return g_draw_hook != PATCH_HOOK_INVALID_ID;
}

bool tefui_ui_initialize(void) {
    tefui_get_api_version = tefui_get_api_version_impl;
    tefui_register_toggle = tefui_register_toggle_impl;
    tefui_register_slider = tefui_register_slider_impl;
    tefui_unregister_owner = tefui_unregister_owner_impl;
    tefui_get_option_count = tefui_get_option_count_impl;
    tefui_get_option_snapshot = tefui_get_option_snapshot_impl;
    tefui_get_bool = tefui_get_bool_impl;
    tefui_set_bool = tefui_set_bool_impl;
    tefui_get_float = tefui_get_float_impl;
    tefui_set_float = tefui_set_float_impl;
    memset(g_option_down, 0, sizeof(g_option_down));
    g_launcher_down = false;
    g_close_down = false;

    if (!api_ready()) {
        fprintf(stderr, "[TEFUI] API plugin symbols are unavailable or incompatible.\n");
        return false;
    }

    tefui_register_toggle("tefui.demo", "test_toggle", "Test toggle", true);
    tefui_register_slider("tefui.demo", "test_slider", "Test slider",
                          0.0f, 100.0f, 1.0f, 50.0f);

    g_runtime_ready = resolve_runtime();
    if (!g_runtime_ready) {
        tefui_unregister_owner("tefui.demo");
        fprintf(stderr, "[TEFUI] Failed to resolve Terraria mobile UI methods.\n");
    }
    return g_runtime_ready;
}

bool tefui_ui_cleanup(void) {
    g_runtime_ready = false;
    g_menu_open = false;
    if (g_draw_hook != PATCH_HOOK_INVALID_ID) {
        patchlib_uninstall_hook(g_draw_hook);
        g_draw_hook = PATCH_HOOK_INVALID_ID;
    }
    if (tefui_unregister_owner) tefui_unregister_owner("tefui.demo");
    return true;
}
