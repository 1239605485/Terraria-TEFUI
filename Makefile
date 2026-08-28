CC ?= gcc
CFLAGS ?= -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror
TEFKERNEL_DIR ?= vendor/tefkernel
INCLUDES = -Iinclude -I$(TEFKERNEL_DIR)/include
BUILD_DIR = build

.PHONY: all clean check test

all: $(BUILD_DIR)/libplugin.linux.x64.so $(BUILD_DIR)/libmodule.linux.x64.so

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/libplugin.linux.x64.so: plugin/plugin.c include/tefui_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -shared plugin/plugin.c \
		$(TEFKERNEL_DIR)/include/tef_api_imp.c -lm -o $@

$(BUILD_DIR)/libmodule.linux.x64.so: module/module.c include/tefui_api.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -shared module/module.c \
		$(TEFKERNEL_DIR)/include/tef_api_imp.c -o $@

check:
	$(CC) $(CFLAGS) $(INCLUDES) -fsyntax-only plugin/plugin.c
	$(CC) $(CFLAGS) $(INCLUDES) -fsyntax-only module/module.c

test: $(BUILD_DIR)/libplugin.linux.x64.so
	$(CC) -O2 -Wall -Wextra -Werror $(INCLUDES) tests/test_registry.c \
		$(TEFKERNEL_DIR)/include/tef_api_imp.c -L$(BUILD_DIR) \
		-Wl,-rpath,'$$ORIGIN' -lplugin.linux.x64 -o $(BUILD_DIR)/test_registry
	$(BUILD_DIR)/test_registry

clean:
	rm -f $(BUILD_DIR)/libplugin.* $(BUILD_DIR)/libmodule.* $(BUILD_DIR)/test_registry
