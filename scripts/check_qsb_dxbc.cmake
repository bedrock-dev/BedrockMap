# Fails when a precompiled .qsb carries HLSL source instead of DXBC.
#
# qsb does not fail when fxc cannot be started, times out or rejects the
# shader; it silently stores HLSL source. D3D11 then compiles the shader at
# first paint, which stalls the first map frame for seconds.
#
# Usage: cmake -DQSB=<qsb.exe> -DSHADERS=<a.qsb;b.qsb> -DSTAMP=<file> -P check_qsb_dxbc.cmake

foreach(shader IN LISTS SHADERS)
    execute_process(
        COMMAND "${QSB}" --dump "${shader}"
        OUTPUT_VARIABLE dump
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "qsb --dump failed for ${shader}")
    endif()
    if(NOT dump MATCHES "DXBC 50")
        message(FATAL_ERROR
            "${shader} has no DXBC; fxc failed or was not found, so qsb shipped HLSL source.\n"
            "Make sure fxc.exe is reachable and the shader compiles with: fxc /E main /T ps_5_0.")
    endif()
endforeach()
file(TOUCH "${STAMP}")
