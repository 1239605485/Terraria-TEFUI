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
static patch_handle_t g_runner_screen_width_getter = PATCH_NULL;
static patch_handle_t g_runner_screen_height_getter = PATCH_NULL;

/* Main.screenWidth/screenHeight are temporarily rewritten while Terraria draws
   parts of the mobile UI. PlayerInput exposes the real back-buffer dimensions,
   which are the stable values we need for centering and touch conversion. */
static patch_handle_t g_real_screen_width_getter = PATCH_NULL;
static patch_handle_t g_real_screen_height_getter = PATCH_NULL;

/* Terraria's own CursorManager already converts touch/mouse input into Cursor
   reference objects. Using Cursor objects is important on Android: unlike
   InControl.TouchManager.GetTouch(), this path never returns a value-type Touch
   struct through TEFKernel's FFI layer. */
static patch_handle_t g_cursor_manager_instance_field = PATCH_NULL;
static patch_handle_t g_cursor_get_num_cursors = PATCH_NULL;
static patch_handle_t g_cursor_get_cursor = PATCH_NULL;
static patch_handle_t g_cursor_position_field = PATCH_NULL;
static patch_handle_t g_cursor_id_field = PATCH_NULL;
static patch_handle_t g_cursor_down_field = PATCH_NULL;
static patch_handle_t g_cursor_was_down_field = PATCH_NULL;
static patch_handle_t g_cursor_ignore_field = PATCH_NULL;

static bool g_menu_open = false;
static float g_launcher_scale = 1.0f;
static float g_close_scale = 1.0f;
static float g_option_scales[TEFUI_MAX_OPTIONS];
static bool g_launcher_down = false;
static bool g_close_down = false;
static bool g_option_down[TEFUI_MAX_OPTIONS];

/* The target Android build reliably draws the borrowed Terraria layouts near
   the left side.  Do not place the launcher in the centre: that coordinate can
   be swallowed by the virtual-controls pass on some phones. */
static float g_launcher_x_ratio = 0.14f;
static float g_launcher_y_ratio = 0.34f;
static bool g_pointer_was_down = false;
static bool g_dragging_launcher = false;
static bool g_launcher_moved = false;
static float g_drag_start_x = 0.0f;
static float g_drag_start_y = 0.0f;
static float g_drag_offset_x = 0.0f;
static float g_drag_offset_y = 0.0f;
static int g_active_cursor_id = -2147483647;
static int g_cursor_transform = -1;
static int g_native_click_suppression_frames = 0;
static bool g_runtime_ready = false;
static bool g_logged_first_draw = false;
static bool g_logged_missing_layout = false;
static bool g_logged_cursor_input = false;

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

/* At DrawVirtualControls time Main.screenWidth/screenHeight can be a temporary
   virtual value. Prefer PlayerInput.RealScreenWidth/Height, then convert the
   physical back-buffer to the coordinate space used by Terraria's GUI. */
static void get_ui_metrics(float *ui_width, float *ui_height, float *ui_scale,
                           int *physical_width, int *physical_height,
                           bool *has_physical_metrics) {
    float width = 960.0f;
    float height = 540.0f;
    float scale = 1.0f;
    int physical_w = 0;
    int physical_h = 0;
    bool physical_ok = false;

    float candidate_scale = 1.0f;
    if (!read_static_float(g_ui_scale_getter, &candidate_scale) ||
        !isfinite(candidate_scale) || candidate_scale < 0.25f || candidate_scale > 4.0f) {
        candidate_scale = 1.0f;
    }
    scale = candidate_scale;

    /* XNAUnityRunner owns the stable Android render surface. Main.screenWidth
       and PlayerInput.RealScreenWidth may be temporary/virtual during this hook. */
    int runner_width = 0;
    int runner_height = 0;
    if (read_static_int(g_runner_screen_width_getter, &runner_width) &&
        read_static_int(g_runner_screen_height_getter, &runner_height) &&
        runner_width >= 640 && runner_height >= 320) {
        physical_w = runner_width;
        physical_h = runner_height;
        physical_ok = true;
        width = (float)runner_width / scale;
        height = (float)runner_height / scale;
    } else {
        int real_width = 0;
        int real_height = 0;
        if (read_static_int(g_real_screen_width_getter, &real_width) &&
            read_static_int(g_real_screen_height_getter, &real_height) &&
            real_width >= 320 && real_height >= 180) {
            physical_w = real_width;
            physical_h = real_height;
            physical_ok = true;
            width = (float)real_width / scale;
            height = (float)real_height / scale;
        } else {
            int main_width = 0;
            int main_height = 0;
            if (read_static_int(g_screen_width_getter, &main_width) &&
                read_static_int(g_screen_height_getter, &main_height) &&
                main_width >= 320 && main_height >= 180) {
                width = (float)main_width;
                height = (float)main_height;
                scale = 1.0f;
            }
        }
    }

    if (!isfinite(width) || !isfinite(height) || width < 480.0f || height < 240.0f ||
        width > 10000.0f || height > 10000.0f) {
        if (physical_ok && physical_w >= 640 && physical_h >= 320) {
            width = (float)physical_w;
            height = (float)physical_h;
            scale = 1.0f;
        } else {
            width = 960.0f;
            height = 540.0f;
            scale = 1.0f;
            physical_w = 0;
            physical_h = 0;
            physical_ok = false;
        }
    }

    if (ui_width) *ui_width = width;
    if (ui_height) *ui_height = height;
    if (ui_scale) *ui_scale = scale;
    if (physical_width) *physical_width = physical_w;
    if (physical_height) *physical_height = physical_h;
    if (has_physical_metrics) *has_physical_metrics = physical_ok;
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

typedef struct cursor_sample_t {
    int id;
    vector2_t position;
    bool down;
    bool was_down;
    bool ignore;
} cursor_sample_t;

static bool point_in_button(float px, float py, float x, float y,
                            float width, float height);

enum {
    /* Cursor.Position is usually already top-left screen/UI space. Keep scaled
       and Y-flipped fallbacks because mobile builds may expose physical pixels. */
    CURSOR_MAP_RAW_TOP_LEFT = 0,
    CURSOR_MAP_SCALED_TOP_LEFT = 1,
    CURSOR_MAP_RAW_BOTTOM_LEFT = 2,
    CURSOR_MAP_SCALED_BOTTOM_LEFT = 3
};

static bool cursor_input_ready(void) {
    return valid_handle(g_cursor_manager_instance_field) &&
           valid_handle(g_cursor_get_num_cursors) && valid_handle(g_cursor_get_cursor) &&
           valid_handle(g_cursor_position_field) && valid_handle(g_cursor_id_field) &&
           valid_handle(g_cursor_down_field) && valid_handle(g_cursor_was_down_field);
}

static patch_handle_t get_cursor_manager(void) {
    if (!cursor_input_ready()) return PATCH_NULL;
    patch_handle_t manager = PATCH_NULL;
    patchlib_field_get_value(g_cursor_manager_instance_field, PATCH_NULL, &manager);
    return manager;
}

static int get_cursor_count(patch_handle_t manager) {
    if (!manager || !cursor_input_ready()) return 0;
    int count = 0;
    if (!patchlib_method_invoke_args(g_cursor_get_num_cursors, manager, &count, NULL)) return 0;
    if (count < 0) return 0;
    if (count > 16) count = 16;
    return count;
}

static bool get_cursor_sample(patch_handle_t manager, int index, cursor_sample_t *sample) {
    if (!manager || !sample || index < 0 || !cursor_input_ready()) return false;

    patch_handle_t cursor = PATCH_NULL;
    void *args[] = {&index};
    if (!patchlib_method_invoke_args(g_cursor_get_cursor, manager, &cursor, args) || !cursor) {
        return false;
    }

    memset(sample, 0, sizeof(*sample));
    patchlib_field_get_value(g_cursor_id_field, cursor, &sample->id);
    patchlib_field_get_value(g_cursor_position_field, cursor, &sample->position);
    patchlib_field_get_value(g_cursor_down_field, cursor, &sample->down);
    patchlib_field_get_value(g_cursor_was_down_field, cursor, &sample->was_down);
    if (valid_handle(g_cursor_ignore_field)) {
        patchlib_field_get_value(g_cursor_ignore_field, cursor, &sample->ignore);
    }
    return isfinite(sample->position.x) && isfinite(sample->position.y);
}

static bool map_cursor_position(const cursor_sample_t *sample, int transform,
                                float ui_width, float ui_height,
                                int physical_width, int physical_height,
                                float *x, float *y) {
    if (!sample || !x || !y || ui_width <= 0.0f || ui_height <= 0.0f) return false;

    float mapped_x = sample->position.x;
    float mapped_y = sample->position.y;
    if (transform == CURSOR_MAP_SCALED_TOP_LEFT ||
        transform == CURSOR_MAP_SCALED_BOTTOM_LEFT) {
        if (physical_width <= 0 || physical_height <= 0) return false;
        mapped_x = sample->position.x * ui_width / (float)physical_width;
        if (transform == CURSOR_MAP_SCALED_TOP_LEFT) {
            mapped_y = sample->position.y * ui_height / (float)physical_height;
        } else {
            mapped_y = ((float)physical_height - sample->position.y) *
                       ui_height / (float)physical_height;
        }
    } else if (transform == CURSOR_MAP_RAW_BOTTOM_LEFT) {
        const float h = physical_height > 0 ? (float)physical_height : ui_height;
        mapped_y = h - sample->position.y;
    }

    if (!isfinite(mapped_x) || !isfinite(mapped_y)) return false;
    *x = mapped_x;
    *y = mapped_y;
    return true;
}

static bool choose_cursor_transform(const cursor_sample_t *sample,
                                    float launcher_x, float launcher_y,
                                    float ui_width, float ui_height,
                                    int physical_width, int physical_height,
                                    int *transform, float *x, float *y) {
    if (!sample || !transform || !x || !y) return false;

    const int candidates[] = {
        CURSOR_MAP_RAW_TOP_LEFT,
        CURSOR_MAP_SCALED_TOP_LEFT,
        CURSOR_MAP_RAW_BOTTOM_LEFT,
        CURSOR_MAP_SCALED_BOTTOM_LEFT
    };
    float best_distance = 1.0e30f;
    bool found = false;
    int best_transform = -1;
    float best_x = 0.0f;
    float best_y = 0.0f;

    for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        float px = 0.0f;
        float py = 0.0f;
        if (!map_cursor_position(sample, candidates[i], ui_width, ui_height,
                                 physical_width, physical_height, &px, &py)) {
            continue;
        }
        if (!point_in_button(px, py, launcher_x, launcher_y, 220.0f, 70.0f)) continue;
        const float dx = px - launcher_x;
        const float dy = py - launcher_y;
        const float distance = dx * dx + dy * dy;
        if (!found || distance < best_distance) {
            found = true;
            best_distance = distance;
            best_transform = candidates[i];
            best_x = px;
            best_y = py;
        }
    }

    if (!found) return false;
    *transform = best_transform;
    *x = best_x;
    *y = best_y;
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
    int physical_width = 0;
    int physical_height = 0;
    bool has_physical_metrics = false;
    get_ui_metrics(&ui_width, &ui_height, &ui_scale,
                   &physical_width, &physical_height, &has_physical_metrics);

    float launcher_x = g_launcher_x_ratio * ui_width;
    float launcher_y = g_launcher_y_ratio * ui_height;
    launcher_x = clampf_local(launcher_x, 110.0f, fmaxf(110.0f, ui_width - 110.0f));
    launcher_y = clampf_local(launcher_y, 40.0f, fmaxf(40.0f, ui_height - 40.0f));

    /* Android-safe interaction path: Terraria CursorManager exposes reference
       Cursor objects with Position/Down/WasDown fields. This avoids the v0.1.7
       crash caused by invoking a method that returns an InControl.Touch struct. */
    patch_handle_t cursor_manager = get_cursor_manager();
    const int cursor_count = cursor_manager ? get_cursor_count(cursor_manager) : 0;
    bool tracked_cursor_found = false;
    bool cursor_released_this_frame = false;

    if (g_dragging_launcher && g_active_cursor_id != -2147483647) {
        for (int i = 0; i < cursor_count; ++i) {
            cursor_sample_t sample;
            if (!get_cursor_sample(cursor_manager, i, &sample) ||
                sample.id != g_active_cursor_id) {
                continue;
            }
            tracked_cursor_found = true;
            if (sample.down) {
                float pointer_x = 0.0f;
                float pointer_y = 0.0f;
                if (map_cursor_position(&sample, g_cursor_transform,
                                        ui_width, ui_height,
                                        physical_width, physical_height,
                                        &pointer_x, &pointer_y)) {
                    const float move_x = pointer_x - g_drag_start_x;
                    const float move_y = pointer_y - g_drag_start_y;
                    if (move_x * move_x + move_y * move_y > 144.0f) {
                        g_launcher_moved = true;
                    }

                    launcher_x = clampf_local(pointer_x + g_drag_offset_x,
                                              110.0f, fmaxf(110.0f, ui_width - 110.0f));
                    launcher_y = clampf_local(pointer_y + g_drag_offset_y,
                                              40.0f, fmaxf(40.0f, ui_height - 40.0f));
                    g_launcher_x_ratio = ui_width > 0.0f ? launcher_x / ui_width : 0.5f;
                    g_launcher_y_ratio = ui_height > 0.0f ? launcher_y / ui_height : 0.5f;
                }
            } else {
                cursor_released_this_frame = true;
            }
            break;
        }

        /* A released mobile cursor may disappear from the active cursor list in
           the same frame. Treat disappearance as release only after a drag has
           actually started, never as an error. */
        if (!tracked_cursor_found) cursor_released_this_frame = true;

        if (cursor_released_this_frame) {
            if (!g_launcher_moved) g_menu_open = !g_menu_open;
            g_dragging_launcher = false;
            g_launcher_moved = false;
            g_active_cursor_id = -2147483647;
            g_cursor_transform = -1;
            g_native_click_suppression_frames = 3;
        }
    }

    if (!g_dragging_launcher && !cursor_released_this_frame) {
        for (int i = 0; i < cursor_count; ++i) {
            cursor_sample_t sample;
            if (!get_cursor_sample(cursor_manager, i, &sample) || !sample.down) {
                continue;
            }

            int transform = -1;
            float pointer_x = 0.0f;
            float pointer_y = 0.0f;
            if (!choose_cursor_transform(&sample, launcher_x, launcher_y,
                                         ui_width, ui_height,
                                         physical_width, physical_height,
                                         &transform, &pointer_x, &pointer_y)) {
                continue;
            }

            g_dragging_launcher = true;
            g_launcher_moved = false;
            g_active_cursor_id = sample.id;
            g_cursor_transform = transform;
            g_drag_start_x = pointer_x;
            g_drag_start_y = pointer_y;
            g_drag_offset_x = launcher_x - pointer_x;
            g_drag_offset_y = launcher_y - pointer_y;
            g_launcher_down = false;

            if (!g_logged_cursor_input && mod_logger_write) {
                mod_logger_write(MOD_LOG_LEVEL_INFO, "TEFUI",
                                 "Cursor input acquired: id=%d map=%d pos=(%.1f,%.1f)",
                                 sample.id, transform, pointer_x, pointer_y);
                g_logged_cursor_input = true;
            }
            break;
        }
    }

    /* Desktop/compatibility fallback. On Android Main.mouseLeft may remain false,
       but keeping this path costs nothing and preserves mouse dragging. */
    if (!cursor_manager && !g_dragging_launcher) {
        float pointer_x = 0.0f;
        float pointer_y = 0.0f;
        bool pointer_down = false;
        const bool has_pointer = get_pointer(ui_scale, &pointer_x, &pointer_y, &pointer_down);
        if (has_pointer) {
            if (pointer_down && !g_pointer_was_down &&
                point_in_button(pointer_x, pointer_y, launcher_x, launcher_y, 220.0f, 70.0f)) {
                g_dragging_launcher = true;
                g_launcher_moved = false;
                g_drag_start_x = pointer_x;
                g_drag_start_y = pointer_y;
                g_drag_offset_x = launcher_x - pointer_x;
                g_drag_offset_y = launcher_y - pointer_y;
            }
            if (pointer_down && g_dragging_launcher) {
                const float move_x = pointer_x - g_drag_start_x;
                const float move_y = pointer_y - g_drag_start_y;
                if (move_x * move_x + move_y * move_y > 144.0f) g_launcher_moved = true;
                launcher_x = clampf_local(pointer_x + g_drag_offset_x,
                                          110.0f, fmaxf(110.0f, ui_width - 110.0f));
                launcher_y = clampf_local(pointer_y + g_drag_offset_y,
                                          40.0f, fmaxf(40.0f, ui_height - 40.0f));
                g_launcher_x_ratio = launcher_x / ui_width;
                g_launcher_y_ratio = launcher_y / ui_height;
            }
            if (!pointer_down && g_pointer_was_down && g_dragging_launcher) {
                if (!g_launcher_moved) g_menu_open = !g_menu_open;
                g_dragging_launcher = false;
                g_launcher_moved = false;
                g_native_click_suppression_frames = 3;
            }
            g_pointer_was_down = pointer_down;
        }
    }

    /* Keep the launcher ASCII-only so it remains visible even if a custom font
       pack has not yet injected CJK glyphs. The actual menu contents are Chinese. */
    const bool launcher_pressed = draw_text_button(
        button_layout, g_menu_open ? "TEFUI [X]" : "TEFUI",
        launcher_x, launcher_y, 190.0f, 46.0f, &g_launcher_scale,
        g_dragging_launcher || g_menu_open);

    /* Never discard GUIStringButton's own click result merely because mouse
       getters exist. That was the v0.1.6 tap regression on Android. Native GUI
       input is the fallback when TouchManager is unavailable and also keeps
       keyboard/mouse builds usable. */
    const bool native_click = pressed_once(launcher_pressed, &g_launcher_down);
    const bool suppress_native_click = g_dragging_launcher ||
        g_native_click_suppression_frames > 0;
    if (native_click && !suppress_native_click) {
        g_menu_open = !g_menu_open;
    }
    if (g_native_click_suppression_frames > 0) --g_native_click_suppression_frames;

    if (!g_logged_first_draw) {
        if (mod_logger_write) {
            mod_logger_write(MOD_LOG_LEVEL_INFO, "TEFUI",
                             "First draw: ui=%.1fx%.1f real=%dx%d scale=%.3f physical=%d cursor=%d launcher=(%.1f,%.1f)",
                             ui_width, ui_height, physical_width, physical_height, ui_scale,
                             has_physical_metrics ? 1 : 0, cursor_input_ready() ? 1 : 0,
                             launcher_x, launcher_y);
        }
        g_logged_first_draw = true;
    }

    if (!g_menu_open || !api_ready()) return;

    patch_handle_t slider_layout = get_borrowed_slider_layout();
    const int option_count = tefui_get_option_count();
    const float menu_half_width = 245.0f;
    /* Keep every borrowed control on the proven-visible left-side strip by
       default.  The launcher itself remains freely draggable after opening. */
    const float menu_x = clampf_local(menu_half_width + 20.0f, menu_half_width,
                                     fmaxf(menu_half_width, ui_width - menu_half_width));
    float estimated_height = 48.0f + 54.0f + 94.0f + 52.0f;
    if (option_count > 2) estimated_height += (float)(option_count - 2) * 60.0f;
    float y = ui_height * 0.22f;
    y = clampf_local(y, 55.0f, fmaxf(55.0f, ui_height - estimated_height - 35.0f));

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
    patch_handle_t xna_runner_type = patchlib_type_get_type("", "XNAUnityRunner");
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

    g_runner_screen_width_getter = xna_runner_type
        ? patchlib_type_get_method_by_param_count(xna_runner_type, "get_ScreenWidth", 0)
        : PATCH_NULL;
    g_runner_screen_height_getter = xna_runner_type
        ? patchlib_type_get_method_by_param_count(xna_runner_type, "get_ScreenHeight", 0)
        : PATCH_NULL;

    /* Stable screen metrics. Main.screenWidth/screenHeight are temporarily
       changed inside Terraria's mobile control draw path. */
    patch_handle_t player_input_type =
        patchlib_type_get_type("Terraria.GameInput", "PlayerInput");
    g_real_screen_width_getter = player_input_type
        ? patchlib_type_get_method_by_param_count(player_input_type, "get_RealScreenWidth", 0)
        : PATCH_NULL;
    g_real_screen_height_getter = player_input_type
        ? patchlib_type_get_method_by_param_count(player_input_type, "get_RealScreenHeight", 0)
        : PATCH_NULL;

    /* Use Terraria's CursorManager instead of InControl.TouchManager.GetTouch.
       CursorManager returns Cursor reference objects, so TEFKernel never has to
       marshal a value-type Touch struct (the v0.1.7 SIGABRT path). */
    patch_handle_t cursor_manager_type = patchlib_type_get_type("", "CursorManager");
    patch_handle_t cursor_type = patchlib_type_get_type("", "Cursor");
    g_cursor_manager_instance_field = cursor_manager_type
        ? patchlib_type_get_field(cursor_manager_type, "Instance")
        : PATCH_NULL;
    g_cursor_get_num_cursors = cursor_manager_type
        ? patchlib_type_get_method_by_param_count(cursor_manager_type, "GetNumCursors", 0)
        : PATCH_NULL;
    g_cursor_get_cursor = cursor_manager_type
        ? patchlib_type_get_method_by_param_count(cursor_manager_type, "GetCursor", 1)
        : PATCH_NULL;
    g_cursor_position_field = cursor_type
        ? patchlib_type_get_field(cursor_type, "Position")
        : PATCH_NULL;
    g_cursor_id_field = cursor_type
        ? patchlib_type_get_field(cursor_type, "Id")
        : PATCH_NULL;
    g_cursor_down_field = cursor_type
        ? patchlib_type_get_field(cursor_type, "Down")
        : PATCH_NULL;
    g_cursor_was_down_field = cursor_type
        ? patchlib_type_get_field(cursor_type, "WasDown")
        : PATCH_NULL;
    g_cursor_ignore_field = cursor_type
        ? patchlib_type_get_field(cursor_type, "Ignore")
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
                         "DrawVirtualControls hook %s; xnaScreen=%d realScreen=%d cursor=%d MainScreen=%d mouse=%d UIScale=%d",
                         g_draw_hook != PATCH_HOOK_INVALID_ID ? "installed" : "failed",
                         valid_handle(g_runner_screen_width_getter) &&
                             valid_handle(g_runner_screen_height_getter),
                         valid_handle(g_real_screen_width_getter) &&
                             valid_handle(g_real_screen_height_getter),
                         cursor_input_ready(),
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
    g_drag_start_x = 0.0f;
    g_drag_start_y = 0.0f;
    g_drag_offset_x = 0.0f;
    g_drag_offset_y = 0.0f;
    g_active_cursor_id = -2147483647;
    g_cursor_transform = -1;
    g_native_click_suppression_frames = 0;
    g_launcher_x_ratio = 0.14f;
    g_launcher_y_ratio = 0.34f;
    g_logged_first_draw = false;
    g_logged_missing_layout = false;
    g_logged_cursor_input = false;

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
    g_dragging_launcher = false;
    g_launcher_moved = false;
    g_active_cursor_id = -2147483647;
    g_cursor_transform = -1;
    g_native_click_suppression_frames = 0;
    if (g_draw_hook != PATCH_HOOK_INVALID_ID) {
        patchlib_uninstall_hook(g_draw_hook);
        g_draw_hook = PATCH_HOOK_INVALID_ID;
    }
    if (tefui_unregister_owner) tefui_unregister_owner("tefui.demo");
    return true;
}
