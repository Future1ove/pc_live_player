cmake_minimum_required(VERSION 3.20)
project(douyin_qr_c VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_QRENCODE "Enable QR popup generation with libqrencode" ON)
option(BUILD_GUI "Build Qt6 GUI (douyin_qr_gui)" ON)
option(BUILD_CLI "Build SDL command-line douyin_qr_c (second exe; optional)" OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED)
endif()

find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY avformat)
find_library(AVCODEC_LIBRARY avcodec)
find_library(AVUTIL_LIBRARY avutil)
find_library(SWSCALE_LIBRARY swscale)
find_library(SWRESAMPLE_LIBRARY swresample)

find_path(ZBAR_INCLUDE_DIR zbar.h)
find_library(ZBAR_LIBRARY zbar)

if(NOT FFMPEG_INCLUDE_DIR OR NOT AVFORMAT_LIBRARY OR NOT AVCODEC_LIBRARY OR NOT AVUTIL_LIBRARY OR NOT SWSCALE_LIBRARY OR NOT SWRESAMPLE_LIBRARY)
    message(FATAL_ERROR "未找到 FFmpeg 开发库（至少需要 avformat/avcodec/avutil/swscale/swresample）。")
endif()

if(NOT ZBAR_INCLUDE_DIR OR NOT ZBAR_LIBRARY)
    message(FATAL_ERROR "未找到 ZBar 开发库，请安装 zbar。")
endif()

if(ENABLE_QRENCODE)
    # 勿先 set(QRENCODE_* "")：CMake 认为变量已存在则 find_path/find_library 不会搜索。
    # 与 FFmpeg/ZBar 不同，若不显式 HINTS vcpkg_installed，configure 可能找不到 qrencode。
    set(_qrencode_vcpkg_roots "")
    if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET)
        if(DEFINED VCPKG_INSTALLED_DIR)
            list(APPEND _qrencode_vcpkg_roots "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        endif()
        list(APPEND _qrencode_vcpkg_roots "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
    endif()
    find_path(
        QRENCODE_INCLUDE_DIR
        NAMES qrencode.h
        HINTS ${_qrencode_vcpkg_roots}
        PATH_SUFFIXES include
    )
    find_library(
        QRENCODE_LIBRARY
        NAMES qrencode libqrencode
        HINTS ${_qrencode_vcpkg_roots}
        PATH_SUFFIXES lib
    )
endif()

set(CORE_SOURCES
    src/app_config.c
    src/audio_output.c
    src/ffmpeg_stream.c
    src/latency_monitor.c
    src/qr_detector.c
    src/status_history.c
)

add_library(douyin_core STATIC ${CORE_SOURCES})

target_include_directories(
    douyin_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${FFMPEG_INCLUDE_DIR}
        ${ZBAR_INCLUDE_DIR}
)

if(SDL2_INCLUDE_DIRS)
    target_include_directories(douyin_core PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

target_link_libraries(
    douyin_core
    PUBLIC
        ${AVFORMAT_LIBRARY}
        ${AVCODEC_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${SWSCALE_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        ${ZBAR_LIBRARY}
)

if(TARGET SDL2::SDL2)
    target_link_libraries(douyin_core PUBLIC SDL2::SDL2)
elseif(SDL2_LIBRARIES)
    target_link_libraries(douyin_core PUBLIC ${SDL2_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(douyin_core PUBLIC ws2_32)
endif()

if(BUILD_CLI)
    add_executable(douyin_qr_c src/main.c src/app.c src/ui_sdl.c)

    target_include_directories(
        douyin_qr_c
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${FFMPEG_INCLUDE_DIR}
            ${ZBAR_INCLUDE_DIR}
    )

    if(SDL2_INCLUDE_DIRS)
        target_include_directories(douyin_qr_c PRIVATE ${SDL2_INCLUDE_DIRS})
    endif()

    if(QRENCODE_INCLUDE_DIR AND QRENCODE_LIBRARY)
        target_include_directories(douyin_qr_c PRIVATE ${QRENCODE_INCLUDE_DIR})
        target_compile_definitions(douyin_qr_c PRIVATE HAVE_QRENCODE=1)
    endif()

    target_link_libraries(douyin_qr_c PRIVATE douyin_core)

    if(QRENCODE_LIBRARY)
        target_link_libraries(douyin_qr_c PRIVATE ${QRENCODE_LIBRARY})
    endif()

    if(TARGET SDL2::SDL2main)
        target_link_libraries(douyin_qr_c PRIVATE SDL2::SDL2main)
    endif()
endif()

if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

    set(CMAKE_AUTOMOC ON)

    set(GUI_SOURCES
        gui/gui_main.cpp
        gui/MainWindow.cpp
        gui/StreamWorker.cpp
        gui/QRPopupDialog.cpp
        gui/QrImageWidget.cpp
        gui/KeyAuthClient.cpp
        gui/KeyAuthDialog.cpp
        gui/KeyHeartbeat.cpp
        gui/MainWindow.hpp
        gui/StreamWorker.hpp
        gui/QRPopupDialog.hpp
        gui/QrImageWidget.hpp
        gui/api_config.hpp
        gui/KeyAuthClient.hpp
        gui/KeyAuthDialog.hpp
        gui/KeyHeartbeat.hpp
    )

    if(WIN32)
        add_executable(douyin_qr_gui WIN32 ${GUI_SOURCES})
    else()
        add_executable(douyin_qr_gui ${GUI_SOURCES})
    endif()

    target_include_directories(
        douyin_qr_gui
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/gui
            ${FFMPEG_INCLUDE_DIR}
            ${ZBAR_INCLUDE_DIR}
    )

    if(QRENCODE_INCLUDE_DIR AND QRENCODE_LIBRARY)
        target_include_directories(douyin_qr_gui PRIVATE ${QRENCODE_INCLUDE_DIR})
        target_compile_definitions(douyin_qr_gui PRIVATE HAVE_QRENCODE=1)
    endif()

    target_link_libraries(douyin_qr_gui PRIVATE douyin_core Qt6::Widgets Qt6::Network)

    if(QRENCODE_LIBRARY)
        target_link_libraries(douyin_qr_gui PRIVATE ${QRENCODE_LIBRARY})
    endif()

    # 默认仅生成此一个带界面 exe（CMake 目标名仍为 douyin_qr_gui）
    set_target_properties(douyin_qr_gui PROPERTIES OUTPUT_NAME "douyin_qr")

    # Windows：PerMonitorV2 DPI，避免 125%/150% 缩放下界面模糊且输入框无法聚焦
    if(MSVC)
        set(_app_manifest "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/app.manifest")
        if(EXISTS "${_app_manifest}")
            target_sources(douyin_qr_gui PRIVATE "${_app_manifest}")
        endif()
    endif()

    # Windows：复制 Qt 平台插件（platforms/qwindows.dll 等），否则运行时报
    # "no Qt platform plugin could be initialized"
    if(WIN32)
        set(_windeployqt_hints "")
        if(DEFINED VCPKG_TARGET_TRIPLET)
            list(
                APPEND
                _windeployqt_hints
                "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/Qt6/bin"
            )
        endif()
        list(APPEND _windeployqt_hints "${Qt6_DIR}/../../../tools/Qt6/bin")
        find_program(
            WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt windeployqt6
            HINTS ${_windeployqt_hints}
        )
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(
                TARGET douyin_qr_gui
                POST_BUILD
                COMMAND
                    "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:douyin_qr_gui>"
                COMMENT "windeployqt: Qt platforms、VC 运行库与依赖 DLL（便于免装 vc_redist）"
            )
        else()
            message(
                WARNING
                "未找到 windeployqt；请手动对 douyin_qr.exe 执行 windeployqt，或把 PATH 指向 vcpkg 的 Qt tools/bin。"
            )
        endif()

        # MSBuild 下常无 VCINSTALLDIR，windeployqt 不会带上 vcruntime/msvcp；从本机 VS Redist 目录复制到 exe 旁。
        if(MSVC)
            set(_vs_vc_root "${CMAKE_CXX_COMPILER}")
            foreach(_i RANGE 6)
                get_filename_component(_vs_vc_root "${_vs_vc_root}" DIRECTORY)
            endforeach()
            # 勿用「字典序最后一个」子目录：Redist/MSVC 下常有 v143（仅 vc_redist 安装包）无 CRT，应匹配 14.xx 下的 x64。
            file(GLOB _msvc_crt_dlls "${_vs_vc_root}/Redist/MSVC/*/x64/Microsoft.VC*.CRT/*.dll")
            if(_msvc_crt_dlls)
                set(_msvc_crt_cmds "")
                foreach(_dll IN LISTS _msvc_crt_dlls)
                    list(
                        APPEND
                        _msvc_crt_cmds
                        COMMAND
                        ${CMAKE_COMMAND}
                        -E
                        copy_if_different
                        "${_dll}"
                        "$<TARGET_FILE_DIR:douyin_qr_gui>"
                    )
                endforeach()
                add_custom_command(
                    TARGET douyin_qr_gui
                    POST_BUILD
                    ${_msvc_crt_cmds}
                    COMMENT "复制 MSVC 运行库 (vcruntime140/msvcp140 等) 与 exe 同目录"
                )
            else()
                message(
                    WARNING
                    "未在「${_vs_vc_root}/Redist/MSVC」下找到 Microsoft.VC*.CRT，无法自动复制运行库；对方机需单独安装 VC++ x64 运行库。"
                )
            endif()
        endif()
    endif()
endif()
