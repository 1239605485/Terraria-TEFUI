#include "mod_core.h"
#include "mod_logger.h"

#include <stdbool.h>
#include <stddef.h>

void (*mod_logger_write)(mod_log_level_t, const char *, const char *, ...) = NULL;

void tefui_registry_initialize(void);
void tefui_registry_cleanup(void);
bool tefui_ui_initialize(void);
bool tefui_ui_cleanup(void);

static void init_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    tefui_registry_initialize();
    const bool ready = tefui_ui_initialize();
    if (mod_logger_write) {
        mod_logger_write(ready ? MOD_LOG_LEVEL_INFO : MOD_LOG_LEVEL_ERROR,
                         "TEFUI", ready ? "TEFUI initialized" : "TEFUI UI initialization failed");
    }
}

static void cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    tefui_ui_cleanup();
    tefui_registry_cleanup();
}

static kernel_mod_info_t g_info = {
    .pkg_id = "celso.tefui",
    .version_code = 202608298,
    .api_version = 1,
    .version = "0.1.7"
};

static kernel_mod_info_t *get_info(void) { return &g_info; }

static kernel_mod_ops_t g_ops = {init_mod, cleanup_mod, get_info};

__attribute__((visibility("default")))
kernel_mod_ops_t *create_kernel_mod(void) { return &g_ops; }
