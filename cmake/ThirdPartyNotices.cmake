function(toofsiqa_git_identity source_dir out_hash out_date)
    set(_hash "")
    set(_date "")
    if(EXISTS "${source_dir}/.git" OR EXISTS "${source_dir}")
        execute_process(
            COMMAND git -C "${source_dir}" rev-parse HEAD
            OUTPUT_VARIABLE _hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _hr
        )
        if(NOT _hr EQUAL 0)
            set(_hash "")
        endif()
        execute_process(
            COMMAND git -C "${source_dir}" log -1 --format=%cs
            OUTPUT_VARIABLE _date
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _dr
        )
        if(NOT _dr EQUAL 0)
            set(_date "")
        endif()
    endif()
    set(${out_hash} "${_hash}" PARENT_SCOPE)
    set(${out_date} "${_date}" PARENT_SCOPE)
endfunction()

function(toofsiqa_notice_append out_var name repo tag source_dir copyright_text)
    set(_block "")
    string(APPEND _block "${name}\n")
    if(repo)
        string(APPEND _block "  source: ${repo}")
        if(tag)
            string(APPEND _block " @ ${tag}")
        endif()
        string(APPEND _block "\n")
    endif()
    toofsiqa_git_identity("${source_dir}" _hash _date)
    if(_hash)
        string(APPEND _block "  commit: ${_hash}")
        if(_date)
            string(APPEND _block " (${_date})")
        endif()
        string(APPEND _block "\n")
    elseif(tag)
        string(APPEND _block "  ref: ${tag}\n")
    endif()
    string(APPEND _block "${copyright_text}\n")
    set(${out_var} "${${out_var}}${_block}\n---\n\n" PARENT_SCOPE)
endfunction()

function(toofsiqa_write_third_party_notices)
    set(_body "")
    set(_any FALSE)

    if(NOT 2FSIQA_USE_SYSTEM_ZLIB_NG)
        set(_any TRUE)
        toofsiqa_notice_append(_body "zlib-ng"
            "${2FSIQA_ZLIB_NG_GIT_REPOSITORY}" "${2FSIQA_ZLIB_NG_GIT_TAG}"
            "${zlib_ng_SOURCE_DIR}"
            "(C) 1995-2024 Jean-loup Gailly and Mark Adler")
    endif()

    if(NOT 2FSIQA_USE_SYSTEM_LIBPNG)
        set(_any TRUE)
        toofsiqa_notice_append(_body "libpng"
            "${2FSIQA_LIBPNG_GIT_REPOSITORY}" "${2FSIQA_LIBPNG_GIT_TAG}"
            "${libpng_SOURCE_DIR}"
            "Copyright (c) 1995-2026 The PNG Reference Library Authors.\nCopyright (c) 2018-2026 Cosmin Truta.\nCopyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.\nCopyright (c) 1996-1997 Andreas Dilger.\nCopyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.")
    endif()

    if(NOT 2FSIQA_USE_SYSTEM_LIBJPEG)
        set(_any TRUE)
        toofsiqa_notice_append(_body "libjpeg-turbo"
            "${2FSIQA_LIBJPEG_GIT_REPOSITORY}" "${2FSIQA_LIBJPEG_GIT_TAG}"
            "${libjpeg_SOURCE_DIR}"
            "Copyright (C) 2009-2026 D. R. Commander\nCopyright (C) 2015-2021, 2023 Mozilla Foundation\nCopyright (C) 2018-2023 Randy randy408@protonmail.com")
    endif()

    if(NOT 2FSIQA_USE_SYSTEM_LIBTIFF)
        set(_any TRUE)
        toofsiqa_notice_append(_body "libtiff"
            "${2FSIQA_LIBTIFF_GIT_REPOSITORY}" "${2FSIQA_LIBTIFF_GIT_TAG}"
            "${libtiff_SOURCE_DIR}"
            "Copyright (c) 1988-1997 Sam Leffler\nCopyright (c) 1991-1997 Silicon Graphics, Inc.")
    endif()

    if(NOT 2FSIQA_USE_SYSTEM_LIBWEBP)
        set(_any TRUE)
        toofsiqa_notice_append(_body "libwebp"
            "${2FSIQA_LIBWEBP_GIT_REPOSITORY}" "${2FSIQA_LIBWEBP_GIT_TAG}"
            "${libwebp_SOURCE_DIR}"
            "Copyright (c) 2010, Google Inc. All rights reserved.")
    endif()

    if(NOT 2FSIQA_USE_SYSTEM_LIBAVIF)
        set(_any TRUE)
        toofsiqa_notice_append(_body "libavif"
            "${2FSIQA_LIBAVIF_GIT_REPOSITORY}" "${2FSIQA_LIBAVIF_GIT_TAG}"
            "${libavif_SOURCE_DIR}"
            "Copyright 2019 Joe Drago. All rights reserved.")
        set(_dav1d_src "")
        if(DEFINED dav1d_SOURCE_DIR)
            set(_dav1d_src "${dav1d_SOURCE_DIR}")
        elseif(EXISTS "${libavif_SOURCE_DIR}/ext/dav1d")
            set(_dav1d_src "${libavif_SOURCE_DIR}/ext/dav1d")
        endif()
        toofsiqa_notice_append(_body "dav1d (via libavif)"
            "https://code.videolan.org/videolan/dav1d.git" ""
            "${_dav1d_src}"
            "Copyright (c) 2018-2025, VideoLAN and dav1d authors")
    endif()

    if(NOT 2FSIQA_USE_SYSTEM_LCMS2)
        set(_any TRUE)
        toofsiqa_notice_append(_body "Little-CMS"
            "${2FSIQA_LCMS2_GIT_REPOSITORY}" "${2FSIQA_LCMS2_GIT_TAG}"
            "${lcms2_SOURCE_DIR}"
            "Copyright (c) 1998-2026 Marti Maria Saguer")
    endif()

    if(NOT 2FSIQA_USE_SYSTEM_HIGHWAY)
        set(_any TRUE)
        toofsiqa_notice_append(_body "highway"
            "${2FSIQA_HIGHWAY_GIT_REPOSITORY}" "${2FSIQA_HIGHWAY_GIT_TAG}"
            "${highway_SOURCE_DIR}"
            "Copyright (c) The Highway Project Authors. All rights reserved.")
    endif()

    if(2FSIQA_LIBCXX STREQUAL "static_owned")
        set(_any TRUE)
        toofsiqa_notice_append(_body "libcxx / libcxxabi / libunwind (LLVM)"
            "${2FSIQA_LIBCXX_GIT_REPOSITORY}" "${2FSIQA_LIBCXX_GIT_TAG}"
            "${llvm_project_SOURCE_DIR}"
            "Copyright (c) 2003-2019 University of Illinois at Urbana-Champaign.\nCopyright (c) 2009-2024 by the contributors listed in CREDITS.TXT\nAll rights reserved.")
    endif()

    if(NOT _any)
        return()
    endif()

    set(_text "[statically linked into 2fsiqa binary]\n\n")
    string(APPEND _text "${_body}")

    set(_out_dir "${CMAKE_BINARY_DIR}/bin")
    file(MAKE_DIRECTORY "${_out_dir}")
    set(_out_file "${_out_dir}/THIRD_PARTY_NOTICES.txt")
    file(WRITE "${_out_file}" "${_text}")
    set(2FSIQA_THIRD_PARTY_NOTICES_FILE "${_out_file}" PARENT_SCOPE)
    message(STATUS "Third-party notices: ${_out_file}")
endfunction()
