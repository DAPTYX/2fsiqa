/**
 * Still-image VMAF context for 2FSIQA.
 * Single-pair path: init → use_features_from_model → read_pictures → score → close.
 * No CUDA, no picture pool, no prev-frame state, no model collections, no output writers.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libvmaf/libvmaf.h"
#include "libvmaf/feature.h"
#include "libvmaf/picture.h"

#include "cpu.h"
#include "feature/feature_extractor.h"
#include "feature/feature_collector.h"
#include "fex_ctx_vector.h"
#include "log.h"
#include "model.h"
#include "picture.h"
#include "predict.h"
#include "thread_pool.h"
#include "vcs_version.h"

typedef struct VmafContext {
    VmafConfiguration cfg;
    VmafFeatureCollector *feature_collector;
    RegisteredFeatureExtractors registered_feature_extractors;
    VmafFeatureExtractorContextPool *fex_ctx_pool;
    VmafThreadPool *thread_pool;
    struct {
        unsigned w, h;
        enum VmafPixelFormat pix_fmt;
        unsigned bpc;
        enum VmafPictureBufferType buf_type;
    } pic_params;
    unsigned pic_cnt;
    bool flushed;
} VmafContext;

typedef struct BatchThreadData {
    VmafFeatureExtractorContext **fex_ctx;
    unsigned cnt;
} BatchThreadData;

static void batch_thread_data_free(void *data)
{
    BatchThreadData *td = data;
    if (!td) return;
    for (unsigned i = 0; i < td->cnt; i++) {
        if (td->fex_ctx[i]) {
            vmaf_feature_extractor_context_close(td->fex_ctx[i]);
            vmaf_feature_extractor_context_destroy(td->fex_ctx[i]);
        }
    }
    free(td->fex_ctx);
    free(td);
}

int vmaf_init(VmafContext **vmaf, VmafConfiguration cfg)
{
    if (!vmaf) return -EINVAL;

    VmafContext *const v = *vmaf = malloc(sizeof(*v));
    if (!v) return -ENOMEM;
    memset(v, 0, sizeof(*v));
    v->cfg = cfg;

    vmaf_init_cpu();
    vmaf_set_cpu_flags_mask(~cfg.cpumask);
    vmaf_set_log_level(cfg.log_level);

    int err = vmaf_feature_collector_init(&(v->feature_collector));
    if (err) goto free_v;

    err = feature_extractor_vector_init(&(v->registered_feature_extractors));
    if (err) goto free_feature_collector;

    if (v->cfg.n_threads > 0) {
        VmafThreadPoolConfig tpool_cfg = {
            .n_threads = v->cfg.n_threads,
            .thread_data_free = batch_thread_data_free,
        };
        err = vmaf_thread_pool_create(&v->thread_pool, tpool_cfg);
        if (err) goto free_feature_extractor_vector;
        err = vmaf_fex_ctx_pool_create(&v->fex_ctx_pool, v->cfg.n_threads);
        if (err) goto free_thread_pool;
    }

    return 0;

free_thread_pool:
    vmaf_thread_pool_destroy(v->thread_pool);
free_feature_extractor_vector:
    feature_extractor_vector_destroy(&(v->registered_feature_extractors));
free_feature_collector:
    vmaf_feature_collector_destroy(v->feature_collector);
free_v:
    free(v);
    *vmaf = NULL;
    return -ENOMEM;
}

int vmaf_close(VmafContext *vmaf)
{
    if (!vmaf) return -EINVAL;

    vmaf_thread_pool_wait(vmaf->thread_pool);
    feature_extractor_vector_destroy(&(vmaf->registered_feature_extractors));
    vmaf_feature_collector_destroy(vmaf->feature_collector);
    vmaf_thread_pool_destroy(vmaf->thread_pool);
    vmaf_fex_ctx_pool_destroy(vmaf->fex_ctx_pool);
    free(vmaf);
    return 0;
}

int vmaf_use_features_from_model(VmafContext *vmaf, VmafModel *model)
{
    if (!vmaf) return -EINVAL;
    if (!model) return -EINVAL;

    RegisteredFeatureExtractors *rfe = &(vmaf->registered_feature_extractors);

    for (unsigned i = 0; i < model->n_features; i++) {
        VmafFeatureExtractor *fex =
            vmaf_get_feature_extractor_by_feature_name(model->feature[i].name, 0);
        if (!fex) {
            vmaf_log(VMAF_LOG_LEVEL_ERROR,
                     "could not initialize feature extractor \"%s\"\n",
                     model->feature[i].name);
            return -EINVAL;
        }

        VmafDictionary *d = NULL;
        if (model->feature[i].opts_dict) {
            int err = vmaf_dictionary_copy(&model->feature[i].opts_dict, &d);
            if (err) return err;
        }

        VmafFeatureExtractorContext *fex_ctx;
        int err = vmaf_feature_extractor_context_create(&fex_ctx, fex, d);
        if (err) return err;

        err = feature_extractor_vector_append(rfe, fex_ctx, 0);
        if (err) {
            vmaf_feature_extractor_context_destroy(fex_ctx);
            return err;
        }
    }

    return vmaf_feature_collector_mount_model(vmaf->feature_collector, model);
}

struct ThreadDataBatch {
    VmafPicture ref, dist;
    unsigned index;
    VmafFeatureCollector *feature_collector;
    RegisteredFeatureExtractors *registered_fex;
    int err;
};

static void threaded_extract_batch_func(void *e, void **thread_data)
{
    struct ThreadDataBatch *f = e;
    f->err = 0;

    BatchThreadData *td = *thread_data;
    if (!td) {
        td = malloc(sizeof(*td));
        if (!td) { f->err = -ENOMEM; goto unref; }
        td->cnt = f->registered_fex->cnt;
        td->fex_ctx = calloc(td->cnt, sizeof(*td->fex_ctx));
        if (!td->fex_ctx) { free(td); f->err = -ENOMEM; goto unref; }
        *thread_data = td;
    }

    for (unsigned i = 0; i < f->registered_fex->cnt; i++) {
        VmafFeatureExtractor *fex = f->registered_fex->fex_ctx[i]->fex;

        if (!td->fex_ctx[i]) {
            VmafDictionary *opts_dict = f->registered_fex->fex_ctx[i]->opts_dict;
            VmafDictionary *d = NULL;
            if (opts_dict) {
                int err = vmaf_dictionary_copy(&opts_dict, &d);
                if (err) { f->err = err; break; }
            }
            int err = vmaf_feature_extractor_context_create(&td->fex_ctx[i], fex, d);
            if (err) { f->err = err; break; }
        }

        int err = vmaf_feature_extractor_context_extract(
            td->fex_ctx[i], &f->ref, NULL, &f->dist, NULL,
            f->index, f->feature_collector);
        if (err) {
            f->err = err;
            break;
        }
    }

unref:
    vmaf_picture_unref(&f->ref);
    vmaf_picture_unref(&f->dist);
}

static int threaded_read_pictures_batch(VmafContext *vmaf, VmafPicture *ref,
                                        VmafPicture *dist, unsigned index)
{
    VmafPicture pic_a, pic_b;
    vmaf_picture_ref(&pic_a, ref);
    vmaf_picture_ref(&pic_b, dist);

    struct ThreadDataBatch data = {
        .ref = pic_a,
        .dist = pic_b,
        .index = index,
        .feature_collector = vmaf->feature_collector,
        .registered_fex = &vmaf->registered_feature_extractors,
        .err = 0,
    };

    int err = vmaf_thread_pool_enqueue(vmaf->thread_pool,
                                       threaded_extract_batch_func,
                                       &data, sizeof(data));
    if (err) {
        vmaf_picture_unref(&pic_a);
        vmaf_picture_unref(&pic_b);
        return err;
    }

    return vmaf_picture_unref(ref) | vmaf_picture_unref(dist);
}

static int validate_pic_params(VmafContext *vmaf, VmafPicture *ref,
                               VmafPicture *dist)
{
    if (!vmaf->pic_params.w) {
        vmaf->pic_params.w = ref->w[0];
        vmaf->pic_params.h = ref->h[0];
        vmaf->pic_params.pix_fmt = ref->pix_fmt;
        vmaf->pic_params.bpc = ref->bpc;
    }

    if ((ref->w[0] != dist->w[0]) || (ref->h[0] != dist->h[0]))
        return -EINVAL;
    if ((ref->pix_fmt != dist->pix_fmt) || (ref->bpc != dist->bpc))
        return -EINVAL;
    if ((ref->w[0] != vmaf->pic_params.w) || (ref->h[0] != vmaf->pic_params.h))
        return -EINVAL;
    if ((ref->pix_fmt != vmaf->pic_params.pix_fmt) ||
        (ref->bpc != vmaf->pic_params.bpc))
        return -EINVAL;

    return 0;
}

static int flush_context(VmafContext *vmaf)
{
    int err = 0;

    if (vmaf->thread_pool) {
        err |= vmaf_thread_pool_wait(vmaf->thread_pool);
        RegisteredFeatureExtractors rfe = vmaf->registered_feature_extractors;
        for (unsigned i = 0; i < rfe.cnt; i++) {
            VmafFeatureExtractor *fex = rfe.fex_ctx[i]->fex;
            if (!fex->flush)
                continue;
            int flush_err = 0;
            while (!(flush_err = fex->flush(fex, vmaf->feature_collector)))
                ;
            if (flush_err < 0)
                err |= flush_err;
        }
    } else {
        RegisteredFeatureExtractors rfe = vmaf->registered_feature_extractors;
        for (unsigned i = 0; i < rfe.cnt; i++) {
            err |= vmaf_feature_extractor_context_flush(
                rfe.fex_ctx[i], vmaf->feature_collector);
        }
    }

    if (!err)
        vmaf->flushed = true;
    return err;
}

int vmaf_read_pictures(VmafContext *vmaf, VmafPicture *ref, VmafPicture *dist,
                       unsigned index)
{
    if (!vmaf) return -EINVAL;
    if (vmaf->flushed) return -EINVAL;
    if (!ref != !dist) return -EINVAL;
    if (!ref && !dist) return flush_context(vmaf);

    vmaf->pic_cnt++;
    int err = validate_pic_params(vmaf, ref, dist);
    if (err) return err;

    if (vmaf->thread_pool)
        return threaded_read_pictures_batch(vmaf, ref, dist, index);

    for (unsigned i = 0; i < vmaf->registered_feature_extractors.cnt; i++) {
        VmafFeatureExtractorContext *fex_ctx =
            vmaf->registered_feature_extractors.fex_ctx[i];
        err = vmaf_feature_extractor_context_extract(
            fex_ctx, ref, NULL, dist, NULL, index, vmaf->feature_collector);
        if (err) return err;
    }

    err |= vmaf_picture_unref(ref);
    err |= vmaf_picture_unref(dist);
    return err;
}

int vmaf_score_at_index(VmafContext *vmaf, VmafModel *model, double *score,
                        unsigned index)
{
    if (!vmaf) return -EINVAL;
    if (!model) return -EINVAL;
    if (!score) return -EINVAL;

    int err = vmaf_feature_collector_get_score(
        vmaf->feature_collector, model->name, score, index);
    if (err) {
        err = vmaf_predict_score_at_index(
            model, vmaf->feature_collector, index, score, true, false, 0);
    }
    return err;
}

const char *vmaf_version(void)
{
    return VMAF_VERSION;
}
