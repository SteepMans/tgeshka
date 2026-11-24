# This file is part of TGeshka, an unofficial client
# based on Telegram Desktop (GPLv3).
#
# Included through CMAKE_PROJECT_INCLUDE by build.ps1 -NoPch (the CI compile cache).
# A compiler cache cannot reuse a precompiled header (/Yu), while the sources rely on
# what their precompiled header includes. So every header a target precompiles is
# force-included instead (/FI) and the precompiled header itself is turned off.

get_property(tgeshka_pch_deferred GLOBAL PROPERTY TGESHKA_PCH_DEFERRED)
if (NOT tgeshka_pch_deferred)
    set_property(GLOBAL PROPERTY TGESHKA_PCH_DEFERRED ON)

    function(tgeshka_collect_targets dir out)
        get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach (subdir ${subdirs})
            tgeshka_collect_targets("${subdir}" sub)
            list(APPEND targets ${sub})
        endforeach()
        set(${out} ${targets} PARENT_SCOPE)
    endfunction()

    function(tgeshka_force_include_pch)
        tgeshka_collect_targets("${CMAKE_SOURCE_DIR}" targets)
        set(count 0)
        foreach (target ${targets})
            get_target_property(type ${target} TYPE)
            if (type STREQUAL "INTERFACE_LIBRARY" OR type STREQUAL "UTILITY")
                continue()
            endif()
            get_target_property(from ${target} PRECOMPILE_HEADERS_REUSE_FROM)
            if (from)
                get_target_property(headers ${from} PRECOMPILE_HEADERS)
            else()
                get_target_property(headers ${target} PRECOMPILE_HEADERS)
            endif()
            if (NOT headers)
                continue()
            endif()
            foreach (header ${headers})
                if (header MATCHES "^\\$<\\$<COMPILE_LANGUAGE:([^>]*)>:(.*)>$")
                    # $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:path> -> the same condition for /FI.
                    target_compile_options(${target} PRIVATE
                        "$<$<COMPILE_LANGUAGE:${CMAKE_MATCH_1}>:/FI${CMAKE_MATCH_2}>")
                elseif (header MATCHES "^<(.*)>$")
                    target_compile_options(${target} PRIVATE "/FI${CMAKE_MATCH_1}")
                elseif (header MATCHES "^\\$<")
                    message(FATAL_ERROR "TGeshka -NoPch: unsupported precompiled header ${header} of ${target}")
                else()
                    target_compile_options(${target} PRIVATE "/FI${header}")
                endif()
            endforeach()
            set_target_properties(${target} PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
            math(EXPR count "${count} + 1")
        endforeach()
        message(STATUS "TGeshka -NoPch: precompiled headers force-included in ${count} targets")
    endfunction()

    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL tgeshka_force_include_pch)
endif()
