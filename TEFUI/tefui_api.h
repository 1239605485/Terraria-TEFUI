#ifndef TEFUI_API_H
#define TEFUI_API_H

#include <stdbool.h>
#include <stddef.h>

#if defined(_WIN32) || defined(_WIN64)
#define TEFUI_EXPORT __declspec(dllexport)
#else
#define TEFUI_EXPORT __attribute__((visibility("default")))
#endif

#define TEFUI_API_VERSION 1
#define TEFUI_MAX_OPTIONS 64
#define TEFUI_OWNER_ID_CAPACITY 64
#define TEFUI_OPTION_ID_CAPACITY 64
#define TEFUI_LABEL_CAPACITY 192

typedef enum tefui_option_type_t {
    TEFUI_OPTION_TOGGLE = 1,
    TEFUI_OPTION_SLIDER = 2
} tefui_option_type_t;

typedef struct tefui_option_snapshot_t {
    int handle;
    tefui_option_type_t type;
    char owner_id[TEFUI_OWNER_ID_CAPACITY];
    char option_id[TEFUI_OPTION_ID_CAPACITY];
    char label[TEFUI_LABEL_CAPACITY];
    bool bool_value;
    float float_value;
    float min_value;
    float max_value;
    float step;
} tefui_option_snapshot_t;

/*
 * Consumer libraries must define exported function-pointer variables with
 * these exact names. TEFKernel's TPF injector fills them before initialization.
 */
typedef int (*tefui_get_api_version_fn)(void);
typedef int (*tefui_register_toggle_fn)(const char *, const char *, const char *, bool);
typedef int (*tefui_register_slider_fn)(const char *, const char *, const char *,
                                        float, float, float, float);
typedef bool (*tefui_unregister_owner_fn)(const char *);
typedef int (*tefui_get_option_count_fn)(void);
typedef bool (*tefui_get_option_snapshot_fn)(int, tefui_option_snapshot_t *);
typedef bool (*tefui_get_bool_fn)(const char *, const char *, bool *);
typedef bool (*tefui_set_bool_fn)(const char *, const char *, bool);
typedef bool (*tefui_get_float_fn)(const char *, const char *, float *);
typedef bool (*tefui_set_float_fn)(const char *, const char *, float);

#ifdef TEFUI_DEFINE_IMPORTS
TEFUI_EXPORT tefui_get_api_version_fn tefui_get_api_version = NULL;
TEFUI_EXPORT tefui_register_toggle_fn tefui_register_toggle = NULL;
TEFUI_EXPORT tefui_register_slider_fn tefui_register_slider = NULL;
TEFUI_EXPORT tefui_unregister_owner_fn tefui_unregister_owner = NULL;
TEFUI_EXPORT tefui_get_option_count_fn tefui_get_option_count = NULL;
TEFUI_EXPORT tefui_get_option_snapshot_fn tefui_get_option_snapshot = NULL;
TEFUI_EXPORT tefui_get_bool_fn tefui_get_bool = NULL;
TEFUI_EXPORT tefui_set_bool_fn tefui_set_bool = NULL;
TEFUI_EXPORT tefui_get_float_fn tefui_get_float = NULL;
TEFUI_EXPORT tefui_set_float_fn tefui_set_float = NULL;
#else
extern tefui_get_api_version_fn tefui_get_api_version;
extern tefui_register_toggle_fn tefui_register_toggle;
extern tefui_register_slider_fn tefui_register_slider;
extern tefui_unregister_owner_fn tefui_unregister_owner;
extern tefui_get_option_count_fn tefui_get_option_count;
extern tefui_get_option_snapshot_fn tefui_get_option_snapshot;
extern tefui_get_bool_fn tefui_get_bool;
extern tefui_set_bool_fn tefui_set_bool;
extern tefui_get_float_fn tefui_get_float;
extern tefui_set_float_fn tefui_set_float;
#endif

#endif
