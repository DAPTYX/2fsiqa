// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include "vmaf_runner.h"
#include "embedded_assets.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/model.h"
#include "libvmaf/picture.h"

extern "C" {
#include "read_json_model.h"
}

#include <string>
#include <thread>

namespace {

int wrap_yuv_picture(VmafPicture* pic, const Yuv444i12& yuv) {
    void* data[3] = {
        yuv.y.get(),
        yuv.cb.get(),
        yuv.cr.get(),
    };
    const ptrdiff_t stride_bytes =
        static_cast<ptrdiff_t>(yuv.stride) * static_cast<ptrdiff_t>(sizeof(uint16_t));
    const ptrdiff_t strides[3] = { stride_bytes, stride_bytes, stride_bytes };
    return vmaf_picture_wrap(
        pic, VMAF_PIX_FMT_YUV444P, yuv.bpc, yuv.width, yuv.height, data, strides);
}

}  // namespace

VmafScore compute_2fs_vmaf(const Yuv444i12& ref, const Yuv444i12& dist) {
    VmafScore result;

    if (ref.width != dist.width || ref.height != dist.height ||
        ref.bpc != dist.bpc || ref.stride != dist.stride) {
        result.error = "yuv geometry mismatch";
        return result;
    }
    if (!ref.y || !ref.cb || !ref.cr || !dist.y || !dist.cb || !dist.cr ||
        ref.stride < ref.width) {
        result.error = "yuv planes invalid";
        return result;
    }

    VmafConfiguration cfg = {};
    cfg.log_level = VMAF_LOG_LEVEL_ERROR;
    {
        unsigned hc = std::thread::hardware_concurrency();
        cfg.n_threads = hc > 0 ? hc : 1;
    }
    cfg.n_subsample = 1;
    cfg.cpumask = 0;
    cfg.gpumask = 0;

    VmafContext* vmaf = nullptr;
    if (vmaf_init(&vmaf, cfg) != 0) {
        result.error = "vmaf_init failed";
        return result;
    }

    VmafModel* model = nullptr;
    VmafModelConfig model_cfg = {};
    model_cfg.name = "2FS-vmaf";
    model_cfg.flags = 0;

    if (vmaf_read_json_model_from_buffer(
            &model, &model_cfg,
            reinterpret_cast<const char*>(embedded::vmaf_v1_0_16_1d5h_2160_data),
            static_cast<int>(embedded::vmaf_v1_0_16_1d5h_2160_size)) != 0) {
        vmaf_close(vmaf);
        result.error = "model load failed: embedded vmaf_v1.0.16_1d5h_2160";
        return result;
    }

    if (vmaf_use_features_from_model(vmaf, model) != 0) {
        vmaf_model_destroy(model);
        vmaf_close(vmaf);
        result.error = "use_features_from_model failed";
        return result;
    }

    VmafPicture pic_ref = {};
    VmafPicture pic_dist = {};
    if (wrap_yuv_picture(&pic_ref, ref) != 0 ||
        wrap_yuv_picture(&pic_dist, dist) != 0) {
        if (pic_ref.ref) vmaf_picture_unref(&pic_ref);
        if (pic_dist.ref) vmaf_picture_unref(&pic_dist);
        vmaf_model_destroy(model);
        vmaf_close(vmaf);
        result.error = "picture wrap failed";
        return result;
    }

    if (vmaf_read_pictures(vmaf, &pic_ref, &pic_dist, 0) != 0) {
        if (pic_ref.ref) vmaf_picture_unref(&pic_ref);
        if (pic_dist.ref) vmaf_picture_unref(&pic_dist);
        vmaf_model_destroy(model);
        vmaf_close(vmaf);
        result.error = "read_pictures failed";
        return result;
    }

    if (vmaf_read_pictures(vmaf, nullptr, nullptr, 0) != 0) {
        vmaf_model_destroy(model);
        vmaf_close(vmaf);
        result.error = "flush failed";
        return result;
    }

    double score = 0.0;
    if (vmaf_score_at_index(vmaf, model, &score, 0) != 0) {
        vmaf_model_destroy(model);
        vmaf_close(vmaf);
        result.error = "score_at_index failed";
        return result;
    }

    result.score = score;
    result.ok = true;

    vmaf_model_destroy(model);
    vmaf_close(vmaf);
    return result;
}
