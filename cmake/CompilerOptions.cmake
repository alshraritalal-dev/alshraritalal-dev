function(talal_apply_compiler_options target_name)
    if(MSVC)
        target_compile_options(${target_name} INTERFACE
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
            /W4
            /MP${TALAL_LOCAL_COMPILE_JOBS}
            /arch:${TALAL_CPU_BASELINE}
            "$<$<CONFIG:Debug>:/Od>"
            "$<$<CONFIG:Debug>:/Zi>"
            "$<$<CONFIG:Debug>:/RTC1>"
            "$<$<CONFIG:RelWithDebInfo>:/O2>"
            "$<$<CONFIG:RelWithDebInfo>:/Zi>"
            "$<$<CONFIG:RelWithDebInfo>:/Gw>"
            "$<$<CONFIG:RelWithDebInfo>:/Gy>"
            "$<$<CONFIG:Release>:/O2>"
            "$<$<CONFIG:Release>:/GL>"
            "$<$<CONFIG:Release>:/Gw>"
            "$<$<CONFIG:Release>:/Gy>")

        target_compile_definitions(${target_name} INTERFACE
            "$<$<CONFIG:RelWithDebInfo>:TALAL_PROFILE_BUILD=1>"
            "$<$<CONFIG:Release>:NDEBUG=1>")

        target_link_options(${target_name} INTERFACE
            "$<$<CONFIG:Debug>:/DEBUG:FULL>"
            "$<$<CONFIG:RelWithDebInfo>:/DEBUG:FULL>"
            "$<$<CONFIG:Release>:/LTCG>"
            "$<$<CONFIG:Release>:/OPT:REF>"
            "$<$<CONFIG:Release>:/OPT:ICF>"
            "$<$<CONFIG:Release>:/CGTHREADS:${TALAL_LOCAL_COMPILE_JOBS}>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -mavx2
            "$<$<CONFIG:Debug>:-O0>"
            "$<$<CONFIG:Debug>:-g>"
            "$<$<CONFIG:RelWithDebInfo>:-O2>"
            "$<$<CONFIG:RelWithDebInfo>:-g>"
            "$<$<CONFIG:Release>:-O3>")
    else()
        message(WARNING "Compiler ${CMAKE_CXX_COMPILER_ID} has no DEMO_WORKSTATION-specific option profile.")
    endif()
endfunction()
