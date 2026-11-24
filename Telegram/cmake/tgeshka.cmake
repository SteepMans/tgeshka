# This file is part of TGeshka, an unofficial client
# based on Telegram Desktop (GPLv3).
#
# Included at the end of Telegram/CMakeLists.txt: own sources and
# the executable name taken from SourceFiles/tgeshka/tgeshka_brand.h.

target_sources(Telegram
PRIVATE
    ${src_loc}/tgeshka/tgeshka_boxes.cpp
    ${src_loc}/tgeshka/tgeshka_boxes.h
    ${src_loc}/tgeshka/tgeshka_brand.h
    ${src_loc}/tgeshka/tgeshka_gateway.cpp
    ${src_loc}/tgeshka/tgeshka_gateway.h
    ${src_loc}/tgeshka/tgeshka_integration.cpp
    ${src_loc}/tgeshka/tgeshka_integration.h
    ${src_loc}/tgeshka/tgeshka_lang.cpp
    ${src_loc}/tgeshka/tgeshka_lang.h
    ${src_loc}/tgeshka/tgeshka_settings.cpp
    ${src_loc}/tgeshka/tgeshka_settings.h
    ${src_loc}/tgeshka/tgeshka_transport_bridge.cpp
    ${src_loc}/tgeshka/tgeshka_transport_bridge.h
    ${src_loc}/tgeshka/tgeshka_update.cpp
    ${src_loc}/tgeshka/tgeshka_update.h
    ${src_loc}/tgeshka/tgeshka_update_ui.cpp
    ${src_loc}/tgeshka/tgeshka_update_ui.h
    ${src_loc}/tgeshka/tgeshka_update_verify.cpp
    ${src_loc}/tgeshka/tgeshka_update_verify.h
    ${src_loc}/tgeshka/tgeshka_whats_new.cpp
    ${src_loc}/tgeshka/tgeshka_whats_new.h
)

# Russian strings for the first launch, before the cloud language pack
# can be loaded through the gateway, and the "What's new" list from
# CHANGELOG-tgeshka.json in the repository root.
nice_target_sources(Telegram ${res_loc}
PRIVATE
    qrc/tgeshka/tgeshka.qrc
)

file(STRINGS ${src_loc}/tgeshka/tgeshka_brand.h tgeshka_app_file_line
    REGEX "^#define TGESHKA_APP_FILE ")
string(REGEX REPLACE "^#define TGESHKA_APP_FILE \"([^\"]+)\".*$" "\\1"
    tgeshka_app_file "${tgeshka_app_file_line}")

# Static Qt6Network pulls qdnslookup_win (DnsQuery_W and friends) through
# the gateway socket; upstream never referenced it, so dnsapi was not linked.
if (WIN32)
    target_link_libraries(Telegram PRIVATE dnsapi)
endif()

if (WIN32 AND tgeshka_app_file)
    set_target_properties(Telegram PROPERTIES OUTPUT_NAME ${tgeshka_app_file})
endif()

# Default gateway address is not in the sources: it is passed at configure
# time (-DTGESHKA_GATEWAY_URL=wss://host/path, CI takes it from a secret).
# Empty: the first launch asks for a tgeshka://gateway link or the address.
set(TGESHKA_GATEWAY_URL "" CACHE STRING "Default gateway address, wss://...")
if (TGESHKA_GATEWAY_URL)
    if (NOT TGESHKA_GATEWAY_URL MATCHES "^wss://[^\"\\ ]+$")
        message(FATAL_ERROR "TGESHKA_GATEWAY_URL must be a wss:// address.")
    endif()
    set_property(SOURCE ${src_loc}/tgeshka/tgeshka_gateway.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS
        "TGESHKA_DEFAULT_GATEWAY_URL=\"${TGESHKA_GATEWAY_URL}\"")
endif()

# TGeshka version of a release build, from its tag (build.ps1 -Version, CI),
# see docs/updates.md. Empty: the default from tgeshka_brand.h.
set(TGESHKA_VERSION "" CACHE STRING "TGeshka version, 1.2.3 or 1.2.3-beta.1")
if (TGESHKA_VERSION)
    if (NOT TGESHKA_VERSION MATCHES "^[0-9]+([.][0-9]+)([.][0-9]+)?([.][0-9]+)?(-[0-9A-Za-z.]+)?$")
        message(FATAL_ERROR "Bad TGESHKA_VERSION: ${TGESHKA_VERSION}")
    endif()
    target_compile_definitions(Telegram PRIVATE
        "TGESHKA_VERSION=\"${TGESHKA_VERSION}\"")
endif()

# Own updates use the stock Updater.exe only to install a package checked
# by tgeshka_update.cpp. Upstream builds it only with its auto-update,
# which stays disabled here (no requests to Telegram update servers).
if (WIN32 AND DESKTOP_APP_DISABLE_AUTOUPDATE AND NOT build_winstore)
    add_executable(Updater WIN32)
    init_non_host_target(Updater)

    add_dependencies(Telegram Updater)

    nice_target_sources(Updater ${src_loc}
    PRIVATE
        _other/updater_win.cpp
        _other/updater.h
    )

    set_target_properties(Updater PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${output_folder}
    )

    get_filename_component(lib_base_loc lib_base REALPATH)
    nice_target_sources(Updater ${lib_base_loc}
    PRIVATE
        base/platform/win/base_windows_safe_library.cpp
        base/platform/win/base_windows_safe_library.h
    )
    target_include_directories(Updater PRIVATE ${lib_base_loc})
    if (MSVC)
        target_link_libraries(Updater
        PRIVATE
            delayimp
        )
        target_link_options(Updater
        PRIVATE
            /DELAYLOAD:user32.dll
            /DELAYLOAD:advapi32.dll
            /DELAYLOAD:shell32.dll
            /DELAYLOAD:ole32.dll
            /DELAYLOAD:shlwapi.dll
        )
    else()
        target_link_options(Updater PRIVATE -municode)
    endif()
    if (DESKTOP_APP_WIN7_BASELINE)
        target_link_options(Updater PRIVATE /SUBSYSTEM:WINDOWS,6.01)
    endif()
endif()
