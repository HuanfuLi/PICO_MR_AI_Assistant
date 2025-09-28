# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Declares the project name. This should be defined early.
# It makes ${CMAKE_PROJECT_NAME} available, which we'll use for consistency.
project("irisagentc")

# --- 1. 创建原生库 (只创建一次) ---
# Creates and names your library from the specified source files.
add_library(${CMAKE_PROJECT_NAME} SHARED native-lib.cpp)

# --- 2. 指定头文件目录 (只指定一次) ---
# Tells CMake where to find header files like <openxr/openxr.h>.
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# --- 3. 添加并定义预编译的 OpenXR 库 ---
# Defines the pre-built openxr_loader.so as an IMPORTED library target.
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION
        ${CMAKE_SOURCE_DIR}/../../../libs/${ANDROID_ABI}/libopenxr_loader.so)

# --- 4. 查找所有需要的 Android 系统库 ---
# Finds system libraries that we need to link against.
find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv3-lib GLESv3)

# --- 5. 链接所有库到您的原生库 (只链接一次) ---
# Links your library against the OpenXR loader and all required system libraries.
target_link_libraries(${CMAKE_PROJECT_NAME}
        # Link our pre-built OpenXR Loader
        openxr_loader
        # Link all necessary Android system libraries
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
)