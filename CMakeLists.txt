cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ENV{CC})
    find_program(_2fsiqa_clang NAMES clang clang-20 clang-19 clang-18 clang-17)
    if(_2fsiqa_clang)
        set(CMAKE_C_COMPILER "${_2fsiqa_clang}" CACHE FILEPATH "C compiler" FORCE)
    endif()
endif()
if(NOT DEFINED ENV{CXX})
    find_program(_2fsiqa_clangxx NAMES clang++ clang++-20 clang++-19 clang++-18 clang++-17)
    if(_2fsiqa_clangxx)
        set(CMAKE_CXX_COMPILER "${_2fsiqa_clangxx}" CACHE FILEPATH "C++ compiler" FORCE)
    endif()
endif()

project(2fsiqa LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerFlags)
include(Dependencies)
include(ThirdPartyNotices)

if(WIN32)
    set(_2fsiqa_libcxx_default "static")
else()
    set(_2fsiqa_libcxx_default "system")
endif()
set(2FSIQA_LIBCXX "${_2fsiqa_libcxx_default}" CACHE STRING
    "C++ standard library: system | static | static_owned")
set_property(CACHE 2FSIQA_LIBCXX PROPERTY STRINGS system static static_owned)
string(TOLOWER "${2FSIQA_LIBCXX}" 2FSIQA_LIBCXX)
if(NOT 2FSIQA_LIBCXX MATCHES "^(system|static|static_owned)$")
    message(FATAL_ERROR "2FSIQA_LIBCXX must be system, static, or static_owned (got '${2FSIQA_LIBCXX}')")
endif()

set(2FSIQA_FETCHCONTENT_BASE "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "")

set(2FSIQA_ZLIB_NG_GIT_REPOSITORY "https://github.com/zlib-ng/zlib-ng.git" CACHE STRING "")
set(2FSIQA_ZLIB_NG_GIT_TAG "develop" CACHE STRING "")
set(2FSIQA_ZLIB_NG_SOURCE_DIR "" CACHE PATH "")
option(2FSIQA_USE_SYSTEM_ZLIB_NG "Use system zlib instead of building zlib-ng" OFF)

set(2FSIQA_LIBPNG_GIT_REPOSITORY "https://github.com/pnggroup/libpng.git" CACHE STRING "")
set(2FSIQA_LIBPNG_GIT_TAG "master" CACHE STRING "")
set(2FSIQA_LIBPNG_SOURCE_DIR "" CACHE PATH "")
option(2FSIQA_USE_SYSTEM_LIBPNG "Use system libpng instead of building" OFF)

set(2FSIQA_LIBJPEG_GIT_REPOSITORY "https://github.com/libjpeg-turbo/libjpeg-turbo.git" CACHE STRING "")
set(2FSIQA_LIBJPEG_GIT_TAG "main" CACHE STRING "")
set(2FSIQA_LIBJPEG_SOURCE_DIR "" CACHE PATH "")
option(2FSIQA_USE_SYSTEM_LIBJPEG "Use system libjpeg instead of building" OFF)

set(2FSIQA_LIBTIFF_GIT_REPOSITORY "https://gitlab.com/libtiff/libtiff.git" CACHE STRING "")
set(2FSIQA_LIBTIFF_GIT_TAG "master" CACHE STRING "")
set(2FSIQA_LIBTIFF_SOURCE_DIR "" CACHE PATH "")
option(2FSIQA_USE_SYSTEM_LIBTIFF "Use system libtiff instead of building" OFF)

set(2FSIQA_LIBWEBP_GIT_REPOSITORY "https://github.com/webmproject/libwebp.git" CACHE STRING "")
set(2FSIQA_LIBWEBP_GIT_TAG "main" CACHE STRING "")
set(2FSIQA_LIBWEBP_SOURCE_DIR "" CACHE PATH "")
option(2FSIQA_USE_SYSTEM_LIBWEBP "Use system libwebp instead of building" OFF)

set(2FSIQA_LIBAVIF_GIT_REPOSITORY "https://github.com/AOMediaCodec/libavif.git" CACHE STRING "")
set(2FSIQA_LIBAVIF_GIT_TAG "main" CACHE STRING "")
set(2FSIQA_LIBAVIF_SOURCE_DIR "" CACHE PATH "")
option(2FSIQA_USE_SYSTEM_LIBAVIF "Use system libavif instead of building" OFF)

set(2FSIQA_LCMS2_GIT_REPOSITORY "https://github.com/mm2/Little-CMS.git" CACHE STRING "")
set(2FSIQA_LCMS2_GIT_TAG "master" CACHE STRING "")
set(2FSIQA_LCMS2_SOURCE_DIR "" CACHE PATH "")
option(2FSIQA_USE_SYSTEM_LCMS2 "Use system lcms2 instead of building" OFF)

set(2FSIQA_HIGHWAY_GIT_REPOSITORY "https://github.com/google/highway.git" CACHE STRING "")
set(2FSIQA_HIGHWAY_GIT_TAG "master" CACHE STRING "")
set(2FSIQA_HIGHWAY_SOURCE_DIR "" CACHE PATH "")
option(2FSIQA_USE_SYSTEM_HIGHWAY "Use system highway instead of building" OFF)

set(2FSIQA_LIBCXX_GIT_REPOSITORY "https://github.com/llvm/llvm-project.git" CACHE STRING "")
set(2FSIQA_LIBCXX_GIT_TAG "main" CACHE STRING "")
set(2FSIQA_LIBCXX_SOURCE_DIR "" CACHE PATH "")

find_package(Threads REQUIRED)

toofsiqa_resolve_all_dependencies()
toofsiqa_write_third_party_notices()

set(2FSIQA_CORE_SOURCES
    2fsiqa/core.cpp
    2fsiqa/batch.cpp
    2fsiqa/cmm.cpp
    2fsiqa/psnr.cpp
    2fsiqa/ssim.cpp
    2fsiqa/dssim.cpp
    2fsiqa/toofsiqa.cpp
)

set(2FSIQA_JXL_SOURCES
    2fsiqa/third_party/libjxl/ssimulacra2.cc
    2fsiqa/third_party/libjxl/xyb.cc
    2fsiqa/third_party/libjxl/gauss_blur.cc
    2fsiqa/third_party/libjxl/butteraugli_2fs.cc
    2fsiqa/third_party/libjxl/butteraugli/butteraugli.cc
    2fsiqa/third_party/libjxl/butteraugli/image.cc
    2fsiqa/third_party/libjxl/butteraugli/convolve_slow.cc
    2fsiqa/third_party/libjxl/butteraugli/convolve_separable5.cc
    2fsiqa/third_party/libjxl/butteraugli/memory_manager_internal.cc
)

set(2FSIQA_VMAF_C_SOURCES
    2fsiqa/third_party/libvmaf/src/libvmaf.c
    2fsiqa/third_party/libvmaf/src/predict.c
    2fsiqa/third_party/libvmaf/src/model.c
    2fsiqa/third_party/libvmaf/src/read_json_model.c
    2fsiqa/third_party/libvmaf/src/pdjson.c
    2fsiqa/third_party/libvmaf/src/picture.c
    2fsiqa/third_party/libvmaf/src/mem.c
    2fsiqa/third_party/libvmaf/src/ref.c
    2fsiqa/third_party/libvmaf/src/dict.c
    2fsiqa/third_party/libvmaf/src/opt.c
    2fsiqa/third_party/libvmaf/src/log.c
    2fsiqa/third_party/libvmaf/src/thread_pool.c
    2fsiqa/third_party/libvmaf/src/fex_ctx_vector.c
    2fsiqa/third_party/libvmaf/src/metadata_handler.c
    2fsiqa/third_party/libvmaf/src/cpu.c
    2fsiqa/third_party/libvmaf/src/vmaf_stubs.c
    2fsiqa/third_party/libvmaf/src/feature/feature_extractor.c
    2fsiqa/third_party/libvmaf/src/feature/feature_collector.c
    2fsiqa/third_party/libvmaf/src/feature/feature_name.c
    2fsiqa/third_party/libvmaf/src/feature/integer_adm.c
    2fsiqa/third_party/libvmaf/src/feature/integer_motion.c
    2fsiqa/third_party/libvmaf/src/feature/cambi.c
    2fsiqa/third_party/libvmaf/src/feature/speed.c
    2fsiqa/third_party/libvmaf/src/feature/alias.c
    2fsiqa/third_party/libvmaf/src/feature/picture_copy.c
    2fsiqa/third_party/libvmaf/src/feature/luminance_tools.c
    2fsiqa/third_party/libvmaf/src/feature/vif_tools.c
    2fsiqa/third_party/libvmaf/src/feature/common/alignment.c
    2fsiqa/third_party/libvmaf/src/feature/common/convolution.c
)

set(2FSIQA_VMAF_CXX_SOURCES
    2fsiqa/third_party/libvmaf/src/svm.cpp
    2fsiqa/third_party/libvmaf/yuv_allreal.cpp
    2fsiqa/third_party/libvmaf/vmaf_runner.cpp
)

add_executable(bin2c 2fsiqa/bin2c/bin2c.c)

set(2FSIQA_ASSET_FILES
    2fsiqa/icc/2FSCP-26_AllReal_g2.2_v4.icc
    2fsiqa/icc/2FSCP-26_AllReal_g2.4_v4.icc
    2fsiqa/icc/2FSCP-26_AllReal_linear_v4.icc
    2fsiqa/icc/2FSCP-26_Grayscale_g2.2_v4.icc
    2fsiqa/icc/2FSCP-26_Grayscale_linear_v4.icc
    2fsiqa/icc/2FSCP-26_sRGB_v4.icc
    2fsiqa/icc/2FSCP-26_sRGB-gray_v4.icc
    2fsiqa/third_party/libvmaf/vmaf_v1.0.16_1d5h_2160.json
)

set(2FSIQA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${2FSIQA_GENERATED_DIR}")

set(2FSIQA_GENERATED_SOURCES "")
foreach(_asset IN LISTS 2FSIQA_ASSET_FILES)
    get_filename_component(_base "${_asset}" NAME)
    string(REGEX REPLACE "\\.[^.]*$" "" _stem "${_base}")
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" _stem "${_stem}")
    if(_stem MATCHES "^[0-9]")
        set(_stem "_${_stem}")
    endif()
    set(_out_cpp "${2FSIQA_GENERATED_DIR}/${_stem}.cpp")
    add_custom_command(
        OUTPUT "${_out_cpp}"
        COMMAND bin2c "${CMAKE_CURRENT_SOURCE_DIR}/${_asset}" "${_out_cpp}" "${_stem}"
        DEPENDS bin2c "${CMAKE_CURRENT_SOURCE_DIR}/${_asset}"
        COMMENT "Embedding ${_base}"
        VERBATIM
    )
    list(APPEND 2FSIQA_GENERATED_SOURCES "${_out_cpp}")
endforeach()

add_executable(toofsiqa
    ${2FSIQA_CORE_SOURCES}
    ${2FSIQA_JXL_SOURCES}
    ${2FSIQA_VMAF_C_SOURCES}
    ${2FSIQA_VMAF_CXX_SOURCES}
    ${2FSIQA_GENERATED_SOURCES}
)
if(WIN32)
    set(2FSIQA_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/toofsiqa.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/toofsiqa.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/toofsiqa.rc"
        @ONLY
    )
    target_sources(toofsiqa PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/toofsiqa.rc")
endif()
set_target_properties(toofsiqa PROPERTIES
    OUTPUT_NAME toofsiqa
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(toofsiqa PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/input
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/third_party/libjxl
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/third_party/libjxl/butteraugli
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/third_party/libvmaf
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/third_party/libvmaf/include
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/third_party/libvmaf/src
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/third_party/libvmaf/src/feature
    ${CMAKE_CURRENT_SOURCE_DIR}/2fsiqa/third_party/libvmaf/src/feature/common
    ${2FSIQA_LCMS2_PLUGIN_INCLUDE_DIRS}
)

if(TOOFSIQA_HAS_LCMS2_FAST_FLOAT)
    target_compile_definitions(toofsiqa PRIVATE TOOFSIQA_HAS_LCMS2_FAST_FLOAT=1)
endif()
if(TOOFSIQA_HAS_LCMS2_THREADED)
    target_compile_definitions(toofsiqa PRIVATE TOOFSIQA_HAS_LCMS2_THREADED=1)
endif()

toofsiqa_apply_strict_fp(toofsiqa)

target_link_libraries(toofsiqa PRIVATE
    ${2FSIQA_ZLIB_TARGET}
    ${2FSIQA_PNG_TARGET}
    ${2FSIQA_JPEG_TARGET}
    ${2FSIQA_TIFF_TARGET}
    ${2FSIQA_WEBP_TARGET}
    ${2FSIQA_AVIF_TARGET}
    ${2FSIQA_LCMS2_TARGET}
    ${2FSIQA_LCMS2_PLUGIN_TARGETS}
    ${2FSIQA_HIGHWAY_TARGET}
    Threads::Threads
)

if(2FSIQA_LIBCXX STREQUAL "static_owned" AND TARGET toofsiqa_owned_cxx)
    target_link_libraries(toofsiqa PRIVATE toofsiqa_owned_cxx)
endif()

if(2FSIQA_LIBCXX STREQUAL "static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>" CACHE STRING "" FORCE)
    if(NOT MSVC AND NOT APPLE)
        target_link_options(toofsiqa PRIVATE -static)
    endif()
    if((NOT WIN32 AND NOT APPLE) OR CYGWIN OR MINGW)
        target_link_options(toofsiqa PRIVATE -static-libgcc -static-libstdc++)
    endif()
    if(MINGW)
        target_link_options(toofsiqa PRIVATE "LINKER:-Bstatic" -lstdc++ -lpthread "LINKER:-Bdynamic")
    endif()
elseif(2FSIQA_LIBCXX STREQUAL "static_owned")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>" CACHE STRING "" FORCE)
    if(WIN32 AND NOT MSVC)
        target_link_options(toofsiqa PRIVATE
            "LINKER:-Bstatic" -lpthread "LINKER:-Bdynamic")
    endif()
endif()

set(_2fsiqa_bin_dir "${CMAKE_BINARY_DIR}/bin")
file(MAKE_DIRECTORY "${_2fsiqa_bin_dir}")
file(COPY
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/AGPL3.md"
    DESTINATION "${_2fsiqa_bin_dir}"
)

install(TARGETS toofsiqa RUNTIME DESTINATION bin)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/AGPL3.md"
    DESTINATION bin
)
if(DEFINED 2FSIQA_THIRD_PARTY_NOTICES_FILE)
    install(FILES "${2FSIQA_THIRD_PARTY_NOTICES_FILE}" DESTINATION bin)
endif()
