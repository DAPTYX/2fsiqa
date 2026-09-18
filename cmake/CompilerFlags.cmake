set(2FSIQA_STRICT_FP_FLAGS_COMMON
    -fno-fast-math
    -fno-associative-math
)

set(2FSIQA_STRICT_FP_FLAGS_CLANG
    -ffp-model=precise
)

set(2FSIQA_ARCH_FLAGS "" CACHE STRING
    "Codec-only -march/-mtune (zlib-ng png jpeg tiff webp avif); empty = compiler default")

function(toofsiqa_target_accepts_compile_options target_name out_var)
    set(_ok TRUE)
    if(NOT TARGET ${target_name})
        set(_ok FALSE)
    else()
        get_target_property(_type ${target_name} TYPE)
        if(_type STREQUAL "INTERFACE_LIBRARY")
            set(_ok FALSE)
        endif()
        get_target_property(_imported ${target_name} IMPORTED)
        if(_imported)
            set(_ok FALSE)
        endif()
        get_target_property(_aliased ${target_name} ALIASED_TARGET)
        if(_aliased)
            set(_ok FALSE)
        endif()
    endif()
    set(${out_var} ${_ok} PARENT_SCOPE)
endfunction()

function(toofsiqa_apply_strict_fp target_name)
    toofsiqa_target_accepts_compile_options(${target_name} _ok)
    if(NOT _ok)
        return()
    endif()

    set(_fp_flags ${2FSIQA_STRICT_FP_FLAGS_COMMON})
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
        list(APPEND _fp_flags ${2FSIQA_STRICT_FP_FLAGS_CLANG})
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:${_fp_flags}>
            $<$<COMPILE_LANGUAGE:CXX>:${_fp_flags}>
        )
    endif()
endfunction()

function(toofsiqa_apply_arch_flags target_name)
    if(NOT 2FSIQA_ARCH_FLAGS OR 2FSIQA_ARCH_FLAGS STREQUAL "")
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        return()
    endif()
    toofsiqa_target_accepts_compile_options(${target_name} _ok)
    if(NOT _ok)
        return()
    endif()

    separate_arguments(_arch_list NATIVE_COMMAND "${2FSIQA_ARCH_FLAGS}")
    if(NOT _arch_list)
        return()
    endif()

    target_compile_options(${target_name} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${_arch_list}>
        $<$<COMPILE_LANGUAGE:CXX>:${_arch_list}>
    )
endfunction()

function(toofsiqa_apply_arch_flags_codec_deps)
    set(_candidates
        zlib-ng zlibstatic zlib
        png_static png
        tiff tiff_static
        webpdecoder webp webpmux libwebpmux
        avif
    )
    foreach(_t IN LISTS _candidates)
        if(TARGET "${_t}")
            toofsiqa_apply_arch_flags("${_t}")
        endif()
    endforeach()
endfunction()
