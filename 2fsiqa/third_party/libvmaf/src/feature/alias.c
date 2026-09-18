#include <string.h>

typedef struct {
    const char *name, *alias;
} Alias;

static const Alias alias_map[] = {
    { "VMAF_integer_feature_adm2_score", "integer_adm2" },
    { "VMAF_integer_feature_aim_score", "integer_aim" },
    { "VMAF_integer_feature_adm3_score", "integer_adm3" },
    { "VMAF_integer_feature_motion_sad_score", "integer_motion_sad" },
    { "VMAF_integer_feature_motion2_score", "integer_motion2" },
    { "VMAF_integer_feature_motion3_score", "integer_motion3" },
    { "Cambi_feature_cambi_score", "cambi" },
    { "Speed_chroma_feature_speed_chroma_u_score", "speed_chroma_u" },
    { "Speed_chroma_feature_speed_chroma_v_score", "speed_chroma_v" },
    { "Speed_chroma_feature_speed_chroma_uv_score", "speed_chroma_uv" },
};

const char *vmaf_feature_name_alias(const char *feature_name)
{
    if (!feature_name) return NULL;
    const unsigned n = sizeof(alias_map) / sizeof(alias_map[0]);
    for (unsigned i = 0; i < n; i++) {
        if (!strcmp(feature_name, alias_map[i].name))
            return alias_map[i].alias;
    }
    return feature_name;
}
