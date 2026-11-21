# windows specific compile definitions

add_compile_definitions(SOLSTICE_PLATFORM="windows")

enable_language(RC)
set(CMAKE_RC_COMPILER windres)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

# gcc complains about misleading indentation in some mingw includes
list(APPEND SOLSTICE_COMPILE_OPTIONS -Wno-misleading-indentation)

# Disable warnings for Windows ARM64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
    list(APPEND SOLSTICE_COMPILE_OPTIONS -Wno-dll-attribute-on-redeclaration)  # Boost
    list(APPEND SOLSTICE_COMPILE_OPTIONS -Wno-unknown-warning-option)  # ViGEmClient
    list(APPEND SOLSTICE_COMPILE_OPTIONS -Wno-unused-variable)  # Boost

    # Qt's static qwindows plugin and MinGW's Windowsapp import library both provide the
    # UiaRaiseNotificationEvent import thunk on ARM64. Keep both required libraries while
    # permitting lld to coalesce their identical definitions.
    if(SOLSTICE_USE_STATIC_QT AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND SOLSTICE_LINK_OPTIONS -Wl,--allow-multiple-definition)
    endif()
endif()

# see gcc bug 98723
add_definitions(-DUSE_BOOST_REGEX)

# curl
add_definitions(-DCURL_STATICLIB)
include_directories(SYSTEM ${CURL_STATIC_INCLUDE_DIRS})
link_directories(${CURL_STATIC_LIBRARY_DIRS})

# miniupnpc
add_definitions(-DMINIUPNP_STATICLIB)

# extra tools/binaries for audio/display devices
add_subdirectory(tools)  # todo - this is temporary, only tools for Windows are needed, for now

# libvirtualgamepad
include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/third-party/libvirtualgamepad/include")

# HIDMaestro is a managed SDK, so its bridge is published beside the executable
# and shipped under drivers/hidmaestro.
find_program(DOTNET_EXECUTABLE dotnet)
set(HIDMAESTRO_VERSION "2.3.0")
set(HIDMAESTRO_CACHE_DIR "${CMAKE_BINARY_DIR}/hidmaestro")
set(HIDMAESTRO_ARCHIVE "${HIDMAESTRO_CACHE_DIR}/HIDMaestro-v${HIDMAESTRO_VERSION}.zip")
set(HIDMAESTRO_CORE "${HIDMAESTRO_CACHE_DIR}/HIDMaestro.Core.dll")
if(DOTNET_EXECUTABLE)
    if(NOT EXISTS "${HIDMAESTRO_CORE}")
        file(MAKE_DIRECTORY "${HIDMAESTRO_CACHE_DIR}")
        if(NOT EXISTS "${HIDMAESTRO_ARCHIVE}")
            file(DOWNLOAD
                    "https://github.com/hifihedgehog/HIDMaestro/releases/download/v${HIDMAESTRO_VERSION}/HIDMaestro-v${HIDMAESTRO_VERSION}.zip"
                    "${HIDMAESTRO_ARCHIVE}"
                    EXPECTED_HASH SHA256=fddb61b76d076aa2cf508a46c1e30e23aa75b4ae622d52b15e7259959fd12295
                    SHOW_PROGRESS)
        endif()
        file(ARCHIVE_EXTRACT
                INPUT "${HIDMAESTRO_ARCHIVE}"
                DESTINATION "${HIDMAESTRO_CACHE_DIR}"
                PATTERNS "HIDMaestro.Core.dll")
    endif()

    add_custom_target(hidmaestro_bridge ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/drivers/hidmaestro"
            COMMAND "${DOTNET_EXECUTABLE}" publish
                    "${CMAKE_SOURCE_DIR}/src_assets/windows/hidmaestro-bridge/HIDMaestroBridge.csproj"
                    -c Release
                    -p:HIDMaestroCorePath=${HIDMAESTRO_CORE}
                    -o "${CMAKE_BINARY_DIR}/drivers/hidmaestro"
            BYPRODUCTS "${CMAKE_BINARY_DIR}/drivers/hidmaestro/HIDMaestroBridge.exe"
            COMMENT "Publishing the HIDMaestro bridge"
            VERBATIM)

    install(DIRECTORY "${CMAKE_BINARY_DIR}/drivers/hidmaestro/"
            DESTINATION "drivers/hidmaestro"
            COMPONENT assets
            PATTERN "*.pdb" EXCLUDE)
else()
    message(WARNING "dotnet was not found, so the HIDMaestro bridge is not built")
endif()

# nvidia
include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/third-party/nvapi")
file(GLOB NVPREFS_FILES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/third-party/nvapi/*.h"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/nvprefs/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/nvprefs/*.h")

# vigem
include_directories(SYSTEM "${CMAKE_SOURCE_DIR}/third-party/ViGEmClient/include")

# solstice icon
if(NOT DEFINED SOLSTICE_ICON_PATH)
    set(SOLSTICE_ICON_PATH "${CMAKE_SOURCE_DIR}/solstice.ico")
endif()

# Create a separate object library for the RC file with minimal includes
add_library(solstice_rc_object OBJECT "${CMAKE_SOURCE_DIR}/src/platform/windows/windows.rc")

# Set minimal properties for RC compilation - only what's needed for the resource file
# Otherwise compilation can fail due to "line too long" errors
set_target_properties(solstice_rc_object PROPERTIES
    COMPILE_DEFINITIONS "PROJECT_ICON_PATH=${SOLSTICE_ICON_PATH};PROJECT_NAME=${PROJECT_NAME};PROJECT_VENDOR=${SOLSTICE_PUBLISHER_NAME};PROJECT_VERSION=${PROJECT_VERSION};PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR};PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH};RC_VERSION_BUILD=${RC_VERSION_BUILD};RC_VERSION_REVISION=${RC_VERSION_REVISION}"  # cmake-lint: disable=C0301
    INCLUDE_DIRECTORIES ""
)

set(PLATFORM_TARGET_FILES
        "${CMAKE_SOURCE_DIR}/src/platform/windows/publish.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/misc.h"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/misc.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/input.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/hidmaestro.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/hidmaestro.h"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/vhf_gamepad.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/vhf_gamepad.h"
        "${CMAKE_SOURCE_DIR}/third-party/libvirtualgamepad/client/client.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/display.h"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/display_base.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/display_vram.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/display_ram.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/display_wgc.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/audio.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/utf_utils.cpp"
        "${CMAKE_SOURCE_DIR}/src/platform/windows/utf_utils.h"
        "${CMAKE_SOURCE_DIR}/third-party/ViGEmClient/src/ViGEmClient.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/ViGEmClient/include/ViGEm/Client.h"
        "${CMAKE_SOURCE_DIR}/third-party/ViGEmClient/include/ViGEm/Common.h"
        "${CMAKE_SOURCE_DIR}/third-party/ViGEmClient/include/ViGEm/Util.h"
        "${CMAKE_SOURCE_DIR}/third-party/ViGEmClient/include/ViGEm/km/BusShared.h"
        ${NVPREFS_FILES})

set(OPENSSL_LIBRARIES
        libssl.a
        libcrypto.a)

list(PREPEND PLATFORM_LIBRARIES
        ${CURL_STATIC_LIBRARIES}
        avrt
        d3d11
        D3DCompiler
        dwmapi
        dxgi
        iphlpapi
        ksuser
        libssp.a
        libstdc++.a
        libwinpthread.a
        minhook::minhook
        ntdll
        setupapi
        shlwapi
        synchronization.lib
        userenv
        ws2_32
        wsock32
)
