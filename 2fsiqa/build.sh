#!/bin/bash
set -euo pipefail

CXX="${CXX:-clang++}"
CC="${CC:-clang}"

FPFLAGS="-ffp-model=precise -fno-fast-math -fno-associative-math"

CFLAGS="-O3 -std=c11 -D_GNU_SOURCE -Wall -fPIC ${FPFLAGS}"
CXXFLAGS="-O3 -std=c++20 -Wall -Wextra ${FPFLAGS}"
OUT="${OUT:-2fsiqa}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
VMAF="$ROOT/third_party/libvmaf"
BUILDDIR="$ROOT/build"
OBJDIR="$BUILDDIR/obj"
GENDIR="$BUILDDIR/generated"
mkdir -p "$OBJDIR" "$GENDIR"

echo "2FSIQA dev build"

echo "  HOST bin2c"
$CC -O2 -o "$BUILDDIR/bin2c" "$ROOT/bin2c/bin2c.c"

embed_asset() {
  local src="$1"
  local base
  base=$(basename "$src")
  local stem="${base%.*}"
  stem=$(printf '%s' "$stem" | sed 's/[^A-Za-z0-9]/_/g')
  case "$stem" in
    [0-9]*) stem="_${stem}" ;;
  esac
  echo "  EMBED $base -> ${stem}"
  "$BUILDDIR/bin2c" "$src" "$GENDIR/${stem}.cpp" "$stem"
}

embed_asset "$ROOT/icc/2FSCP-26_AllReal_g2.2_v4.icc"
embed_asset "$ROOT/icc/2FSCP-26_AllReal_g2.4_v4.icc"
embed_asset "$ROOT/icc/2FSCP-26_AllReal_linear_v4.icc"
embed_asset "$ROOT/icc/2FSCP-26_Grayscale_g2.2_v4.icc"
embed_asset "$ROOT/icc/2FSCP-26_Grayscale_linear_v4.icc"
embed_asset "$ROOT/icc/2FSCP-26_sRGB_v4.icc"
embed_asset "$ROOT/icc/2FSCP-26_sRGB-gray_v4.icc"
embed_asset "$ROOT/third_party/libvmaf/vmaf_v1.0.16_1d5h_2160.json"

GENERATED_CPP=(
  "$GENDIR/_2FSCP_26_AllReal_g2_2_v4.cpp"
  "$GENDIR/_2FSCP_26_AllReal_g2_4_v4.cpp"
  "$GENDIR/_2FSCP_26_AllReal_linear_v4.cpp"
  "$GENDIR/_2FSCP_26_Grayscale_g2_2_v4.cpp"
  "$GENDIR/_2FSCP_26_Grayscale_linear_v4.cpp"
  "$GENDIR/_2FSCP_26_sRGB_v4.cpp"
  "$GENDIR/_2FSCP_26_sRGB_gray_v4.cpp"
  "$GENDIR/vmaf_v1_0_16_1d5h_2160.cpp"
)

INC=(
  -I"$ROOT"
  -I"$ROOT/input"
  -I"$ROOT/third_party/libjxl"
  -I"$ROOT/third_party/libjxl/butteraugli"
  -I"$VMAF"
  -I"$VMAF/include"
  -I"$VMAF/src"
  -I"$VMAF/src/feature"
  -I"$VMAF/src/feature/common"
  -I"$ROOT/../libwebp-main/src"
  -I"$ROOT/../libavif-main/include"
)

VMAF_C_SOURCES=(
  "$VMAF/src/libvmaf.c"
  "$VMAF/src/predict.c"
  "$VMAF/src/model.c"
  "$VMAF/src/read_json_model.c"
  "$VMAF/src/pdjson.c"
  "$VMAF/src/picture.c"
  "$VMAF/src/mem.c"
  "$VMAF/src/ref.c"
  "$VMAF/src/dict.c"
  "$VMAF/src/opt.c"
  "$VMAF/src/log.c"
  "$VMAF/src/thread_pool.c"
  "$VMAF/src/fex_ctx_vector.c"
  "$VMAF/src/metadata_handler.c"
  "$VMAF/src/cpu.c"
  "$VMAF/src/vmaf_stubs.c"
  "$VMAF/src/feature/feature_extractor.c"
  "$VMAF/src/feature/feature_collector.c"
  "$VMAF/src/feature/feature_name.c"
  "$VMAF/src/feature/integer_adm.c"
  "$VMAF/src/feature/integer_motion.c"
  "$VMAF/src/feature/cambi.c"
  "$VMAF/src/feature/speed.c"
  "$VMAF/src/feature/alias.c"
  "$VMAF/src/feature/picture_copy.c"
  "$VMAF/src/feature/luminance_tools.c"
  "$VMAF/src/feature/vif_tools.c"
  "$VMAF/src/feature/common/alignment.c"
  "$VMAF/src/feature/common/convolution.c"
)

OBJS=()
for src in "${VMAF_C_SOURCES[@]}"; do
  base=$(basename "$src" .c)
  obj="$OBJDIR/${base}.o"
  echo "  CC  $base"
  $CC $CFLAGS "${INC[@]}" -c "$src" -o "$obj"
  OBJS+=("$obj")
done

echo "  CXX svm"
$CXX $CXXFLAGS "${INC[@]}" -c "$VMAF/src/svm.cpp" -o "$OBJDIR/svm.o"
OBJS+=("$OBJDIR/svm.o")

echo "  Linking."
$CXX $CXXFLAGS "${INC[@]}" \
    core.cpp batch.cpp cmm.cpp psnr.cpp ssim.cpp dssim.cpp toofsiqa.cpp \
    third_party/libjxl/ssimulacra2.cc \
    third_party/libjxl/xyb.cc \
    third_party/libjxl/gauss_blur.cc \
    third_party/libjxl/butteraugli_2fs.cc \
    third_party/libjxl/butteraugli/butteraugli.cc \
    third_party/libjxl/butteraugli/image.cc \
    third_party/libjxl/butteraugli/convolve_slow.cc \
    third_party/libjxl/butteraugli/convolve_separable5.cc \
    third_party/libjxl/butteraugli/memory_manager_internal.cc \
    "$VMAF/yuv_allreal.cpp" \
    "$VMAF/vmaf_runner.cpp" \
    "${GENERATED_CPP[@]}" \
    "${OBJS[@]}" \
    -o "$OUT" \
    $(pkg-config --cflags --libs libpng libtiff-4 lcms2 libhwy) -ljpeg \
    -lwebp -lwebpmux -lavif \
    -pthread -lm

echo "[OK] $OUT built successfully."
echo ""
echo "Usage: ./$OUT [options] <reference> <distorted>"
echo "       ./$OUT [options] --batch <reference_dir> <distorted_dir>"
echo "Default: all metrics combined into 2fsiqa score only"
echo "Options: --psnr --ssim --dssim --lacra --butter --vmaf --showall"
