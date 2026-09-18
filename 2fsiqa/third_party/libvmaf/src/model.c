#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "libvmaf/model.h"

#include "log.h"
#include "model.h"
#include "read_json_model.h"
#include "svm.h"

char *vmaf_model_generate_name(VmafModelConfig *cfg)
{
    const char *default_name = "vmaf";
    const size_t name_sz =
        cfg->name ? strlen(cfg->name) + 1 : strlen(default_name) + 1;

    char *name = malloc(name_sz);
    if (!name) return NULL;
    memset(name, 0, name_sz);

    if (!cfg->name)
        strcpy(name, default_name);
    else
        strcpy(name, cfg->name);

    return name;
}

int vmaf_model_load_from_path(VmafModel **model, VmafModelConfig *cfg,
                              const char *path)
{
    int err = vmaf_read_json_model_from_path(model, cfg, path);
    if (err) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR,
                 "could not read model from path: \"%s\"\n", path);
        const char *ext = strrchr(path, '.');
        if (ext && !strcmp(ext, ".pkl")) {
            vmaf_log(
                VMAF_LOG_LEVEL_ERROR,
                "support for pkl model files has been removed, use json\n");
        }
    }
    return err;
}

void vmaf_model_destroy(VmafModel *model)
{
    if (!model) return;
    free(model->path);
    free(model->name);
    svm_free_and_destroy_model(&(model->svm));
    for (unsigned i = 0; i < model->n_features; i++) {
        free(model->feature[i].name);
        vmaf_dictionary_free(&model->feature[i].opts_dict);
    }
    free(model->feature);
    free(model->score_transform.knots.list);
    free(model);
}

