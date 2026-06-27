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
    src/AudioPlayerWidget.cpp
    src/RadioVideoDialog.cpp
    src/SubtitleBurnDialog.cpp
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
    set(MPV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv")
    set(MPV_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv.7z")

    if(NOT EXISTS "${MPV_DIR}/include/mpv/client.h")
        message(STATUS "libmpv not found. Querying latest release from shinchiro's repository...")
        set(API_URL "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest")
        set(JSON_FILE "${CMAKE_CURRENT_BINARY_DIR}/latest_mpv_release.json")
        file(DOWNLOAD "${API_URL}" "${JSON_FILE}" STATUS download_status)

        list(GET download_status 0 status_code)
        if(NOT status_code EQUAL 0)
            message(FATAL_ERROR "Failed to query latest libmpv release info. Status: ${download_status}")
        endif()

        file(READ "${JSON_FILE}" RELEASE_JSON)
        string(REGEX MATCH "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/[0-9]+/mpv-dev-x86_64-[0-9]+-git-[a-zA-Z0-9]+\\.7z" MPV_DEV_URL "${RELEASE_JSON}")

        if(NOT MPV_DEV_URL)
            message(FATAL_ERROR "Failed to parse download URL for libmpv-dev from release JSON.")
        endif()

        message(STATUS "Downloading libmpv from: ${MPV_DEV_URL}")
        file(MAKE_DIRECTORY "${MPV_DIR}")
        file(DOWNLOAD "${MPV_DEV_URL}" "${MPV_ARCHIVE}" SHOW_PROGRESS)

        message(STATUS "Extracting libmpv...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${MPV_ARCHIVE}"
            WORKING_DIRECTORY "${MPV_DIR}"
            RESULT_VARIABLE extract_result
        )
        if(NOT extract_result EQUAL 0)
            message(FATAL_ERROR "Failed to extract mpv archive. Please ensure Windows tar is available.")
        endif()
        file(REMOVE "${MPV_ARCHIVE}")

        # Debug print extracted files
        file(GLOB_RECURSE EXTRACTED_FILES RELATIVE "${MPV_DIR}" "${MPV_DIR}/*")
        message(STATUS "Extracted files in ${MPV_DIR}: ${EXTRACTED_FILES}")
    endif()

    # 如果是 MSVC，从 .def 文件生成 mpv.lib 导入库
    if(MSVC)
        if(NOT EXISTS "${MPV_DIR}/mpv.lib")
            message(STATUS "Generating MSVC-compatible mpv.lib using ${CMAKE_AR}...")
            execute_process(
                COMMAND "${CMAKE_AR}" /def:"${CMAKE_CURRENT_SOURCE_DIR}/src/libmpv.def" /out:"${MPV_DIR}/mpv.lib" /machine:x64
                WORKING_DIRECTORY "${MPV_DIR}"
                RESULT_VARIABLE lib_result
            )
            if(NOT lib_result EQUAL 0)
                message(WARNING "Failed to generate mpv.lib using ${CMAKE_AR}. Will try linking to libmpv.dll.a directly.")
                set(MPV_LINK_TARGET "${MPV_DIR}/libmpv.dll.a")
            else()
                set(MPV_LINK_TARGET "${MPV_DIR}/mpv.lib")
            endif()
        else()
            set(MPV_LINK_TARGET "${MPV_DIR}/mpv.lib")
        endif()
    else()
        set(MPV_LINK_TARGET "${MPV_DIR}/libmpv.dll.a")
    endif()

    target_include_directories(MediaCutter PRIVATE "${MPV_DIR}/include")
    target_link_libraries(MediaCutter PRIVATE Qt6::Widgets Qt6::OpenGLWidgets "${MPV_LINK_TARGET}")
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

    if(EXISTS "${VCPKG_BASE_DIR}")
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
    endif()

    add_custom_command(TARGET MediaCutter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/libmpv-2.dll"
        $<TARGET_FILE_DIR:MediaCutter>
    )
endif()
