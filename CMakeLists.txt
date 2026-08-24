cmake_minimum_required(VERSION 3.24)
project(LiveSub VERSION 1.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# 构建模式
#   LIVESUB_PREBUILT_VULKAN=ON  → 链接 llama.cpp 官方预编译 Vulkan DLL
#                                  （无需本地编译 llama.cpp，需先下载并解压）
#   OFF（默认）                  → 本地源码编译 llama.cpp（CPU；Vulkan 需 SDK）
# ---------------------------------------------------------------------------
option(LIVESUB_PREBUILT_VULKAN "链接预编译 Vulkan DLL（tools/sdk/vulkan）" OFF)
set(LIVESUB_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/sdk/vulkan" CACHE PATH "预编译 DLL 目录")

# Windows + MinGW：cpp-httplib 等需要 Win10+ API 级别
if(WIN32 AND MINGW)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# sherpa-onnx（SenseVoice / 流式 zipformer 引擎）
#   二进制库在 third_party/sherpa-onnx/lib（预编译 win-x64，运行时动态加载）；
#   C API 头文件体积大不入仓，configure 时自动下载到 build 目录
# ---------------------------------------------------------------------------
set(SHERPA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx")
set(SHERPA_API_H "${SHERPA_DIR}/include/sherpa-onnx-c-api.h")
if(NOT EXISTS "${SHERPA_API_H}")
    file(MAKE_DIRECTORY "${SHERPA_DIR}/include")
    file(DOWNLOAD
        "https://cdn.jsdelivr.net/gh/k2-fsa/sherpa-onnx@v1.13.6/sherpa-onnx/c-api/c-api.h"
        "${SHERPA_API_H}"
        SHOW_PROGRESS)
endif()

if(LIVESUB_PREBUILT_VULKAN)
    set(LIVESUB_LLAMA_INCLUDE
        third_party/llama.cpp/include
        third_party/llama.cpp/ggml/include
        third_party/llama.cpp/tools/mtmd
    )
    set(LIVESUB_LLAMA_LIBS
        ${LIVESUB_SDK_DIR}/llama.dll
        ${LIVESUB_SDK_DIR}/mtmd.dll
        ${LIVESUB_SDK_DIR}/ggml.dll
        ${LIVESUB_SDK_DIR}/ggml-base.dll
        ${LIVESUB_SDK_DIR}/ggml-vulkan.dll
    )
else()
    set(LLAMA_BUILD_MTMD   ON  CACHE BOOL "" FORCE)  # 只构建 mtmd 库
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS  OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_APP    OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL         OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_UI     OFF CACHE BOOL "" FORCE)
    set(GGML_OPENMP        OFF CACHE BOOL "" FORCE)
    set(GGML_NATIVE        ON  CACHE BOOL "" FORCE)
    add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL)
    set(LIVESUB_LLAMA_LIBS mtmd)
endif()

# ---------------------------------------------------------------------------
# LiveSub（GUI 子系统：双击运行无控制台窗口，日志走 livesub.log + 托盘）
# ---------------------------------------------------------------------------
add_executable(livesub WIN32
    src/livesub.rc
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/audio/wasapi_capture.cpp
    src/audio/resampler.cpp
    src/audio/wav_reader.cpp
    src/asr/vad.cpp
    src/asr/audio_queue.cpp
    src/asr/asr_engine.cpp
    src/asr/sherpa_engine.cpp
    src/asr/text_merge.cpp
    src/ui/subtitle_window.cpp
    src/ui/settings_window.cpp
    src/ui/tray_icon.cpp
    src/output/text_output.cpp
    src/input/voice_input.cpp
)

target_include_directories(livesub PRIVATE src ${SHERPA_DIR}/include)
if(LIVESUB_PREBUILT_VULKAN)
    target_include_directories(livesub PRIVATE ${LIVESUB_LLAMA_INCLUDE})
endif()
target_link_libraries(livesub PRIVATE ${LIVESUB_LLAMA_LIBS} Threads::Threads)

if(WIN32)
    target_link_libraries(livesub PRIVATE
        ole32 oleaut32
        dwmapi
        d2d1 dwrite
        windowscodecs
        comctl32
    )
    target_compile_definitions(livesub PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# MinGW 运行时（libstdc++ 等）：构建时从编译器目录复制，保证打包/发布可独立运行
if(MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_RUNTIME_DLLS)
    foreach(_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${MINGW_BIN_DIR}/${_dll}")
            list(APPEND MINGW_RUNTIME_DLLS "${MINGW_BIN_DIR}/${_dll}")
        endif()
    endforeach()
    if(MINGW_RUNTIME_DLLS)
        add_custom_command(TARGET livesub POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MINGW_RUNTIME_DLLS} $<TARGET_FILE_DIR:livesub>)
    endif()
endif()

# sherpa-onnx 运行时 DLL（SenseVoice / 流式 zipformer 引擎用，动态加载）
add_custom_command(TARGET livesub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHERPA_DIR}/lib/sherpa-onnx-c-api.dll
        ${SHERPA_DIR}/lib/onnxruntime.dll
        ${SHERPA_DIR}/lib/onnxruntime_providers_shared.dll
        $<TARGET_FILE_DIR:livesub>)

# 预编译模式：运行时复制 DLL（ggml-cpu-* 为按 CPU 特性拆分的变体，运行时加载）
if(LIVESUB_PREBUILT_VULKAN)
    add_custom_command(TARGET livesub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${LIVESUB_SDK_DIR}/llama.dll
            ${LIVESUB_SDK_DIR}/mtmd.dll
            ${LIVESUB_SDK_DIR}/ggml.dll
            ${LIVESUB_SDK_DIR}/ggml-base.dll
            ${LIVESUB_SDK_DIR}/ggml-vulkan.dll
            ${LIVESUB_SDK_DIR}/llama-common.dll
            ${LIVESUB_SDK_DIR}/libomp140.x86_64.dll
            $<TARGET_FILE_DIR:livesub>)
    add_custom_command(TARGET livesub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${LIVESUB_SDK_DIR}/ggml-cpu-x64.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-haswell.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-ivybridge.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-piledriver.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-sandybridge.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-skylakex.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-alderlake.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-cannonlake.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-cascadelake.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-cooperlake.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-icelake.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-sapphirerapids.dll
            ${LIVESUB_SDK_DIR}/ggml-cpu-zen4.dll
            $<TARGET_FILE_DIR:livesub>)
endif()

# ---------------------------------------------------------------------------
# 测试与工具
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_resampler tests/test_resampler.cpp src/audio/resampler.cpp)
target_include_directories(test_resampler PRIVATE src)
target_compile_definitions(test_resampler PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME test_resampler COMMAND test_resampler)

add_executable(test_wav_asr tests/test_wav_asr.cpp src/asr/asr_engine.cpp src/config.cpp src/audio/resampler.cpp)
target_include_directories(test_wav_asr PRIVATE src)
if(LIVESUB_PREBUILT_VULKAN)
    target_include_directories(test_wav_asr PRIVATE ${LIVESUB_LLAMA_INCLUDE})
endif()
target_link_libraries(test_wav_asr PRIVATE ${LIVESUB_LLAMA_LIBS} Threads::Threads)
target_compile_definitions(test_wav_asr PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(record_test_wav tools/record_test_wav.cpp src/audio/wasapi_capture.cpp)
target_include_directories(record_test_wav PRIVATE src)
target_link_libraries(record_test_wav PRIVATE ole32 oleaut32)
target_compile_definitions(record_test_wav PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
