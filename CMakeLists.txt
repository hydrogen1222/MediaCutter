cmake_minimum_required(VERSION 3.16)
project(MediaCutter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(MediaCutter 
    src/main.cpp 
    src/MainWindow.cpp
    src/MpvWidget.cpp
    src/ClipModel.cpp
    src/FFmpegRunner.cpp
)

target_include_directories(MediaCutter PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/include
)

target_link_libraries(MediaCutter PRIVATE 
    Qt6::Widgets 
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/lib/mpv.lib"
)

# --- 优化：自动处理 Windows 下的 Qt 插件部署 ---
if(WIN32)
    # 区分 Debug 和 Release 的 vcpkg 插件路径
    set(VCPKG_BASE_DIR "C:/vcpkg/installed/x64-windows")
    set(VCPKG_PLUGINS_RELEASE "${VCPKG_BASE_DIR}/Qt6/plugins")
    set(VCPKG_PLUGINS_DEBUG "${VCPKG_BASE_DIR}/debug/Qt6/plugins")
    
    # 根据当前构建类型选择路径和文件名
    set(PLUGIN_SRC_DIR "$<$<CONFIG:Debug>:${VCPKG_PLUGINS_DEBUG}>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_PLUGINS_RELEASE}>")
    set(DEBUG_SUFFIX "$<$<CONFIG:Debug>:d>")

    add_custom_command(TARGET MediaCutter POST_BUILD
        # 创建 platforms 目录并拷贝 qwindows.dll / qwindowsd.dll
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MediaCutter>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLUGIN_SRC_DIR}/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:MediaCutter>/platforms/qwindows${DEBUG_SUFFIX}.dll"
        # 拷贝 styles 插件 (修正为 qmodernwindowsstyle)
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MediaCutter>/styles"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PLUGIN_SRC_DIR}/styles/qmodernwindowsstyle${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:MediaCutter>/styles/qmodernwindowsstyle${DEBUG_SUFFIX}.dll"
        COMMENT "Manually deploying Qt plugins from vcpkg..."
        COMMAND_EXPAND_LISTS
    )
endif()
# ---------------------------------------

add_custom_command(TARGET MediaCutter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mpv/lib/libmpv-2.dll"
    $<TARGET_FILE_DIR:MediaCutter>
)
