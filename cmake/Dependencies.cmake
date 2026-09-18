include(FetchContent)

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE INTERNAL "")

function(toofsiqa_fetch_dep dep_name git_repo git_tag source_dir_var)
    if(source_dir_var AND NOT "${source_dir_var}" STREQUAL "")
        set(_src "${source_dir_var}")
        if(NOT EXISTS "${_src}")
            message(FATAL_ERROR "${dep_name}: SOURCE_DIR does not exist: ${_src}")
        endif()
        set(${dep_name}_SOURCE_DIR "${_src}" PARENT_SCOPE)
        set(${dep_name}_BINARY_DIR "${2FSIQA_FETCHCONTENT_BASE}/${dep_name}-build" PARENT_SCOPE)
        return()
    endif()

    FetchContent_Declare(
        ${dep_name}
        GIT_REPOSITORY ${git_repo}
        GIT_TAG ${git_tag}
        GIT_SHALLOW TRUE
        SOURCE_DIR "${2FSIQA_FETCHCONTENT_BASE}/${dep_name}-src"
        BINARY_DIR "${2FSIQA_FETCHCONTENT_BASE}/${dep_name}-build"
        EXCLUDE_FROM_ALL
    )
    FetchContent_GetProperties(${dep_name})
    if(NOT ${dep_name}_POPULATED)
        cmake_policy(PUSH)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(${dep_name})
        cmake_policy(POP)
    endif()
    set(${dep_name}_SOURCE_DIR "${${dep_name}_SOURCE_DIR}" PARENT_SCOPE)
    set(${dep_name}_BINARY_DIR "${${dep_name}_BINARY_DIR}" PARENT_SCOPE)
endfunction()

function(toofsiqa_fetch_and_subdir dep_name git_repo git_tag source_dir_var)
    if(source_dir_var AND NOT "${source_dir_var}" STREQUAL "")
        set(_src "${source_dir_var}")
        if(NOT EXISTS "${_src}")
            message(FATAL_ERROR "${dep_name}: SOURCE_DIR does not exist: ${_src}")
        endif()
        set(${dep_name}_SOURCE_DIR "${_src}" PARENT_SCOPE)
        add_subdirectory("${_src}" "${2FSIQA_FETCHCONTENT_BASE}/${dep_name}-build" EXCLUDE_FROM_ALL)
        return()
    endif()

    FetchContent_Declare(
        ${dep_name}
        GIT_REPOSITORY ${git_repo}
        GIT_TAG ${git_tag}
        GIT_SHALLOW TRUE
        SOURCE_DIR "${2FSIQA_FETCHCONTENT_BASE}/${dep_name}-src"
        BINARY_DIR "${2FSIQA_FETCHCONTENT_BASE}/${dep_name}-build"
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(${dep_name})
    set(${dep_name}_SOURCE_DIR "${${dep_name}_SOURCE_DIR}" PARENT_SCOPE)
    set(${dep_name}_BINARY_DIR "${${dep_name}_BINARY_DIR}" PARENT_SCOPE)
endfunction()

function(toofsiqa_resolve_real_target out_var candidate)
    set(_t "${candidate}")
    while(TARGET "${_t}")
        get_target_property(_aliased "${_t}" ALIASED_TARGET)
        if(_aliased)
            set(_t "${_aliased}")
        else()
            break()
        endif()
    endwhile()
    set(${out_var} "${_t}" PARENT_SCOPE)
endfunction()

macro(toofsiqa_resolve_zlib_ng)
    if(2FSIQA_USE_SYSTEM_ZLIB_NG)
        find_package(ZLIB REQUIRED)
        set(2FSIQA_ZLIB_TARGET ZLIB::ZLIB)
    else()
        set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)
        set(ZLIB_ALIASES ON CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(WITH_GTEST OFF CACHE BOOL "" FORCE)
        set(WITH_FUZZERS OFF CACHE BOOL "" FORCE)
        set(WITH_BENCHMARKS OFF CACHE BOOL "" FORCE)
        set(INSTALL_UTILS OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        toofsiqa_fetch_and_subdir(zlib_ng
            "${2FSIQA_ZLIB_NG_GIT_REPOSITORY}"
            "${2FSIQA_ZLIB_NG_GIT_TAG}"
            "${2FSIQA_ZLIB_NG_SOURCE_DIR}")

        set(_zlib_candidate "")
        if(TARGET zlibstatic)
            set(_zlib_candidate zlibstatic)
        elseif(TARGET zlib-ng-static)
            set(_zlib_candidate zlib-ng-static)
        elseif(TARGET zlib)
            set(_zlib_candidate zlib)
        elseif(TARGET zlib-ng)
            set(_zlib_candidate zlib-ng)
        else()
            message(FATAL_ERROR "zlib-ng did not produce a zlib-compatible target")
        endif()
        toofsiqa_resolve_real_target(2FSIQA_ZLIB_TARGET "${_zlib_candidate}")
        if(NOT TARGET "${2FSIQA_ZLIB_TARGET}")
            message(FATAL_ERROR "zlib-ng target resolution failed for '${_zlib_candidate}'")
        endif()

        if(NOT TARGET ZLIB::ZLIB)
            add_library(ZLIB::ZLIB ALIAS ${2FSIQA_ZLIB_TARGET})
        endif()

        set(_zlib_ng_build_inc "${CMAKE_BINARY_DIR}/_deps/zlib_ng-build")
        set(_zlib_ng_src_inc "${zlib_ng_SOURCE_DIR}")
        set(ZLIB_FOUND TRUE CACHE INTERNAL "zlib-ng provides ZLIB")
        set(ZLIB_LIBRARY ${2FSIQA_ZLIB_TARGET} CACHE FILEPATH "zlib-ng target" FORCE)
        set(ZLIB_LIBRARIES ${2FSIQA_ZLIB_TARGET} CACHE STRING "zlib-ng target" FORCE)
        set(ZLIB_INCLUDE_DIR "${_zlib_ng_build_inc}" CACHE PATH "zlib-ng headers" FORCE)
        set(ZLIB_INCLUDE_DIRS "${_zlib_ng_build_inc};${_zlib_ng_src_inc}" CACHE STRING "zlib-ng headers" FORCE)
    endif()
endmacro()

macro(toofsiqa_resolve_libpng)
    if(2FSIQA_USE_SYSTEM_LIBPNG)
        find_package(PNG REQUIRED)
        set(2FSIQA_PNG_TARGET PNG::PNG)
    else()
        set(PNG_SHARED OFF CACHE BOOL "" FORCE)
        set(PNG_STATIC ON CACHE BOOL "" FORCE)
        set(PNG_TESTS OFF CACHE BOOL "" FORCE)
        set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
        set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
        set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        if(DEFINED ZLIB_INCLUDE_DIRS)
            set(CMAKE_REQUIRED_INCLUDES "${ZLIB_INCLUDE_DIRS}")
        endif()
        toofsiqa_fetch_and_subdir(libpng
            "${2FSIQA_LIBPNG_GIT_REPOSITORY}"
            "${2FSIQA_LIBPNG_GIT_TAG}"
            "${2FSIQA_LIBPNG_SOURCE_DIR}")
        if(TARGET png_static)
            set(2FSIQA_PNG_TARGET png_static)
        elseif(TARGET png)
            set(2FSIQA_PNG_TARGET png)
        else()
            message(FATAL_ERROR "libpng did not produce a static target")
        endif()
    endif()
endmacro()

macro(toofsiqa_resolve_libjpeg)
    if(2FSIQA_USE_SYSTEM_LIBJPEG)
        find_package(JPEG REQUIRED)
        set(2FSIQA_JPEG_TARGET JPEG::JPEG)
    else()
        include(ExternalProject)
        toofsiqa_fetch_dep(libjpeg
            "${2FSIQA_LIBJPEG_GIT_REPOSITORY}"
            "${2FSIQA_LIBJPEG_GIT_TAG}"
            "${2FSIQA_LIBJPEG_SOURCE_DIR}")
        set(_jpeg_src "${libjpeg_SOURCE_DIR}")
        set(_jpeg_bin "${CMAKE_BINARY_DIR}/_deps/libjpeg-build")
        set(_jpeg_install "${CMAKE_BINARY_DIR}/_deps/libjpeg-install")
        file(MAKE_DIRECTORY "${_jpeg_install}/include")
        file(MAKE_DIRECTORY "${_jpeg_install}/lib")
        set(_jpeg_ep_args
            -Wno-author
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_INSTALL_PREFIX=${_jpeg_install}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DENABLE_SHARED=OFF
            -DENABLE_STATIC=ON
            -DWITH_TURBOJPEG=OFF
            -DWITH_TOOLS=OFF
            -DWITH_TESTS=OFF
            -DWITH_FUZZ=OFF
            -DBUILD_SHARED_LIBS=OFF
        )
        if(2FSIQA_ARCH_FLAGS AND NOT 2FSIQA_ARCH_FLAGS STREQUAL "")
            list(APPEND _jpeg_ep_args "-DCMAKE_C_FLAGS=${2FSIQA_ARCH_FLAGS}")
        endif()
        ExternalProject_Add(toofsiqa_libjpeg_ep
            SOURCE_DIR "${_jpeg_src}"
            BINARY_DIR "${_jpeg_bin}"
            CMAKE_ARGS ${_jpeg_ep_args}
            BUILD_BYPRODUCTS
                "${_jpeg_install}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}"
        )
        add_library(toofsiqa_jpeg STATIC IMPORTED GLOBAL)
        set_target_properties(toofsiqa_jpeg PROPERTIES
            IMPORTED_LOCATION
                "${_jpeg_install}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}"
            INTERFACE_INCLUDE_DIRECTORIES
                "${_jpeg_install}/include"
        )
        add_dependencies(toofsiqa_jpeg toofsiqa_libjpeg_ep)
        set(2FSIQA_JPEG_TARGET toofsiqa_jpeg)

        if(NOT TARGET JPEG::JPEG)
            add_library(JPEG::JPEG ALIAS toofsiqa_jpeg)
        endif()

        set(JPEG_FOUND TRUE CACHE INTERNAL "libjpeg-turbo provides JPEG")
        set(JPEG_LIBRARY toofsiqa_jpeg CACHE FILEPATH "libjpeg-turbo target" FORCE)
        set(JPEG_LIBRARIES toofsiqa_jpeg CACHE STRING "libjpeg-turbo target" FORCE)
        set(JPEG_INCLUDE_DIR "${_jpeg_install}/include" CACHE PATH "libjpeg-turbo headers" FORCE)
        set(JPEG_INCLUDE_DIRS "${_jpeg_install}/include" CACHE STRING "libjpeg-turbo headers" FORCE)
    endif()
endmacro()

macro(toofsiqa_resolve_libtiff)
    if(2FSIQA_USE_SYSTEM_LIBTIFF)
        find_package(TIFF REQUIRED)
        set(2FSIQA_TIFF_TARGET TIFF::TIFF)
    else()
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(tiff-static ON CACHE BOOL "" FORCE)
        set(tiff-tools OFF CACHE BOOL "" FORCE)
        set(tiff-tests OFF CACHE BOOL "" FORCE)
        set(tiff-docs OFF CACHE BOOL "" FORCE)
        set(tiff-contrib OFF CACHE BOOL "" FORCE)
        set(tiff-deprecated OFF CACHE BOOL "" FORCE)
        set(tiff-install OFF CACHE BOOL "" FORCE)
        set(tiff-cxx OFF CACHE BOOL "" FORCE)
        set(tiff-opengl OFF CACHE BOOL "" FORCE)
        set(zlib ON CACHE BOOL "" FORCE)
        set(jpeg ON CACHE BOOL "" FORCE)
        set(jbig OFF CACHE BOOL "" FORCE)
        set(lerc OFF CACHE BOOL "" FORCE)
        set(lzma OFF CACHE BOOL "" FORCE)
        set(zstd OFF CACHE BOOL "" FORCE)
        set(webp OFF CACHE BOOL "" FORCE)
        set(libdeflate OFF CACHE BOOL "" FORCE)
        set(old-jpeg OFF CACHE BOOL "" FORCE)
        toofsiqa_fetch_and_subdir(libtiff
            "${2FSIQA_LIBTIFF_GIT_REPOSITORY}"
            "${2FSIQA_LIBTIFF_GIT_TAG}"
            "${2FSIQA_LIBTIFF_SOURCE_DIR}")
        if(TARGET tiff)
            set(2FSIQA_TIFF_TARGET tiff)
        elseif(TARGET tiff_static)
            set(2FSIQA_TIFF_TARGET tiff_static)
        else()
            message(FATAL_ERROR "libtiff did not produce a tiff target")
        endif()
    endif()
endmacro()

macro(toofsiqa_resolve_libwebp)
    if(2FSIQA_USE_SYSTEM_LIBWEBP)
        set(2FSIQA_WEBP_TARGET "")
        find_package(WebP QUIET)
        if(TARGET WebP::webpdecoder)
            list(APPEND 2FSIQA_WEBP_TARGET WebP::webpdecoder)
        elseif(TARGET WebP::webp)
            list(APPEND 2FSIQA_WEBP_TARGET WebP::webp)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
            list(APPEND 2FSIQA_WEBP_TARGET PkgConfig::WEBP)
        endif()
        if(TARGET WebP::mux)
            list(APPEND 2FSIQA_WEBP_TARGET WebP::mux)
        elseif(TARGET WebP::webpmux)
            list(APPEND 2FSIQA_WEBP_TARGET WebP::webpmux)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(WEBPMUX REQUIRED IMPORTED_TARGET libwebpmux)
            list(APPEND 2FSIQA_WEBP_TARGET PkgConfig::WEBPMUX)
        endif()
    else()
        set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)
        set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(HAVE_WINCODEC_H FALSE CACHE INTERNAL "" FORCE)
        toofsiqa_fetch_and_subdir(libwebp
            "${2FSIQA_LIBWEBP_GIT_REPOSITORY}"
            "${2FSIQA_LIBWEBP_GIT_TAG}"
            "${2FSIQA_LIBWEBP_SOURCE_DIR}")

        set(2FSIQA_WEBP_TARGET "")
        if(TARGET webpdecoder)
            list(APPEND 2FSIQA_WEBP_TARGET webpdecoder)
        elseif(TARGET webp)
            list(APPEND 2FSIQA_WEBP_TARGET webp)
        else()
            message(FATAL_ERROR "libwebp did not produce a decoder target")
        endif()

        if(TARGET webpmux)
            list(APPEND 2FSIQA_WEBP_TARGET webpmux)
        elseif(TARGET libwebpmux)
            list(APPEND 2FSIQA_WEBP_TARGET libwebpmux)
        else()
            message(FATAL_ERROR
                "libwebp did not produce a webpmux target "
                "(enable WEBP_BUILD_LIBWEBPMUX; required for WebP ICC)")
        endif()
    endif()
endmacro()

macro(toofsiqa_resolve_libavif)
    if(2FSIQA_USE_SYSTEM_LIBAVIF)
        find_package(libavif QUIET)
        if(TARGET avif)
            set(2FSIQA_AVIF_TARGET avif)
        elseif(TARGET libavif::avif)
            set(2FSIQA_AVIF_TARGET libavif::avif)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(AVIF REQUIRED IMPORTED_TARGET libavif)
            set(2FSIQA_AVIF_TARGET PkgConfig::AVIF)
        endif()
    else()
        set(AVIF_CODEC_DAV1D "LOCAL" CACHE STRING "" FORCE)
        set(AVIF_CODEC_AOM "OFF" CACHE STRING "" FORCE)
        set(AVIF_CODEC_LIBGAV1 "OFF" CACHE STRING "" FORCE)
        set(AVIF_CODEC_RAV1E "OFF" CACHE STRING "" FORCE)
        set(AVIF_CODEC_SVT "OFF" CACHE STRING "" FORCE)
        set(AVIF_CODEC_AVM "OFF" CACHE STRING "" FORCE)
        set(AVIF_BUILD_APPS OFF CACHE BOOL "" FORCE)
        set(AVIF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(AVIF_ENABLE_GTEST OFF CACHE BOOL "" FORCE)
        set(AVIF_LIBYUV "OFF" CACHE STRING "" FORCE)
        set(AVIF_LIBSHARPYUV "OFF" CACHE STRING "" FORCE)
        set(AVIF_ZLIBPNG "OFF" CACHE STRING "" FORCE)
        set(AVIF_JPEG "OFF" CACHE STRING "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        toofsiqa_fetch_and_subdir(libavif
            "${2FSIQA_LIBAVIF_GIT_REPOSITORY}"
            "${2FSIQA_LIBAVIF_GIT_TAG}"
            "${2FSIQA_LIBAVIF_SOURCE_DIR}")
        if(TARGET avif)
            set(2FSIQA_AVIF_TARGET avif)
        else()
            message(FATAL_ERROR "libavif did not produce an avif target")
        endif()
    endif()
endmacro()

macro(toofsiqa_resolve_lcms2)
    set(2FSIQA_LCMS2_PLUGIN_TARGETS "")
    set(2FSIQA_LCMS2_PLUGIN_INCLUDE_DIRS "")
    set(TOOFSIQA_HAS_LCMS2_THREADED FALSE)
    set(TOOFSIQA_HAS_LCMS2_FAST_FLOAT FALSE)
    if(2FSIQA_USE_SYSTEM_LCMS2)
        find_package(LCMS2 QUIET)
        if(TARGET LCMS2::LCMS2)
            set(2FSIQA_LCMS2_TARGET LCMS2::LCMS2)
        elseif(TARGET lcms2)
            set(2FSIQA_LCMS2_TARGET lcms2)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
            set(2FSIQA_LCMS2_TARGET PkgConfig::LCMS2)
        endif()

        find_path(2FSIQA_LCMS2_FAST_FLOAT_INCLUDE_DIR
            NAMES lcms2_fast_float.h
            PATH_SUFFIXES lcms2
        )
        find_library(2FSIQA_LCMS2_FAST_FLOAT_LIBRARY
            NAMES lcms2_fast_float liblcms2_fast_float
        )
        if(2FSIQA_LCMS2_FAST_FLOAT_INCLUDE_DIR AND 2FSIQA_LCMS2_FAST_FLOAT_LIBRARY)
            add_library(toofsiqa_system_lcms2_fast_float INTERFACE)
            target_include_directories(toofsiqa_system_lcms2_fast_float INTERFACE
                "${2FSIQA_LCMS2_FAST_FLOAT_INCLUDE_DIR}")
            target_link_libraries(toofsiqa_system_lcms2_fast_float INTERFACE
                "${2FSIQA_LCMS2_FAST_FLOAT_LIBRARY}")
            list(APPEND 2FSIQA_LCMS2_PLUGIN_TARGETS toofsiqa_system_lcms2_fast_float)
            set(TOOFSIQA_HAS_LCMS2_FAST_FLOAT TRUE)
            message(STATUS "Found lcms2 fast_float plugin: ${2FSIQA_LCMS2_FAST_FLOAT_LIBRARY}")
        else()
            message(STATUS "lcms2 fast_float plugin not found; continuing without it")
        endif()

        find_path(2FSIQA_LCMS2_THREADED_INCLUDE_DIR
            NAMES lcms2_threaded.h
            PATH_SUFFIXES lcms2
        )
        find_library(2FSIQA_LCMS2_THREADED_LIBRARY
            NAMES lcms2_threaded liblcms2_threaded
        )
        if(2FSIQA_LCMS2_THREADED_INCLUDE_DIR AND 2FSIQA_LCMS2_THREADED_LIBRARY)
            add_library(toofsiqa_system_lcms2_threaded INTERFACE)
            target_include_directories(toofsiqa_system_lcms2_threaded INTERFACE
                "${2FSIQA_LCMS2_THREADED_INCLUDE_DIR}")
            target_link_libraries(toofsiqa_system_lcms2_threaded INTERFACE
                "${2FSIQA_LCMS2_THREADED_LIBRARY}")
            list(APPEND 2FSIQA_LCMS2_PLUGIN_TARGETS toofsiqa_system_lcms2_threaded)
            set(TOOFSIQA_HAS_LCMS2_THREADED TRUE)
            message(STATUS "Found lcms2 threaded plugin: ${2FSIQA_LCMS2_THREADED_LIBRARY}")
        else()
            message(STATUS "lcms2 threaded plugin not found; using app-level CMM slice fallback")
        endif()
    else()
        set(LCMS2_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(LCMS2_BUILD_STATIC ON CACHE BOOL "" FORCE)
        set(LCMS2_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(LCMS2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LCMS2_BUILD_JPGICC OFF CACHE BOOL "" FORCE)
        set(LCMS2_BUILD_TIFICC OFF CACHE BOOL "" FORCE)
        set(LCMS2_BUILD_TIFDIFF OFF CACHE BOOL "" FORCE)
        set(LCMS2_WITH_JPEG OFF CACHE BOOL "" FORCE)
        set(LCMS2_WITH_TIFF OFF CACHE BOOL "" FORCE)
        set(LCMS2_WITH_ZLIB OFF CACHE BOOL "" FORCE)
        set(LCMS2_WITH_FASTFLOAT ON CACHE BOOL "" FORCE)
        set(LCMS2_WITH_THREADED_PLUGIN ON CACHE BOOL "" FORCE)
        set(LCMS2_WITH_THREADS ON CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        toofsiqa_fetch_and_subdir(lcms2
            "${2FSIQA_LCMS2_GIT_REPOSITORY}"
            "${2FSIQA_LCMS2_GIT_TAG}"
            "${2FSIQA_LCMS2_SOURCE_DIR}")
        if(NOT EXISTS "${lcms2_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "Little-CMS source has no CMakeLists.txt: ${lcms2_SOURCE_DIR}")
        endif()
        if(TARGET lcms2_static)
            set(2FSIQA_LCMS2_TARGET lcms2_static)
        elseif(TARGET lcms2)
            set(2FSIQA_LCMS2_TARGET lcms2)
        else()
            message(FATAL_ERROR "Little-CMS did not produce an lcms2 target")
        endif()
        if(TARGET lcms2_fast_float)
            list(APPEND 2FSIQA_LCMS2_PLUGIN_TARGETS lcms2_fast_float)
            list(APPEND 2FSIQA_LCMS2_PLUGIN_INCLUDE_DIRS
                "${lcms2_SOURCE_DIR}/plugins/fast_float/include")
            set(TOOFSIQA_HAS_LCMS2_FAST_FLOAT TRUE)
        else()
            message(FATAL_ERROR "Little-CMS did not produce lcms2_fast_float (LCMS2_WITH_FASTFLOAT=ON)")
        endif()
        if(TARGET lcms2_threaded)
            list(APPEND 2FSIQA_LCMS2_PLUGIN_TARGETS lcms2_threaded)
            list(APPEND 2FSIQA_LCMS2_PLUGIN_INCLUDE_DIRS
                "${lcms2_SOURCE_DIR}/plugins/threaded/include")
            set(TOOFSIQA_HAS_LCMS2_THREADED TRUE)
        else()
            message(FATAL_ERROR "Little-CMS did not produce lcms2_threaded (LCMS2_WITH_THREADED_PLUGIN=ON)")
        endif()
    endif()
endmacro()

macro(toofsiqa_resolve_highway)
    if(2FSIQA_USE_SYSTEM_HIGHWAY)
        find_package(HWY QUIET)
        if(TARGET hwy::hwy)
            set(2FSIQA_HIGHWAY_TARGET hwy::hwy)
        elseif(TARGET hwy)
            set(2FSIQA_HIGHWAY_TARGET hwy)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(HWY REQUIRED IMPORTED_TARGET libhwy)
            set(2FSIQA_HIGHWAY_TARGET PkgConfig::HWY)
        endif()
    else()
        set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
        set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
        set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        toofsiqa_fetch_and_subdir(highway
            "${2FSIQA_HIGHWAY_GIT_REPOSITORY}"
            "${2FSIQA_HIGHWAY_GIT_TAG}"
            "${2FSIQA_HIGHWAY_SOURCE_DIR}")
        if(TARGET hwy)
            set(2FSIQA_HIGHWAY_TARGET hwy)
        else()
            message(FATAL_ERROR "highway did not produce hwy target")
        endif()
    endif()
endmacro()

macro(toofsiqa_resolve_owned_libcxx)
    if(2FSIQA_LIBCXX STREQUAL "static_owned")
        toofsiqa_fetch_dep(llvm_project
            "${2FSIQA_LIBCXX_GIT_REPOSITORY}"
            "${2FSIQA_LIBCXX_GIT_TAG}"
            "${2FSIQA_LIBCXX_SOURCE_DIR}")
        set(LLVM_ENABLE_RUNTIMES "libunwind;libcxxabi;libcxx" CACHE STRING "" FORCE)
        set(LIBUNWIND_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
        set(LIBUNWIND_ENABLE_STATIC ON CACHE BOOL "" FORCE)
        set(LIBCXXABI_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
        set(LIBCXXABI_ENABLE_STATIC ON CACHE BOOL "" FORCE)
        set(LIBCXXABI_USE_LLVM_UNWINDER ON CACHE BOOL "" FORCE)
        set(LIBCXXABI_ENABLE_STATIC_UNWINDER ON CACHE BOOL "" FORCE)
        set(LIBCXX_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
        set(LIBCXX_ENABLE_STATIC ON CACHE BOOL "" FORCE)
        set(LIBCXX_ENABLE_STATIC_ABI_LIBRARY ON CACHE BOOL "" FORCE)
        set(LIBCXX_CXX_ABI libcxxabi CACHE STRING "" FORCE)
        set(LIBCXX_ENABLE_EXCEPTIONS ON CACHE BOOL "" FORCE)
        set(LIBCXX_ENABLE_RTTI ON CACHE BOOL "" FORCE)
        set(LIBCXX_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBCXX_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)
        set(LIBCXXABI_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBUNWIND_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBCXXABI_ENABLE_THREADS ON CACHE BOOL "" FORCE)
        set(LIBUNWIND_ENABLE_THREADS ON CACHE BOOL "" FORCE)
        if(POLICY CMP0219)
            set(CMAKE_POLICY_DEFAULT_CMP0219 NEW)
        endif()
        list(INSERT CMAKE_MODULE_PATH 0
            "${llvm_project_SOURCE_DIR}/cmake"
            "${llvm_project_SOURCE_DIR}/cmake/Modules"
            "${llvm_project_SOURCE_DIR}/llvm/cmake"
            "${llvm_project_SOURCE_DIR}/llvm/cmake/modules"
            "${llvm_project_SOURCE_DIR}/runtimes/cmake"
            "${llvm_project_SOURCE_DIR}/runtimes/cmake/Modules"
        )
        set(LLVM_PATH "${llvm_project_SOURCE_DIR}" CACHE PATH "" FORCE)
        set(LLVM_MAIN_SRC_DIR "${llvm_project_SOURCE_DIR}/llvm" CACHE PATH "" FORCE)
        add_subdirectory(${llvm_project_SOURCE_DIR}/runtimes ${CMAKE_BINARY_DIR}/_deps/libcxx-build EXCLUDE_FROM_ALL)

        set(_owned_cxx_inc "${CMAKE_BINARY_DIR}/_deps/libcxx-build/include/c++/v1")
        if(NOT EXISTS "${_owned_cxx_inc}")
            set(_owned_cxx_inc "${llvm_project_SOURCE_DIR}/libcxx/include")
        endif()

        add_library(toofsiqa_owned_cxx INTERFACE)
        target_compile_options(toofsiqa_owned_cxx INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>
        )
        target_include_directories(toofsiqa_owned_cxx INTERFACE
            "${_owned_cxx_inc}"
        )
        target_link_options(toofsiqa_owned_cxx INTERFACE
            $<$<LINK_LANGUAGE:CXX>:-nostdlib++>
        )
        if(NOT TARGET cxx_static)
            message(FATAL_ERROR "owned libc++: cxx_static target missing after runtimes build")
        endif()
        target_link_libraries(toofsiqa_owned_cxx INTERFACE cxx_static)
        if(TARGET unwind_static)
            target_link_libraries(toofsiqa_owned_cxx INTERFACE unwind_static)
        endif()
        if(WIN32 AND NOT MSVC)
            target_link_libraries(toofsiqa_owned_cxx INTERFACE m)
        endif()
    endif()
endmacro()

function(toofsiqa_apply_strict_fp_static_deps)
    set(_candidates
        zlib-ng zlibstatic zlib png_static png
        tiff tiff_static
        webpdecoder webp webpmux libwebpmux
        avif
        lcms2 lcms2_static lcms2_threaded lcms2_fast_float
        hwy hwy_list_targets
    )
    foreach(_t IN LISTS _candidates)
        if(TARGET "${_t}")
            toofsiqa_apply_strict_fp("${_t}")
        endif()
    endforeach()
endfunction()

macro(toofsiqa_resolve_all_dependencies)
    toofsiqa_resolve_zlib_ng()
    toofsiqa_resolve_libpng()
    toofsiqa_resolve_libjpeg()
    toofsiqa_resolve_libtiff()
    toofsiqa_resolve_libwebp()
    toofsiqa_resolve_libavif()
    toofsiqa_resolve_lcms2()
    toofsiqa_resolve_highway()
    toofsiqa_resolve_owned_libcxx()
    toofsiqa_apply_strict_fp_static_deps()
    toofsiqa_apply_arch_flags_codec_deps()
endmacro()
