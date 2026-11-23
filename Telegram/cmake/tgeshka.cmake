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
    ${src_loc}/tgeshka/tgeshka_transport_bridge.cpp
    ${src_loc}/tgeshka/tgeshka_transport_bridge.h
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
