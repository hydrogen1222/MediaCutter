cmake_minimum_required(VERSION 3.16)
project(MediaCutter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets LinguistTools)

add_executable(MediaCutter
    src/main.cpp
    src/MainWindow.cpp
    src/MpvWidget.cpp
    src/ClipModel.cpp
    src/FFmpegRunner.cpp
    src/resources.qrc
)

# --- 翻译 ---
set(TS_FILES translations/media-cutter_zh_CN.ts)
qt_add_lrelease(MediaCutter
    TS_FILES ${TS_FILES}
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)
qt_add_resources(MediaCutter "translations"
    PREFIX "/translations"
    BASE   "${CMAKE_CURRENT_BINARY_DIR}"
    FILES  ${QM_FILES}
)

# --- libmpv ---
if(WIN32)
    # Windows: 自动下载预编译的 mpv-dev 包到 third_party/mpv
    set(MPV_DEV_URL "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20240512/mpv-dev-x86_64-20240512-git-f99a9b6.7z")
    set(MPV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv")
    set(MPV_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv.7z")

    if(NOT EXISTS "${MPV_DIR}/include/mpv/client.h")
        message(STATUS "libmpv not found. Downloading from shinchiro release...")
        file(MAKE_DIRECTORY "${MPV_DIR}")
        file(DOWNLOAD "${MPV_DEV_URL}" "${MPV_ARCHIVE}" SHOW_PROGRESS)

        message(STATUS "Extracting libmpv...")
        execute_process(
            COMMAND tar -xf "${MPV_ARCHIVE}" -C "${MPV_DIR}"
            RESULT_VARIABLE extract_result
        )
        if(NOT extract_result EQUAL 0)
            message(FATAL_ERROR "Failed to extract mpv archive. Please ensure Windows tar is available.")
        endif()
        file(REMOVE "${MPV_ARCHIVE}")
    endif()

    target_include_directories(MediaCutter PRIVATE "${MPV_DIR}/include")
    target_link_libraries(MediaCutter PRIVATE Qt6::Widgets Qt6::OpenGLWidgets "${MPV_DIR}/lib/mpv.lib")
else()
    # Linux/macOS: 通过 pkg-config 使用系统安装的 libmpv
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    target_link_libraries(MediaCutter PRIVATE Qt6::Widgets Qt6::OpenGLWidgets PkgConfig::MPV)
endif()

# --- Windows 专属：vcpkg Qt 插件部署 + libmpv DLL 拷贝 ---
if(WIN32)
    # 尝试自动检测 vcpkg 安装路径
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_BASE_DIR "$ENV{VCPKG_ROOT}/installed/x64-windows")
    else()
        # 默认路径，如果未定义环境变量
        set(VCPKG_BASE_DIR "C:/vcpkg/installed/x64-windows")
    endif()

    set(VCPKG_PLUGINS_RELEASE "${VCPKG_BASE_DIR}/Qt6/plugins")
    set(VCPKG_PLUGINS_DEBUG "${VCPKG_BASE_DIR}/debug/Qt6/plugins")

    set(PLUGIN_SRC_DIR "$<$<CONFIG:Debug>:${VCPKG_PLUGINS_DEBUG}>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_PLUGINS_RELEASE}>")
    set(DEBUG_SUFFIX "$<$<CONFIG:Debug>:d>")

    add_custom_command(TARGET MediaCutter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MediaCutter>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLUGIN_SRC_DIR}/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:MediaCutter>/platforms/qwindows${DEBUG_SUFFIX}.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MediaCutter>/styles"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLUGIN_SRC_DIR}/styles/qmodernwindowsstyle${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:MediaCutter>/styles/qmodernwindowsstyle${DEBUG_SUFFIX}.dll"
        COMMENT "Manually deploying Qt plugins from vcpkg..."
        COMMAND_EXPAND_LISTS
    )

    add_custom_command(TARGET MediaCutter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/lib/libmpv-2.dll"
        $<TARGET_FILE_DIR:MediaCutter>
    )
endif()
