#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/string.h"
#include "tefkernel/patchlib/type.h"
#include "mod_logger.h"

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
    /* This is the anchor value used by v0.1.1, the last build proven visible
       on the target Terraria 1.4.5.6.4 Android build. */
    ANCHOR_TOP_LEFT = 9
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

/* Optional helpers. None of these are allowed to make UI initialization fail. */
static patch_handle_t g_screen_width_getter = PATCH_NULL;
static patch_handle_t g_screen_height_getter = PATCH_NULL;
static patch_handle_t g_mouse_x_getter = PATCH_NULL;
static patch_handle_t g_mouse_y_getter = PATCH_NULL;
static patch_handle_t g_mouse_left_getter = PATCH_NULL;
static patch_handle_t g_ui_scale_getter = PATCH_NULL;

static bool g_menu_open = false;
static float g_launcher_scale = 1.0f;
static float g_close_scale = 1.0f;
static float g_option_scales[TEFUI_MAX_OPTIONS];
static bool g_launcher_down = false;
static bool g_close_down = false;
static bool g_option_down[TEFUI_MAX_OPTIONS];

static float g_launcher_x_ratio = 0.5f;
static float g_launcher_y_ratio = 0.5f;
static bool g_pointer_was_down = false;
static bool g_dragging_launcher = false;
static bool g_launcher_moved = false;
static float g_drag_start_x = 0.0f;
static float g_drag_start_y = 0.0f;
static bool g_runtime_ready = false;
static bool g_logged_first_draw = false;
static bool g_logged_missing_layout = false;

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

/* Keep the persistent scale state used by the proven-visible v0.1.1 build.
   Click debouncing is handled separately, so the old repeated-click twitch is gone. */
static bool draw_text_button(patch_handle_t layout, const char *text,
                             float x, float y, float width, float height,
                             float *scale, bool forced_pressed) {
    if (!layout || !text || !scale || !valid_handle(g_string_button_draw)) return false;

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
    int anchor = ANCHOR_TOP_LEFT;
    patchlib_field_set_value(g_button_location_field, layout, &location);
    patchlib_field_set_value(g_button_size_field, layout, &size);
    patchlib_field_set_value(g_button_anchor_control_field, layout, &anchor_control);
    patchlib_field_set_value(g_button_anchor_field, layout, &anchor);

    patch_handle_t managed_text = patchlib_string_create(text);
    bool disabled = false;
    bool clicked = false;
    float *scale_ref = scale;
    if (managed_text) {
        void *args[] = {&layout, &managed_text, &scale_ref, &forced_pressed, &disabled};
        patchlib_method_invoke_args(g_string_button_draw, PATCH_NULL, &clicked, args);
    }

    patchlib_field_set_value(g_button_location_field, layout, &old_location);
    patchlib_field_set_value(g_button_size_field, layout, &old_size);
    patchlib_field_set_value(g_button_anchor_control_field, layout, &old_anchor_control);
    patchlib_field_set_value(g_button_anchor_field, layout, &old_anchor);
    return clicked;
}

static bool draw_slider(patch_handle_t layout, float x, float y,
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
    int anchor = ANCHOR_TOP_LEFT;
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

static bool read_static_int(patch_handle_t getter, int *output) {
    return output && valid_handle(getter) &&
           patchlib_method_invoke_args(getter, PATCH_NULL, output, NULL);
}

static bool read_static_bool(patch_handle_t getter, bool *output) {
    return output && valid_handle(getter) &&
           patchlib_method_invoke_args(getter, PATCH_NULL, output, NULL);
}

static bool read_static_float(patch_handle_t getter, float *output) {
    return output && valid_handle(getter) &&
           patchlib_method_invoke_args(getter, PATCH_NULL, output, NULL);
}

static float clampf_local(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* Terraria renders its GUI in UI coordinates while screenWidth/screenHeight are
   real-screen coordinates on mobile. UIScale converts between the two. */
static void get_ui_metrics(float *ui_width, float *ui_height, float *ui_scale,
                           bool *has_screen_metrics) {
    float width = 960.0f;
    float height = 540.0f;
    float scale = 1.0f;
    bool ok = false;

    int raw_width = 0;
    int raw_height = 0;
    if (read_static_int(g_screen_width_getter, &raw_width) &&
        read_static_int(g_screen_height_getter, &raw_height) &&
        raw_width > 0 && raw_height > 0) {
        float candidate_scale = 1.0f;
        if (!read_static_float(g_ui_scale_getter, &candidate_scale) ||
            !isfinite(candidate_scale) || candidate_scale < 0.5f || candidate_scale > 4.0f) {
            candidate_scale = 1.0f;
        }
        scale = candidate_scale;
        width = (float)raw_width / scale;
        height = (float)raw_height / scale;
        if (!isfinite(width) || !isfinite(height) || width < 320.0f || height < 180.0f ||
            width > 10000.0f || height > 10000.0f) {
            width = (float)raw_width;
            height = (float)raw_height;
            scale = 1.0f;
        }
        ok = true;
    }

    if (ui_width) *ui_width = width;
    if (ui_height) *ui_height = height;
    if (ui_scale) *ui_scale = scale;
    if (has_screen_metrics) *has_screen_metrics = ok;
}

static bool get_pointer(float ui_scale, float *x, float *y, bool *down) {
    int raw_x = 0;
    int raw_y = 0;
    bool raw_down = false;
    if (!read_static_int(g_mouse_x_getter, &raw_x) ||
        !read_static_int(g_mouse_y_getter, &raw_y) ||
        !read_static_bool(g_mouse_left_getter, &raw_down)) {
        return false;
    }
    if (!isfinite(ui_scale) || ui_scale <= 0.0f) ui_scale = 1.0f;
    if (x) *x = (float)raw_x / ui_scale;
    if (y) *y = (float)raw_y / ui_scale;
    if (down) *down = raw_down;
    return true;
}

static bool point_in_button(float px, float py, float x, float y, float width, float height) {
    /* StringButton_Layout Location behaves as the button centre with this anchor
       on the target build, as demonstrated by the v0.1.1 left-edge clipping. */
    return px >= x - width * 0.5f && px <= x + width * 0.5f &&
           py >= y - height * 0.5f && py <= y + height * 0.5f;
}

static void draw_menu(void) {
    patch_handle_t button_layout = get_borrowed_button_layout();
    if (!button_layout) {
        if (!g_logged_missing_layout && mod_logger_write) {
            mod_logger_write(MOD_LOG_LEVEL_WARNING, "TEFUI",
                             "Draw hook is running but SettingsOverlay button layout is unavailable");
            g_logged_missing_layout = true;
        }
        return;
    }

    float ui_width = 960.0f;
    float ui_height = 540.0f;
    float ui_scale = 1.0f;
    bool has_screen_metrics = false;
    get_ui_metrics(&ui_width, &ui_height, &ui_scale, &has_screen_metrics);

    float launcher_x = g_launcher_x_ratio * ui_width;
    float launcher_y = g_launcher_y_ratio * ui_height;
    launcher_x = clampf_local(launcher_x, 95.0f, fmaxf(95.0f, ui_width - 95.0f));
    launcher_y = clampf_local(launcher_y, 30.0f, fmaxf(30.0f, ui_height - 30.0f));

    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    bool pointer_down = false;
    const bool has_pointer = get_pointer(ui_scale, &pointer_x, &pointer_y, &pointer_down);
    bool suppress_release_click = false;

    if (has_pointer) {
        if (pointer_down && !g_pointer_was_down &&
            point_in_button(pointer_x, pointer_y, launcher_x, launcher_y, 190.0f, 46.0f)) {
            g_dragging_launcher = true;
            g_launcher_moved = false;
            g_drag_start_x = pointer_x;
            g_drag_start_y = pointer_y;
        }
        if (pointer_down && g_dragging_launcher) {
            const float move_x = pointer_x - g_drag_start_x;
            const float move_y = pointer_y - g_drag_start_y;
            if (move_x * move_x + move_y * move_y > 64.0f) g_launcher_moved = true;
            g_launcher_x_ratio = clampf_local(pointer_x / ui_width, 0.04f, 0.96f);
            g_launcher_y_ratio = clampf_local(pointer_y / ui_height, 0.05f, 0.95f);
            launcher_x = g_launcher_x_ratio * ui_width;
            launcher_y = g_launcher_y_ratio * ui_height;
        }
        if (!pointer_down && g_pointer_was_down && g_dragging_launcher) {
            suppress_release_click = g_launcher_moved;
            if (!g_launcher_moved) g_menu_open = !g_menu_open;
            g_dragging_launcher = false;
            g_launcher_moved = false;
        }
        g_pointer_was_down = pointer_down;
    }

    /* Keep the launcher ASCII-only so it remains visible even if a custom font
       pack has not yet injected CJK glyphs. The actual menu contents are Chinese. */
    const bool launcher_pressed = draw_text_button(
        button_layout, g_menu_open ? "TEFUI [X]" : "TEFUI",
        launcher_x, launcher_y, 190.0f, 46.0f, &g_launcher_scale, false);

    /* Fallback interaction for builds where mouse getters are unavailable. */
    if (!has_pointer && pressed_once(launcher_pressed, &g_launcher_down) &&
        !suppress_release_click) {
        g_menu_open = !g_menu_open;
    } else if (has_pointer) {
        g_launcher_down = false;
    }

    if (!g_logged_first_draw) {
        if (mod_logger_write) {
            mod_logger_write(MOD_LOG_LEVEL_INFO, "TEFUI",
                             "First draw: ui=%.1fx%.1f scale=%.3f metrics=%d pointer=%d launcher=(%.1f,%.1f)",
                             ui_width, ui_height, ui_scale,
                             has_screen_metrics ? 1 : 0, has_pointer ? 1 : 0,
                             launcher_x, launcher_y);
        }
        g_logged_first_draw = true;
    }

    if (!g_menu_open || !api_ready()) return;

    patch_handle_t slider_layout = get_borrowed_slider_layout();
    const int option_count = tefui_get_option_count();
    const float menu_x = ui_width * 0.5f;
    float y = ui_height * 0.28f;

    /* Chinese menu header. */
    float header_scale = 1.0f;
    (void)draw_text_button(button_layout, "模组菜单", menu_x, y, 360.0f, 42.0f,
                           &header_scale, false);
    y += 48.0f;

    for (int i = 0; i < option_count && i < TEFUI_MAX_OPTIONS; ++i) {
        tefui_option_snapshot_t option;
        memset(&option, 0, sizeof(option));
        if (!tefui_get_option_snapshot(i, &option)) continue;

        char line[TEFUI_LABEL_CAPACITY + 48];
        if (option.type == TEFUI_OPTION_TOGGLE) {
            snprintf(line, sizeof(line), "%s：%s", option.label,
                     option.bool_value ? "开启" : "关闭");
            const bool option_pressed = draw_text_button(
                button_layout, line, menu_x, y, 460.0f, 46.0f,
                &g_option_scales[i], option.bool_value);
            if (pressed_once(option_pressed, &g_option_down[i])) {
                tefui_set_bool(option.owner_id, option.option_id, !option.bool_value);
            }
            y += 54.0f;
        } else if (option.type == TEFUI_OPTION_SLIDER) {
            snprintf(line, sizeof(line), "%s：%.1f", option.label, option.float_value);
            (void)draw_text_button(button_layout, line, menu_x, y, 460.0f, 40.0f,
                                   &g_option_scales[i], false);
            y += 42.0f;
            if (slider_layout) {
                float range = option.max_value - option.min_value;
                float normalized = range > 0.0f
                    ? (option.float_value - option.min_value) / range
                    : 0.0f;
                normalized = clampf_local(normalized, 0.0f, 1.0f);
                if (draw_slider(slider_layout, menu_x, y, &normalized)) {
                    const float value = option.min_value + normalized * range;
                    tefui_set_float(option.owner_id, option.option_id, value);
                }
                y += 52.0f;
            }
        }
    }

    const bool close_pressed = draw_text_button(
        button_layout, "关闭菜单", menu_x, y, 200.0f, 44.0f,
        &g_close_scale, false);
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

    /* Optional mobile metrics/input. They improve centering/dragging but are not
       dependencies of drawing. This prevents the v0.1.5 regression. */
    g_screen_width_getter = main_type
        ? patchlib_type_get_method_by_param_count(main_type, "get_screenWidth", 0)
        : PATCH_NULL;
    g_screen_height_getter = main_type
        ? patchlib_type_get_method_by_param_count(main_type, "get_screenHeight", 0)
        : PATCH_NULL;
    g_mouse_x_getter = main_type
        ? patchlib_type_get_method_by_param_count(main_type, "get_mouseX", 0)
        : PATCH_NULL;
    g_mouse_y_getter = main_type
        ? patchlib_type_get_method_by_param_count(main_type, "get_mouseY", 0)
        : PATCH_NULL;
    g_mouse_left_getter = main_type
        ? patchlib_type_get_method_by_param_count(main_type, "get_mouseLeft", 0)
        : PATCH_NULL;
    g_ui_scale_getter = main_type
        ? patchlib_type_get_method_by_param_count(main_type, "get_UIScale", 0)
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
        if (mod_logger_write) {
            mod_logger_write(MOD_LOG_LEVEL_ERROR, "TEFUI",
                             "Required Terraria UI type/method resolution failed");
        }
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
        if (mod_logger_write) {
            mod_logger_write(MOD_LOG_LEVEL_ERROR, "TEFUI",
                             "Required Terraria UI field/method resolution failed");
        }
        return false;
    }

    g_draw_hook = patchlib_install_prepost_hook(draw_virtual_controls, NULL,
                                                draw_virtual_controls_postfix);
    if (mod_logger_write) {
        mod_logger_write(g_draw_hook != PATCH_HOOK_INVALID_ID ? MOD_LOG_LEVEL_INFO : MOD_LOG_LEVEL_ERROR,
                         "TEFUI",
                         "DrawVirtualControls hook %s; screen=%d mouse=%d UIScale=%d",
                         g_draw_hook != PATCH_HOOK_INVALID_ID ? "installed" : "failed",
                         valid_handle(g_screen_width_getter) && valid_handle(g_screen_height_getter),
                         valid_handle(g_mouse_x_getter) && valid_handle(g_mouse_y_getter) &&
                             valid_handle(g_mouse_left_getter),
                         valid_handle(g_ui_scale_getter));
    }
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
    for (int i = 0; i < TEFUI_MAX_OPTIONS; ++i) g_option_scales[i] = 1.0f;
    g_launcher_scale = 1.0f;
    g_close_scale = 1.0f;
    g_launcher_down = false;
    g_close_down = false;
    g_pointer_was_down = false;
    g_dragging_launcher = false;
    g_launcher_moved = false;
    g_launcher_x_ratio = 0.5f;
    g_launcher_y_ratio = 0.5f;
    g_logged_first_draw = false;
    g_logged_missing_layout = false;

    if (!api_ready()) {
        if (mod_logger_write) {
            mod_logger_write(MOD_LOG_LEVEL_ERROR, "TEFUI",
                             "API plugin symbols are unavailable or incompatible");
        }
        return false;
    }

    tefui_register_toggle("tefui.demo", "test_toggle", "测试开关", true);
    tefui_register_slider("tefui.demo", "test_slider", "测试滑块",
                          0.0f, 100.0f, 1.0f, 50.0f);

    g_runtime_ready = resolve_runtime();
    if (!g_runtime_ready) {
        tefui_unregister_owner("tefui.demo");
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
