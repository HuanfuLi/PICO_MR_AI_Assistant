#include <jni.h>
#include <string>
#include <thread>
#include <vector>

// Android Logging
#include <android/log.h>
#include <android/native_window.h>

// --- FIX: EGL/GLES headers must be included BEFORE OpenXR headers ---
// EGL and GLES definitions are needed by openxr_platform.h
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

// OpenXR Headers
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#define LOG_TAG "IrisAgent_Native"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

#define OXR_CHECK(instance, result, message) \
    if (XR_FAILED(result)) { \
        char resultString[XR_MAX_RESULT_STRING_SIZE]; \
        if (instance != XR_NULL_HANDLE) { \
            xrResultToString(instance, result, resultString); \
            ALOGE("%s failed: %s", message, resultString); \
        } else { \
            ALOGE("%s failed with error code: %d", message, result); \
        } \
        return; \
    }

// Struct to hold application state
struct AppState {
    JavaVM* vm = nullptr;
    jobject mainActivity = nullptr;
    XrInstance xrInstance = XR_NULL_HANDLE;
    XrSession xrSession = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSpace stageSpace = XR_NULL_HANDLE;
    bool resumed = false;
    std::thread renderThread;
};
static AppState appState = {};

extern "C" JNIEXPORT void JNICALL
Java_cnit355_finalproject_irisagentc_MainActivity_onCreateNative(
        JNIEnv* env, jobject, jobject activity) {

    ALOGI("--- Native onCreate ---");
    env->GetJavaVM(&appState.vm);
    appState.mainActivity = env->NewGlobalRef(activity);

    XrInstanceCreateInfoAndroidKHR createInfoAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    createInfoAndroid.applicationVM = appState.vm;
    createInfoAndroid.applicationActivity = appState.mainActivity;

    XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = &createInfoAndroid;
    strcpy(createInfo.applicationInfo.applicationName, "ProjectIrisMVP");
    createInfo.applicationInfo.applicationVersion = 1;
    strcpy(createInfo.applicationInfo.engineName, "CustomEngine");
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    std::vector<const char*> extensions = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, // Corrected spelling
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,      // Corrected spelling
            "XR_EPIC_view_configuration_passthrough"
    };
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.enabledExtensionNames = extensions.data();

    XrResult result = xrCreateInstance(&createInfo, &appState.xrInstance);
    OXR_CHECK(appState.xrInstance, result, "xrCreateInstance");
    ALOGI("OpenXR instance created successfully.");

    XrSystemGetInfo systemGetInfo = {XR_TYPE_SYSTEM_GET_INFO};
    systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = xrGetSystem(appState.xrInstance, &systemGetInfo, &appState.systemId);
    OXR_CHECK(appState.xrInstance, result, "xrGetSystem");

    ALOGI("Native setup complete. Waiting for onResume.");
}

void renderLoop() {
    ALOGI("Render thread started.");
    while (appState.resumed) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ALOGI("Render thread finished.");
}

extern "C" JNIEXPORT void JNICALL
Java_cnit355_finalproject_irisagentc_MainActivity_onResumeNative(JNIEnv*, jobject) {
    ALOGI("--- Native onResume ---");
    appState.resumed = true;
    appState.renderThread = std::thread(renderLoop);
}

extern "C" JNIEXPORT void JNICALL
Java_cnit355_finalproject_irisagentc_MainActivity_onPauseNative(JNIEnv*, jobject) {
    ALOGI("--- Native onPause ---");
    appState.resumed = false;
    if (appState.renderThread.joinable()) {
        appState.renderThread.join();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_cnit355_finalproject_irisagentc_MainActivity_onDestroyNative(JNIEnv* env, jobject) {
    ALOGI("--- Native onDestroy ---");
    if(appState.stageSpace != XR_NULL_HANDLE) xrDestroySpace(appState.stageSpace);
    if(appState.xrSession != XR_NULL_HANDLE) xrDestroySession(appState.xrSession);
    if(appState.xrInstance != XR_NULL_HANDLE) xrDestroyInstance(appState.xrInstance);
    env->DeleteGlobalRef(appState.mainActivity);
    appState = {};
    ALOGI("Native cleanup complete.");
}