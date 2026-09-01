/*
 * TEFUI API: a small, ABI-stable feature switch registry for TEFKernel mods.
 * Include this file from a Mod after tef_api.h / mod_core.h.
 */
#ifndef TEFUI_API_H
#define TEFUI_API_H

#include "tef_api.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEFUI_API_VERSION 1

typedef void (API_CALL *tefui_feature_changed_callback_t)(
        const char *feature_id, bool enabled, void *user_data);

/*
 * Mod-owned feature switch. Strings are copied by the plugin during the call,
 * so the caller may release its temporary buffers after registration.
 */
#if defined(TEFUI_PLUGIN_BUILD)
#define TEFUI_FUNCTION(ret, name, ...) API_EXPORT ret API_CALL name(__VA_ARGS__)
#else
#define TEFUI_FUNCTION(ret, name, ...) DEFINE_FUNCTION(ret, name, __VA_ARGS__)
#endif

TEFUI_FUNCTION(bool, tefui_register_feature,
               const char *feature_id,
               const char *display_name,
               bool default_enabled,
               tefui_feature_changed_callback_t on_changed,
               void *user_data);

TEFUI_FUNCTION(bool, tefui_unregister_feature, const char *feature_id);
TEFUI_FUNCTION(bool, tefui_is_feature_enabled, const char *feature_id);
TEFUI_FUNCTION(bool, tefui_set_feature_enabled,
               const char *feature_id, bool enabled);
TEFUI_FUNCTION(size_t, tefui_get_feature_count);
TEFUI_FUNCTION(const char *, tefui_get_feature_id, size_t index);
TEFUI_FUNCTION(const char *, tefui_get_feature_name, size_t index);
TEFUI_FUNCTION(bool, tefui_is_ui_available);

#undef TEFUI_FUNCTION

#ifdef __cplusplus
}
#endif

#endif /* TEFUI_API_H */
