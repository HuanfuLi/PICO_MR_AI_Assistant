#include <jni.h>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <cmath> // For sinf and cosf

#include <android/log.h>
#include <android/native_window.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#define LOG_TAG "IrisAgent_Native"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define OXR_CHECK(instance, result, message) \
    [&](XrResult res) { \
        if (XR_SUCCEEDED(res)) return res; \
        char resultString[XR_MAX_RESULT_STRING_SIZE]; \
        if (instance != XR_NULL_HANDLE) { \
            xrResultToString(instance, res, resultString); \
            ALOGE("%s failed: %s", message, resultString); \
        } else { \
            ALOGE("%s failed with error code: %d", message, res); \
        } \
        return res; \
    }(result)

// =============================================================================
// 3D Math Library (Matrix)
// =============================================================================
struct Matrix4f {
    float M[16];
    static Matrix4f CreateIdentity() {
        Matrix4f r;
        for (int i = 0; i < 16; i++) r.M[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        return r;
    }
};

Matrix4f Matrix4f_Multiply(const Matrix4f& a, const Matrix4f& b) {
    Matrix4f result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.M[i * 4 + j] = a.M[i * 4 + 0] * b.M[0 * 4 + j] +
                                  a.M[i * 4 + 1] * b.M[1 * 4 + j] +
                                  a.M[i * 4 + 2] * b.M[2 * 4 + j] +
                                  a.M[i * 4 + 3] * b.M[3 * 4 + j];
        }
    }
    return result;
}

Matrix4f Matrix4f_CreateProjectionFov(const XrFovf fov, const float nearZ, const float farZ) {
    const float tanLeft = tanf(fov.angleLeft);
    const float tanRight = tanf(fov.angleRight);
    const float tanDown = tanf(fov.angleDown);
    const float tanUp = tanf(fov.angleUp);
    const float tanAngleWidth = tanRight - tanLeft;
    const float tanAngleHeight = tanUp - tanDown;
    Matrix4f result = {};
    result.M[0] = 2.0f / tanAngleWidth;
    result.M[5] = 2.0f / tanAngleHeight;
    result.M[8] = (tanRight + tanLeft) / tanAngleWidth;
    result.M[9] = (tanUp + tanDown) / tanAngleHeight;
    result.M[10] = -(farZ + nearZ) / (farZ - nearZ);
    result.M[11] = -1.0f;
    result.M[14] = -2.0f * farZ * nearZ / (farZ - nearZ);
    return result;
}

Matrix4f Matrix4f_CreateFromQuaternion(const XrQuaternionf& q) {
    Matrix4f result = Matrix4f::CreateIdentity();
    const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
    const float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    const float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    result.M[0] = 1.0f - (yy + zz); result.M[1] = xy - wz; result.M[2] = xz + wy;
    result.M[4] = xy + wz; result.M[5] = 1.0f - (xx + zz); result.M[6] = yz - wx;
    result.M[8] = xz - wy; result.M[9] = yz + wx; result.M[10] = 1.0f - (xx + yy);
    return result;
}

Matrix4f Matrix4f_CreateView(const XrPosef& pose) {
    Matrix4f rotation = Matrix4f_CreateFromQuaternion(pose.orientation);
    Matrix4f translation = Matrix4f::CreateIdentity();
    translation.M[12] = -pose.position.x;
    translation.M[13] = -pose.position.y;
    translation.M[14] = -pose.position.z;
    return Matrix4f_Multiply(rotation, translation);
}

Matrix4f Matrix4f_CreateTranslation(float x, float y, float z) {
    Matrix4f r = Matrix4f::CreateIdentity();
    r.M[12] = x; r.M[13] = y; r.M[14] = z;
    return r;
}

// =============================================================================
// App State & Structures
// =============================================================================
struct GraphicsState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config = nullptr;
};

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    GLuint depthTexture = 0;
};

struct GraphicsPipeline {
    GLuint shaderProgram = 0;
    GLint mvpLocation = -1;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
};

struct AppState {
    JavaVM* vm = nullptr;
    jobject mainActivity = nullptr;
    XrInstance xrInstance = XR_NULL_HANDLE;
    XrSession xrSession = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSpace stageSpace = XR_NULL_HANDLE;
    XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    GraphicsState graphics = {};
    GraphicsPipeline pipeline = {};
    std::vector<XrViewConfigurationView> viewConfigs;
    std::vector<Swapchain> swapchains;
    std::vector<XrView> views;
    std::vector<uint32_t> framebuffers;
    std::thread appThread;
    std::mutex appMutex;
    std::condition_variable appCondition;
    bool resumed = false;
    bool running = false;
    bool sessionReady = false;
};
static AppState appState = {};

// =============================================================================
// Graphics Setup & Lifecycle
// =============================================================================

bool CreateGraphicsPipeline() {
    const char* vertexShaderSrc = R"glsl(
        #version 320 es
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aColor; // Input for vertex color
        uniform mat4 uMvp;
        out vec3 vColor;
        void main() {
            gl_Position = uMvp * vec4(aPos, 1.0);
            vColor = aColor; // Pass color to fragment shader
        }
    )glsl";
    const char* fragmentShaderSrc = R"glsl(
        #version 320 es
        precision mediump float;
        in vec3 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vec4(vColor, 1.0);
        }
    )glsl";

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSrc, nullptr);
    glCompileShader(vertexShader);
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSrc, nullptr);
    glCompileShader(fragmentShader);
    appState.pipeline.shaderProgram = glCreateProgram();
    glAttachShader(appState.pipeline.shaderProgram, vertexShader);
    glAttachShader(appState.pipeline.shaderProgram, fragmentShader);
    glLinkProgram(appState.pipeline.shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    appState.pipeline.mvpLocation = glGetUniformLocation(appState.pipeline.shaderProgram, "uMvp");

    float vertices[] = {
            -0.5f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f, // Bottom-left, Red
            0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f, // Bottom-right, Green
            0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f, // Top-right, Blue
            -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f  // Top-left, Yellow
    };

    unsigned int indices[] = {
            0, 1, 2, // First triangle
            2, 3, 0  // Second triangle
    };

    glGenVertexArrays(1, &appState.pipeline.vao);
    glGenBuffers(1, &appState.pipeline.vbo);
    glGenBuffers(1, &appState.pipeline.ebo);
    glBindVertexArray(appState.pipeline.vao);
    glBindBuffer(GL_ARRAY_BUFFER, appState.pipeline.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, appState.pipeline.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    return true;
}

bool initializeGraphics() {
    ALOGI("Initializing EGL graphics...");
    appState.graphics.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (appState.graphics.display == EGL_NO_DISPLAY) { ALOGE("eglGetDisplay failed"); return false; }
    EGLint major, minor;
    if (!eglInitialize(appState.graphics.display, &major, &minor)) { ALOGE("eglInitialize failed"); return false; }
    ALOGI("EGL initialized, version %d.%d", major, minor);
    const EGLint attribs[] = { EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
    EGLint num_config;
    if (!eglChooseConfig(appState.graphics.display, attribs, &appState.graphics.config, 1, &num_config)) { ALOGE("eglChooseConfig failed"); return false; }
    const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    appState.graphics.context = eglCreateContext(appState.graphics.display, appState.graphics.config, EGL_NO_CONTEXT, context_attribs);
    if (appState.graphics.context == EGL_NO_CONTEXT) { ALOGE("eglCreateContext failed"); return false; }
    ALOGI("EGL Context created successfully.");
    return true;
}

void app_main();

extern "C" JNIEXPORT void JNICALL
Java_cnit355_finalproject_irisagentc_MainActivity_onCreateNative(JNIEnv* env, jobject, jobject activity) {
    ALOGI("--- Native onCreate ---");
    env->GetJavaVM(&appState.vm);
    appState.mainActivity = env->NewGlobalRef(activity);
    appState.running = true;
    appState.appThread = std::thread(app_main);
}

extern "C" JNIEXPORT void JNICALL
Java_cnit355_finalproject_irisagentc_MainActivity_onResumeNative(JNIEnv*, jobject) {
    ALOGI("--- Native onResume ---");
    std::unique_lock<std::mutex> lock(appState.appMutex);
    appState.resumed = true;
    appState.appCondition.notify_all();
}

extern "C" JNIEXPORT void JNICALL
Java_cnit355_finalproject_irisagentc_MainActivity_onPauseNative(JNIEnv*, jobject) {
    ALOGI("--- Native onPause ---");
    std::unique_lock<std::mutex> lock(appState.appMutex);
    appState.resumed = false;
    appState.appCondition.notify_all();
}

extern "C" JNIEXPORT void JNICALL
Java_cnit355_finalproject_irisagentc_MainActivity_onDestroyNative(JNIEnv* env, jobject) {
    ALOGI("--- Native onDestroy ---");
    std::unique_lock<std::mutex> lock(appState.appMutex);
    appState.running = false;
    appState.appCondition.notify_all();
    lock.unlock();
    if (appState.appThread.joinable()) {
        appState.appThread.join();
    }
    env->DeleteGlobalRef(appState.mainActivity);
}

// =============================================================================
// Main Application Thread
// =============================================================================
void app_main() {
    JNIEnv* env;
    appState.vm->AttachCurrentThread(&env, nullptr);
    ALOGI("App thread attached to JVM.");

    // **FIX**: All local variables are now declared at the top of the function scope.
    XrApplicationInfo appInfo = {};
    XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    XrLoaderInitInfoAndroidKHR loaderInitInfo = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    XrInstanceCreateInfoAndroidKHR createInfoAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    std::vector<const char*> extensions;
    XrSystemGetInfo systemGetInfo = {XR_TYPE_SYSTEM_GET_INFO, nullptr, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetOpenGLESGraphicsRequirementsKHR = nullptr;
    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
    XrReferenceSpaceCreateInfo spaceCreateInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    uint32_t viewCount = 0;
    uint32_t blendModeCount = 0;
    std::vector<XrEnvironmentBlendMode> blendModes; // Declare vector
    bool alphaBlendSupported = false;


    {
        std::unique_lock<std::mutex> lock(appState.appMutex);
        ALOGI("App thread waiting for resume...");
        appState.appCondition.wait(lock, [] { return appState.resumed; });
        ALOGI("App thread resumed.");
    }

    strcpy(appInfo.applicationName, "ProjectIrisMVP"); appInfo.applicationVersion = 1; strcpy(appInfo.engineName, "CustomEngine"); appInfo.engineVersion = 1; appInfo.apiVersion = XR_CURRENT_API_VERSION; createInfo.applicationInfo = appInfo; loaderInitInfo.applicationVM = appState.vm; loaderInitInfo.applicationContext = appState.mainActivity; if (OXR_CHECK(nullptr, xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR), "xrGetInstanceProcAddr") != XR_SUCCESS) goto cleanup; if (OXR_CHECK(nullptr, xrInitializeLoaderKHR((const XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfo), "xrInitializeLoaderKHR") != XR_SUCCESS) goto cleanup; ALOGI("OpenXR Loader initialized."); createInfoAndroid.applicationVM = appState.vm; createInfoAndroid.applicationActivity = appState.mainActivity; createInfo.next = &createInfoAndroid; extensions = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME}; createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size()); createInfo.enabledExtensionNames = extensions.data(); if (OXR_CHECK(appState.xrInstance, xrCreateInstance(&createInfo, &appState.xrInstance), "xrCreateInstance") != XR_SUCCESS) goto cleanup; ALOGI("OpenXR instance created."); if (OXR_CHECK(appState.xrInstance, xrGetSystem(appState.xrInstance, &systemGetInfo, &appState.systemId), "xrGetSystem") != XR_SUCCESS) goto cleanup; ALOGI("OpenXR system found.");

    if (!initializeGraphics()) goto cleanup;

    if (eglMakeCurrent(appState.graphics.display, EGL_NO_SURFACE, EGL_NO_SURFACE, appState.graphics.context) == EGL_FALSE) { ALOGE("eglMakeCurrent failed!"); goto cleanup; }
    ALOGI("EGL context made current on app thread.");

    // Check for passthrough support (logic remains here, but declarations are moved up)
    xrEnumerateEnvironmentBlendModes(appState.xrInstance, appState.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blendModeCount, nullptr);
    blendModes.resize(blendModeCount); // **FIX**: Resize vector after getting count
    xrEnumerateEnvironmentBlendModes(appState.xrInstance, appState.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blendModeCount, &blendModeCount, blendModes.data());
    for (const auto& mode : blendModes) {
        if (mode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) {
            alphaBlendSupported = true;
            break;
        }
    }
    if (alphaBlendSupported) {
        appState.blendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
        ALOGI("Passthrough (ALPHA_BLEND) is supported and selected.");
    } else {
        ALOGI("Passthrough (ALPHA_BLEND) is not supported, falling back to OPAQUE.");
    }

    if (OXR_CHECK(appState.xrInstance, xrGetInstanceProcAddr(appState.xrInstance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&pfnGetOpenGLESGraphicsRequirementsKHR), "xrGetInstanceProcAddr") != XR_SUCCESS) goto cleanup; if (OXR_CHECK(appState.xrInstance, pfnGetOpenGLESGraphicsRequirementsKHR(appState.xrInstance, appState.systemId, &graphicsRequirements), "xrGetOpenGLESGraphicsRequirementsKHR") != XR_SUCCESS) goto cleanup; graphicsBinding.display = appState.graphics.display; graphicsBinding.config = appState.graphics.config; graphicsBinding.context = appState.graphics.context; sessionCreateInfo.next = &graphicsBinding; sessionCreateInfo.systemId = appState.systemId; if (OXR_CHECK(appState.xrInstance, xrCreateSession(appState.xrInstance, &sessionCreateInfo, &appState.xrSession), "xrCreateSession") != XR_SUCCESS) goto cleanup; ALOGI("OpenXR session created."); spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE; spaceCreateInfo.poseInReferenceSpace = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}}; if (OXR_CHECK(appState.xrInstance, xrCreateReferenceSpace(appState.xrSession, &spaceCreateInfo, &appState.stageSpace), "xrCreateReferenceSpace") != XR_SUCCESS) goto cleanup; ALOGI("OpenXR stage space created.");

    xrEnumerateViewConfigurationViews(appState.xrInstance, appState.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
    appState.viewConfigs.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    appState.views.resize(viewCount, {XR_TYPE_VIEW});
    xrEnumerateViewConfigurationViews(appState.xrInstance, appState.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, appState.viewConfigs.data());
    appState.swapchains.resize(viewCount);
    appState.framebuffers.resize(viewCount);
    for (uint32_t i = 0; i < viewCount; ++i) {
        auto& sc = appState.swapchains[i];
        sc.width = appState.viewConfigs[i].recommendedImageRectWidth;
        sc.height = appState.viewConfigs[i].recommendedImageRectHeight;
        XrSwapchainCreateInfo swapchainCI = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapchainCI.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainCI.format = GL_RGBA8;
        swapchainCI.width = static_cast<uint32_t>(sc.width);
        swapchainCI.height = static_cast<uint32_t>(sc.height);
        swapchainCI.sampleCount = 1;
        swapchainCI.faceCount = 1;
        swapchainCI.arraySize = 1;
        swapchainCI.mipCount = 1;
        OXR_CHECK(appState.xrInstance, xrCreateSwapchain(appState.xrSession, &swapchainCI, &sc.handle), "xrCreateSwapchain");
        uint32_t imageCount = 0;
        xrEnumerateSwapchainImages(sc.handle, 0, &imageCount, nullptr);
        sc.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xrEnumerateSwapchainImages(sc.handle, imageCount, &imageCount, (XrSwapchainImageBaseHeader*)sc.images.data());

        glGenTextures(1, &sc.depthTexture);
        glBindTexture(GL_TEXTURE_2D, sc.depthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, sc.width, sc.height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    ALOGI("Swapchains created for %d views.", viewCount);

    CreateGraphicsPipeline();

    while (appState.running) {
        XrEventDataBuffer eventData = {XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(appState.xrInstance, &eventData) == XR_SUCCESS) {
            if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto ssc = *reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
                ALOGI("OpenXR session state changed to %d", ssc.state);
                if (ssc.state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO, nullptr, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
                    xrBeginSession(appState.xrSession, &bi);
                    appState.sessionReady = true;
                } else if (ssc.state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(appState.xrSession);
                    appState.sessionReady = false;
                } else if (ssc.state == XR_SESSION_STATE_EXITING || ssc.state == XR_SESSION_STATE_LOSS_PENDING) {
                    appState.running = false;
                }
            }
            eventData = {XR_TYPE_EVENT_DATA_BUFFER};
        }

        if (!appState.sessionReady || !appState.resumed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        XrFrameState frameState = {XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo frameWaitInfo = {XR_TYPE_FRAME_WAIT_INFO};
        xrWaitFrame(appState.xrSession, &frameWaitInfo, &frameState);

        xrBeginFrame(appState.xrSession, nullptr);

        std::vector<XrCompositionLayerBaseHeader*> layers;
        XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        std::vector<XrCompositionLayerProjectionView> projectionViews(viewCount, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});

        if (frameState.shouldRender) {
            XrViewState viewState = {XR_TYPE_VIEW_STATE};
            XrViewLocateInfo viewLocateInfo = {XR_TYPE_VIEW_LOCATE_INFO, nullptr, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, frameState.predictedDisplayTime, appState.stageSpace};
            uint32_t viewCountOutput;
            xrLocateViews(appState.xrSession, &viewLocateInfo, &viewState, viewCount, &viewCountOutput, appState.views.data());

            for (uint32_t i = 0; i < viewCount; ++i) {
                auto& sc = appState.swapchains[i];
                uint32_t imageIndex;
                xrAcquireSwapchainImage(sc.handle, nullptr, &imageIndex);
                XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION};
                xrWaitSwapchainImage(sc.handle, &waitInfo);

                if (appState.framebuffers[i] == 0) glGenFramebuffers(1, &appState.framebuffers[i]);
                glBindFramebuffer(GL_FRAMEBUFFER, appState.framebuffers[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc.images[imageIndex].image, 0);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sc.depthTexture, 0);

                glViewport(0, 0, sc.width, sc.height);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);

                Matrix4f proj = Matrix4f_CreateProjectionFov(appState.views[i].fov, 0.1f, 100.0f);
                Matrix4f view = Matrix4f_CreateView(appState.views[i].pose);

                Matrix4f model = Matrix4f_CreateTranslation(0.0f, 0.0f, -1.0f);
                Matrix4f mvp = Matrix4f_Multiply(Matrix4f_Multiply(proj, view), model);

                glUseProgram(appState.pipeline.shaderProgram);
                glUniformMatrix4fv(appState.pipeline.mvpLocation, 1, GL_FALSE, mvp.M);
                glBindVertexArray(appState.pipeline.vao);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                glUseProgram(0);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                xrReleaseSwapchainImage(sc.handle, nullptr);

                projectionViews[i].pose = appState.views[i].pose;
                projectionViews[i].fov = appState.views[i].fov;
                projectionViews[i].subImage.swapchain = sc.handle;
                projectionViews[i].subImage.imageRect = {{0, 0}, {sc.width, sc.height}};
            }

            layer.space = appState.stageSpace; layer.viewCount = viewCount; layer.views = projectionViews.data();
            layers.push_back((XrCompositionLayerBaseHeader*)&layer);
        }

        XrFrameEndInfo frameEndInfo = {XR_TYPE_FRAME_END_INFO, nullptr, frameState.predictedDisplayTime, appState.blendMode, (uint32_t)layers.size(), layers.data()};
        xrEndFrame(appState.xrSession, &frameEndInfo);
    }

    cleanup:
    ALOGI("Cleaning up native resources...");
    glDeleteFramebuffers(appState.framebuffers.size(), appState.framebuffers.data());
    glDeleteProgram(appState.pipeline.shaderProgram);
    glDeleteBuffers(1, &appState.pipeline.vbo);
    glDeleteBuffers(1, &appState.pipeline.ebo);
    glDeleteVertexArrays(1, &appState.pipeline.vao);
    for(auto& sc : appState.swapchains) {
        if (sc.handle != XR_NULL_HANDLE) xrDestroySwapchain(sc.handle);
        if (sc.depthTexture != 0) glDeleteTextures(1, &sc.depthTexture);
    }
    if (appState.stageSpace != XR_NULL_HANDLE) xrDestroySpace(appState.stageSpace);
    if (appState.xrSession != XR_NULL_HANDLE) xrDestroySession(appState.xrSession);
    if (appState.graphics.context != EGL_NO_CONTEXT) {
        eglMakeCurrent(appState.graphics.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(appState.graphics.display, appState.graphics.context);
    }
    if (appState.graphics.display != EGL_NO_DISPLAY) eglTerminate(appState.graphics.display);
    if (appState.xrInstance != XR_NULL_HANDLE) xrDestroyInstance(appState.xrInstance);

    appState.xrInstance = XR_NULL_HANDLE;
    appState.xrSession = XR_NULL_HANDLE;
    appState.stageSpace = XR_NULL_HANDLE;
    appState.systemId = XR_NULL_SYSTEM_ID;
    appState.graphics = {};

    ALOGI("App thread detached from JVM.");
    appState.vm->DetachCurrentThread();
}