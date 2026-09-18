/**
 * Still-image motion extractor for 2FSIQA.
 * Emits zero motion scores. Option schema limited to keys the model JSON
 * sets non-default so feature-name mangling stays correct.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "dict.h"
#include "feature_collector.h"
#include "feature_extractor.h"
#include "feature_name.h"

#define DEFAULT_MOTION_MAX_VAL (10000.0)

typedef struct MotionState {
    VmafDictionary *feature_name_dict;
    double motion_max_val;
    bool motion_force_zero;
} MotionState;

static const VmafOption options[] = {
    {
        .name = "motion_force_zero",
        .alias = "force_0",
        .offset = offsetof(MotionState, motion_force_zero),
        .type = VMAF_OPT_TYPE_BOOL,
        .default_val.b = false,
        .flags = VMAF_OPT_FLAG_FEATURE_PARAM,
    },
    {
        .name = "motion_max_val",
        .alias = "mmxv",
        .offset = offsetof(MotionState, motion_max_val),
        .type = VMAF_OPT_TYPE_DOUBLE,
        .default_val.d = DEFAULT_MOTION_MAX_VAL,
        .min = 0.0,
        .max = 10000.0,
        .flags = VMAF_OPT_FLAG_FEATURE_PARAM,
    },
    { 0 },
};

static int init(VmafFeatureExtractor *fex, enum VmafPixelFormat pix_fmt,
                unsigned bpc, unsigned w, unsigned h)
{
    (void)pix_fmt;
    (void)bpc;
    (void)w;
    (void)h;

    MotionState *s = fex->priv;
    s->feature_name_dict =
        vmaf_feature_name_dict_from_provided_features(fex->provided_features,
                                                      fex->options, s);
    if (!s->feature_name_dict)
        return -ENOMEM;
    return 0;
}

static int extract(VmafFeatureExtractor *fex,
                   VmafPicture *ref_pic, VmafPicture *ref_pic_90,
                   VmafPicture *dist_pic, VmafPicture *dist_pic_90,
                   unsigned index, VmafFeatureCollector *feature_collector)
{
    (void)ref_pic;
    (void)ref_pic_90;
    (void)dist_pic;
    (void)dist_pic_90;

    MotionState *s = fex->priv;
    static const char *names[] = {
        "VMAF_integer_feature_motion_sad_score",
        "VMAF_integer_feature_motion2_score",
        "VMAF_integer_feature_motion3_score",
        NULL
    };
    for (unsigned i = 0; names[i]; i++) {
        int err = vmaf_feature_collector_append_with_dict(
            feature_collector, s->feature_name_dict, names[i], 0.0, index);
        if (err)
            return err;
    }
    return 0;
}

static int flush(VmafFeatureExtractor *fex,
                 VmafFeatureCollector *feature_collector)
{
    (void)fex;
    (void)feature_collector;
    return 1;
}

static int close_fex(VmafFeatureExtractor *fex)
{
    MotionState *s = fex->priv;
    return vmaf_dictionary_free(&s->feature_name_dict);
}

static const char *provided_features[] = {
    "VMAF_integer_feature_motion_sad_score",
    "VMAF_integer_feature_motion2_score",
    "VMAF_integer_feature_motion3_score",
    NULL
};

VmafFeatureExtractor vmaf_fex_integer_motion = {
    .name = "motion",
    .options = options,
    .init = init,
    .extract = extract,
    .flush = flush,
    .close = close_fex,
    .priv_size = sizeof(MotionState),
    .provided_features = provided_features,
    .flags = 0,
};
