#include "tefui/tefui_api.h"
#include "tefplugin/tpf_core.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#endif

namespace {

struct Feature {
    std::string id;
    std::string name;
    bool enabled;
    tefui_feature_changed_callback_t callback;
    void *user_data;
};

std::mutex g_features_mutex;
std::vector<Feature> g_features;

#if defined(__ANDROID__)
JavaVM *g_vm = nullptr;
jclass g_bridge_class = nullptr;
bool g_bridge_ready = false;

void log_android(int priority, const char *message) {
    __android_log_print(priority, "TEFUI", "%s", message);
}

JNIEnv *get_env(bool *attached) {
    *attached = false;
    if (!g_vm) return nullptr;

    JNIEnv *env = nullptr;
    const jint result = g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (result == JNI_OK) return env;
    if (result != JNI_EDETACHED) return nullptr;
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    *attached = true;
    return env;
}

void clear_exception(JNIEnv *env) {
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

jclass load_bridge_class(JNIEnv *env) {
    jclass activity_thread = env->FindClass("android/app/ActivityThread");
    if (!activity_thread) {
        clear_exception(env);
        return nullptr;
    }

    jmethodID current_thread = env->GetStaticMethodID(
            activity_thread, "currentActivityThread",
            "()Landroid/app/ActivityThread;");
    jobject thread = current_thread
            ? env->CallStaticObjectMethod(activity_thread, current_thread)
            : nullptr;
    clear_exception(env);
    if (!thread) {
        env->DeleteLocalRef(activity_thread);
        return nullptr;
    }

    jmethodID get_application = env->GetMethodID(
            activity_thread, "getApplication", "()Landroid/app/Application;");
    jobject application = get_application
            ? env->CallObjectMethod(thread, get_application)
            : nullptr;
    clear_exception(env);
    if (!application) {
        env->DeleteLocalRef(thread);
        env->DeleteLocalRef(activity_thread);
        return nullptr;
    }

    jclass context = env->FindClass("android/content/Context");
    jmethodID get_loader = context
            ? env->GetMethodID(context, "getClassLoader", "()Ljava/lang/ClassLoader;")
            : nullptr;
    jobject loader = get_loader ? env->CallObjectMethod(application, get_loader) : nullptr;
    clear_exception(env);

    jclass result = nullptr;
    if (loader) {
        jclass loader_class = env->FindClass("java/lang/ClassLoader");
        jmethodID load_class = loader_class
                ? env->GetMethodID(loader_class, "loadClass",
                                   "(Ljava/lang/String;)Ljava/lang/Class;")
                : nullptr;
        jstring name = env->NewStringUTF("eternal.future.tefkernel.TefUiBridge");
        jobject class_object = load_class && name
                ? env->CallObjectMethod(loader, load_class, name)
                : nullptr;
        clear_exception(env);
        if (class_object) {
            result = static_cast<jclass>(class_object);
        }
        if (name) env->DeleteLocalRef(name);
        if (loader_class) env->DeleteLocalRef(loader_class);
    }

    if (loader) env->DeleteLocalRef(loader);
    if (context) env->DeleteLocalRef(context);
    env->DeleteLocalRef(application);
    env->DeleteLocalRef(thread);
    env->DeleteLocalRef(activity_thread);
    return result;
}

void invoke_bridge_void(const char *method_name) {
    if (!g_bridge_ready || !g_bridge_class) return;
    bool attached = false;
    JNIEnv *env = get_env(&attached);
    if (!env) return;
    jmethodID method = env->GetStaticMethodID(g_bridge_class, method_name, "()V");
    if (method) env->CallStaticVoidMethod(g_bridge_class, method);
    clear_exception(env);
    if (attached) g_vm->DetachCurrentThread();
}

void invoke_bridge_initialize() {
    bool attached = false;
    JNIEnv *env = get_env(&attached);
    if (!env) return;

    jclass bridge = load_bridge_class(env);
    if (!bridge) {
        log_android(ANDROID_LOG_WARN,
                    "TefUiBridge is not available; the native API remains usable");
        if (attached) g_vm->DetachCurrentThread();
        return;
    }

    static const JNINativeMethod methods[] = {
            {"nativeGetFeatureCount", "()I", reinterpret_cast<void *>(+[](
                    JNIEnv *, jclass) -> jint {
                return static_cast<jint>(tefui_get_feature_count());
            })},
            {"nativeGetFeatureId", "(I)Ljava/lang/String;", reinterpret_cast<void *>(+[](
                    JNIEnv *e, jclass, jint index) -> jstring {
                const char *id = tefui_get_feature_id(static_cast<size_t>(index));
                return id ? e->NewStringUTF(id) : nullptr;
            })},
            {"nativeGetFeatureName", "(I)Ljava/lang/String;", reinterpret_cast<void *>(+[](
                    JNIEnv *e, jclass, jint index) -> jstring {
                const char *name = tefui_get_feature_name(static_cast<size_t>(index));
                return name ? e->NewStringUTF(name) : nullptr;
            })},
            {"nativeIsFeatureEnabled", "(Ljava/lang/String;)Z", reinterpret_cast<void *>(+[](
                    JNIEnv *e, jclass, jstring id) -> jboolean {
                if (!id) return JNI_FALSE;
                const char *chars = e->GetStringUTFChars(id, nullptr);
                const bool value = chars && tefui_is_feature_enabled(chars);
                if (chars) e->ReleaseStringUTFChars(id, chars);
                return value ? JNI_TRUE : JNI_FALSE;
            })},
            {"nativeOnFeatureToggle", "(Ljava/lang/String;Z)V", reinterpret_cast<void *>(+[](
                    JNIEnv *e, jclass, jstring id, jboolean enabled) {
                if (!id) return;
                const char *chars = e->GetStringUTFChars(id, nullptr);
                if (chars) {
                    tefui_set_feature_enabled(chars, enabled == JNI_TRUE);
                    e->ReleaseStringUTFChars(id, chars);
                }
            })}
    };

    if (env->RegisterNatives(bridge, methods,
                             static_cast<jint>(sizeof(methods) / sizeof(methods[0]))) != JNI_OK) {
        clear_exception(env);
        log_android(ANDROID_LOG_ERROR, "Failed to register TefUiBridge natives");
        env->DeleteLocalRef(bridge);
        if (attached) g_vm->DetachCurrentThread();
        return;
    }

    g_bridge_class = static_cast<jclass>(env->NewGlobalRef(bridge));
    env->DeleteLocalRef(bridge);
    g_bridge_ready = g_bridge_class != nullptr;
    if (g_bridge_ready) invoke_bridge_void("initialize");
    if (attached) g_vm->DetachCurrentThread();
}

void try_initialize_android_bridge() {
    void *art = dlopen("libart.so", RTLD_NOW | RTLD_NOLOAD);
    if (!art) art = dlopen("libart.so", RTLD_NOW);
    if (!art) return;

    using GetCreatedVms = jint (*)(JavaVM **, jsize, jsize *);
    auto get_created_vms = reinterpret_cast<GetCreatedVms>(dlsym(art, "JNI_GetCreatedJavaVMs"));
    if (get_created_vms) {
        jsize count = 0;
        if (get_created_vms(&g_vm, 1, &count) != JNI_OK || count == 0) g_vm = nullptr;
    }
    if (g_vm) invoke_bridge_initialize();
    dlclose(art);
}
#else
void try_initialize_android_bridge() {}
#endif

bool valid_text(const char *value) {
    return value && value[0] != '\0' && std::strlen(value) <= 127;
}

void refresh_ui() {
#if defined(__ANDROID__)
    invoke_bridge_void("refresh");
#endif
}

} // namespace

bool API_CALL tefui_register_feature(const char *feature_id,
                                     const char *display_name,
                                     bool default_enabled,
                                     tefui_feature_changed_callback_t on_changed,
                                     void *user_data) {
    if (!valid_text(feature_id) || !valid_text(display_name)) return false;

    {
        std::lock_guard<std::mutex> lock(g_features_mutex);
        const auto exists = std::find_if(g_features.begin(), g_features.end(),
                [feature_id](const Feature &feature) { return feature.id == feature_id; });
        if (exists != g_features.end()) return false;
        g_features.push_back({feature_id, display_name, default_enabled, on_changed, user_data});
    }
    refresh_ui();
    return true;
}

bool API_CALL tefui_unregister_feature(const char *feature_id) {
    if (!valid_text(feature_id)) return false;
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(g_features_mutex);
        const auto it = std::remove_if(g_features.begin(), g_features.end(),
                [feature_id](const Feature &feature) { return feature.id == feature_id; });
        removed = it != g_features.end();
        g_features.erase(it, g_features.end());
    }
    if (removed) refresh_ui();
    return removed;
}

bool API_CALL tefui_is_feature_enabled(const char *feature_id) {
    if (!feature_id) return false;
    std::lock_guard<std::mutex> lock(g_features_mutex);
    const auto it = std::find_if(g_features.begin(), g_features.end(),
            [feature_id](const Feature &feature) { return feature.id == feature_id; });
    return it != g_features.end() && it->enabled;
}

bool API_CALL tefui_set_feature_enabled(const char *feature_id, bool enabled) {
    if (!feature_id) return false;

    tefui_feature_changed_callback_t callback = nullptr;
    void *user_data = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_features_mutex);
        const auto it = std::find_if(g_features.begin(), g_features.end(),
                [feature_id](const Feature &feature) { return feature.id == feature_id; });
        if (it == g_features.end()) return false;
        if (it->enabled == enabled) return true;
        it->enabled = enabled;
        callback = it->callback;
        user_data = it->user_data;
    }

    if (callback) callback(feature_id, enabled, user_data);
    refresh_ui();
    return true;
}

size_t API_CALL tefui_get_feature_count() {
    std::lock_guard<std::mutex> lock(g_features_mutex);
    return g_features.size();
}

const char *API_CALL tefui_get_feature_id(size_t index) {
    std::lock_guard<std::mutex> lock(g_features_mutex);
    return index < g_features.size() ? g_features[index].id.c_str() : nullptr;
}

const char *API_CALL tefui_get_feature_name(size_t index) {
    std::lock_guard<std::mutex> lock(g_features_mutex);
    return index < g_features.size() ? g_features[index].name.c_str() : nullptr;
}

bool API_CALL tefui_is_ui_available() {
#if defined(__ANDROID__)
    return g_bridge_ready;
#else
    return false;
#endif
}

static const tpf_plugin_info_t g_plugin_info = {
        "eternal.future.tefui",
        "TEF 游戏内模组控制面板",
        "eternalfuture-e38299",
        "0.1.0",
        2026083101
};

static bool API_CALL initialize_plugin(plugin_handle_t *this_handle) {
    bool ok = true;
    ok = tpf_register_symbol(this_handle, "tefui_register_feature",
                             reinterpret_cast<const void *>(tefui_register_feature)) && ok;
    ok = tpf_register_symbol(this_handle, "tefui_unregister_feature",
                             reinterpret_cast<const void *>(tefui_unregister_feature)) && ok;
    ok = tpf_register_symbol(this_handle, "tefui_is_feature_enabled",
                             reinterpret_cast<const void *>(tefui_is_feature_enabled)) && ok;
    ok = tpf_register_symbol(this_handle, "tefui_set_feature_enabled",
                             reinterpret_cast<const void *>(tefui_set_feature_enabled)) && ok;
    ok = tpf_register_symbol(this_handle, "tefui_get_feature_count",
                             reinterpret_cast<const void *>(tefui_get_feature_count)) && ok;
    ok = tpf_register_symbol(this_handle, "tefui_get_feature_id",
                             reinterpret_cast<const void *>(tefui_get_feature_id)) && ok;
    ok = tpf_register_symbol(this_handle, "tefui_get_feature_name",
                             reinterpret_cast<const void *>(tefui_get_feature_name)) && ok;
    ok = tpf_register_symbol(this_handle, "tefui_is_ui_available",
                             reinterpret_cast<const void *>(tefui_is_ui_available)) && ok;

#if defined(__ANDROID__)
    try_initialize_android_bridge();
#endif
    return ok;
}

static void API_CALL cleanup_plugin(plugin_handle_t *) {
#if defined(__ANDROID__)
    if (g_bridge_ready) {
        invoke_bridge_void("shutdown");
        bool attached = false;
        JNIEnv *env = get_env(&attached);
        if (env && g_bridge_class) env->DeleteGlobalRef(g_bridge_class);
        g_bridge_class = nullptr;
        g_bridge_ready = false;
        if (attached) g_vm->DetachCurrentThread();
    }
#endif
    std::lock_guard<std::mutex> lock(g_features_mutex);
    g_features.clear();
}

static const tpf_plugin_ops_t g_plugin_ops = {
        initialize_plugin,
        cleanup_plugin,
        []() -> const tpf_plugin_info_t * { return &g_plugin_info; }
};

extern "C" API_EXPORT const tpf_plugin_ops_t *API_CALL tpf_create_plugin(void) {
    return &g_plugin_ops;
}

