/**
 *
 *  Copyright 2016-2020 Netflix, Inc.
 *
 *     Licensed under the BSD+Patent License (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         https://opensource.org/licenses/BSDplusPatent
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dict.h"
#include "feature/alias.h"
#include "feature/feature_collector.h"
#include "feature/feature_extractor.h"
#include "feature/feature_name.h"
#include "log.h"
#include "model.h"
#include "predict.h"
#include "svm.h"

static int normalize(const VmafModel *model, double slope, double intercept,
                     double *feature_score)
{
    switch (model->norm_type) {
    case(VMAF_MODEL_NORMALIZATION_TYPE_NONE):
        break;
    case(VMAF_MODEL_NORMALIZATION_TYPE_LINEAR_RESCALE):
        *feature_score = slope * (*feature_score) + intercept;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int denormalize_feature(const VmafModel *model, double slope,
                                double intercept, double *feature_score)
{
    switch (model->norm_type) {
    case(VMAF_MODEL_NORMALIZATION_TYPE_NONE):
        break;
    case(VMAF_MODEL_NORMALIZATION_TYPE_LINEAR_RESCALE):
        *feature_score = (*feature_score - intercept) / slope;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int denormalize(const VmafModel *model, double *prediction)
{
    switch (model->norm_type) {
    case(VMAF_MODEL_NORMALIZATION_TYPE_NONE):
        break;
    case(VMAF_MODEL_NORMALIZATION_TYPE_LINEAR_RESCALE):
        *prediction = (*prediction - model->intercept) / model->slope;
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int find_linear_function_parameters(VmafPoint p1, VmafPoint p2, double *alpha, double *beta) {

    if (!(p1.x <= p2.x && p1.y <= p2.y))
        return -EINVAL;  // first_point coordinates need to be smaller or equal to second_point coordinates

    if (p2.x - p1.x == 0 || p2.y - p1.y == 0) {
        if (!(p1.x == p2.x && p1.y == p2.y))
            return -EINVAL;  // first_point and second_point cannot lie on a horizontal or vertical line
        *alpha = 1.0;  // both points are the same
        *beta = 0.0;
    }
    else if (p1.x == 0) {
        *beta = p1.y;
        *alpha = (p2.y - *beta) / p2.x;
    }
    else {
        *alpha = (p2.y - p1.y) / (p2.x - p1.x);
        *beta = p1.y - (p1.x * (*alpha));
    }

    return 0;
}

static int piecewise_linear_mapping(double x, VmafPoint *knots, unsigned n_knots, double *y) {

    // initialize them to prevent uninitialized variable warnings
    double slope = 0.0, offset = 0.0;

    if (n_knots <= 1)
        return EINVAL;
    unsigned n_seg = n_knots - 1;

    *y = 0.0;

    // construct the function
    for (unsigned idx = 0; idx < n_seg; idx++) {
        if (!(knots[idx].x <  knots[idx + 1].x &&
              knots[idx].y <= knots[idx + 1].y))
            return EINVAL;

        bool cond0 = knots[idx].x <= x;
        bool cond1 = x <= knots[idx + 1].x;

        if (knots[idx].y == knots[idx + 1].y) {  // the segment is horizontal
            if (cond0 && cond1) {
                *y = knots[idx].y;
            }

            if (idx == 0) {
                // for points below the defined range
                if (x < knots[idx].x)
                    *y = knots[idx].y;
            }

            if (idx == n_seg - 1) {
                // for points above the defined range
                if (x > knots[idx + 1].x)
                    *y = knots[idx].y;
            }
        } else {
            find_linear_function_parameters(knots[idx], knots[idx + 1],
                                            &slope, &offset);

            if (cond0 && cond1){
                *y = slope * x + offset;
            }

            if (idx == 0) {
                // for points below the defined range
                if (x < knots[idx].x)
                    *y = slope * x + offset;
            }

            if (idx == n_seg - 1) {
                // for points above the defined range
                if (x > knots[idx + 1].x)
                    *y = slope * x + offset;
            }
        }
    }

    return 0;
}

/*  Reproducing the logic in quality_runner.VmafQualityRunner.transform_score().
    Transform final quality score in the following optional steps (in this
    order):
    1) polynomial mapping. e.g. {'p0': 1, 'p1': 1, 'p2': 0.5} means
    transform through 1 + x + 0.5 * x^2. For now, only support polynomail
    up to 2nd-order.
    2) piecewise-linear mapping, where the change points are defined in
    'knots', in the form of [[x0, y0], [x1, y1], ...].
    3) rectification, supporting 'out_lte_in' (output is less than or equal
    to input) and 'out_gte_in' (output is greater than or equal to input).
 */
static int transform(const VmafModel *model, double *y_in,
                     enum VmafModelFlags flags)
{
    if (!model->score_transform.enabled)
        return 0;
    if (flags & VMAF_MODEL_FLAG_DISABLE_TRANSFORM)
        return 0;

    double y_stage, y_out;

    // polynomial mapping
    y_stage = *y_in;
    if (model->score_transform.p0.enabled ||
        model->score_transform.p1.enabled ||
        model->score_transform.p2.enabled)
    {
        y_out = 0.;
        if (model->score_transform.p0.enabled)
            y_out += model->score_transform.p0.value;
        if (model->score_transform.p1.enabled)
            y_out += model->score_transform.p1.value * y_stage;
        if (model->score_transform.p2.enabled)
            y_out += model->score_transform.p2.value * y_stage * y_stage;
    }
    else {
        y_out = y_stage;
    }

    // piecewise-linear mapping
    y_stage = y_out;
    if (model->score_transform.knots.enabled) {
        piecewise_linear_mapping(y_stage,
                                 model->score_transform.knots.list,
                                 model->score_transform.knots.n_knots,
                                 &y_out);
    }
    else {
        y_out = y_stage;
    }

    // rectification
    if (model->score_transform.out_lte_in)
        y_out = (y_out > *y_in) ? *y_in : y_out;
    if (model->score_transform.out_gte_in)
        y_out = (y_out < *y_in) ? *y_in : y_out;

    *y_in = y_out;
    return 0;
}

static int clip(const VmafModel *model, double *prediction,
                enum VmafModelFlags flags)
{
    if (!model->score_clip.enabled)
        return 0;
    if (flags & VMAF_MODEL_FLAG_DISABLE_CLIP)
        return 0;

    *prediction = (*prediction < model->score_clip.min) ?
        model->score_clip.min : *prediction;
    *prediction = (*prediction > model->score_clip.max) ?
        model->score_clip.max : *prediction;

    return 0;
}

static int post_process_feature_from_another(VmafModel *model,
                                              struct svm_node *node,
                                              double correction_parameter,
                                              double value_to_be_corrected,
                                              char *guiding_feature_substr,
                                              char *guided_feature_substr)
{
    int err = 0;

    double guiding_score;
    double guided_score;
    bool found_guiding_score = false;
    bool found_guided_score = false;
    unsigned guided_idx;

    for (unsigned i = 0; i < model->n_features; i++) {
        if (strstr(model->feature[i].name, guiding_feature_substr) != NULL) {
            if (found_guiding_score) {
                vmaf_log(VMAF_LOG_LEVEL_ERROR,
                         "post_process_feature_from_another(): Substring '%s' "
                         "corresponds to more than one feature\n",
                         guiding_feature_substr);
                return -EINVAL;
            }
            guiding_score = node[i].value;
            found_guiding_score = true;
            err = denormalize_feature(model, model->feature[i].slope,
                                      model->feature[i].intercept, &guiding_score);
            if (err) return -EINVAL;
        }
        if (strstr(model->feature[i].name, guided_feature_substr) != NULL) {
            if (found_guided_score) {
                vmaf_log(VMAF_LOG_LEVEL_ERROR,
                         "post_process_feature_from_another(): Substring '%s' "
                         "corresponds to more than one feature\n",
                         guided_feature_substr);
                return -EINVAL;
            }
            guided_score = node[i].value;
            found_guided_score = true;
            err = denormalize_feature(model, model->feature[i].slope,
                                      model->feature[i].intercept, &guided_score);
            if (err) return -EINVAL;
            if (guided_score == value_to_be_corrected)
                guided_idx = i;
            else
                return 0;
        }
    }

    double corrected_guided_score =
        (-correction_parameter * guiding_score) + correction_parameter;
    err = normalize(model, model->feature[guided_idx].slope,
                    model->feature[guided_idx].intercept,
                    &corrected_guided_score);
    if (err) return -EINVAL;
    node[guided_idx].value = corrected_guided_score;
    return 0;
}

int vmaf_predict_score_at_index(VmafModel *model,
                                VmafFeatureCollector *feature_collector,
                                unsigned index, double *vmaf_score,
                                bool write_prediction,
                                bool propagate_metadata,
                                enum VmafModelFlags flags)
{
    if (!model) return -EINVAL;
    if (!feature_collector) return -EINVAL;
    if (!vmaf_score) return -EINVAL;

    int err = 0;

    struct svm_node *node = malloc(sizeof(*node) * (model->n_features + 1));
    if (!node) return -ENOMEM;

    for (unsigned i = 0; i < model->n_features; i++) {
        VmafFeatureExtractor *fex =
            vmaf_get_feature_extractor_by_feature_name(model->feature[i].name, 0);

        if (!fex) {
            vmaf_log(VMAF_LOG_LEVEL_ERROR,
                     "vmaf_predict_score_at_index(): no feature extractor "
                     "providing feature '%s'\n", model->feature[i].name);
            err = -EINVAL;
            goto free_node;
        }

        VmafDictionary *opts_dict = NULL;
        if (model->feature[i].opts_dict) {
            err = vmaf_dictionary_copy(&model->feature[i].opts_dict, &opts_dict);
            if (err) return err;
        }

        VmafFeatureExtractorContext *fex_ctx;
        err = vmaf_feature_extractor_context_create(&fex_ctx, fex, opts_dict);
        if (err) {
            vmaf_log(VMAF_LOG_LEVEL_ERROR,
                     "vmaf_predict_score_at_index(): could not generate "
                     "feature extractor context\n");
            vmaf_dictionary_free(&opts_dict);
            return err;
        }

        char *feature_name =
            vmaf_feature_name_from_options(model->feature[i].name,
                    fex_ctx->fex->options, fex_ctx->fex->priv);

        vmaf_feature_extractor_context_destroy(fex_ctx);

        if (!feature_name) {
            vmaf_log(VMAF_LOG_LEVEL_ERROR,
                     "vmaf_predict_score_at_index(): could not generate "
                     "feature name\n");
            err = -ENOMEM;
            goto free_node;
        }

        double feature_score;
        err = vmaf_feature_collector_get_score(feature_collector,
                                               feature_name, &feature_score,
                                               index);

        if (err) {
            if (!propagate_metadata) {
              vmaf_log(VMAF_LOG_LEVEL_ERROR,
                       "vmaf_predict_score_at_index(): no feature '%s' "
                       "at index %d\n", feature_name, index);
            }
            free(feature_name);
            goto free_node;
        }
        free(feature_name);

        err = normalize(model, model->feature[i].slope,
                        model->feature[i].intercept, &feature_score);
        if (err) goto free_node;

        node[i].index = i + 1;
        node[i].value = feature_score;
    }
    if (model->chroma_from_luma.enabled) {
        if (!model->chroma_from_luma.chroma_correction_parameter)
            goto free_node;
        err = post_process_feature_from_another(
            model, node,
            model->chroma_from_luma.chroma_correction_parameter,
            0.0, "adm3", "speed_chroma");
        if (err) goto free_node;
    }

    node[model->n_features].index = -1;

    double prediction = svm_predict(model->svm, node);

    err = denormalize(model, &prediction);
    if (err) goto free_node;

    err = transform(model, &prediction, flags);
    if (err) goto free_node;

    err = clip(model, &prediction, flags);
    if (err) goto free_node;

    if (write_prediction) {
        err = vmaf_feature_collector_append(feature_collector, model->name,
                                            prediction, index);
        if (err) goto free_node;
    }

    *vmaf_score = prediction;

free_node:
    free(node);
    return err;
}
