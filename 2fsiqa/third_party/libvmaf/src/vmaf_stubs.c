#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "feature/feature_collector.h"
#include "libvmaf/libvmaf.h"

int vmaf_write_output_xml(VmafContext *vmaf, VmafFeatureCollector *fc, FILE *outfile,
                          unsigned subsample, unsigned width, unsigned height,
                          double fps, unsigned pic_cnt)
{
    (void)vmaf;
    (void)fc;
    (void)outfile;
    (void)subsample;
    (void)width;
    (void)height;
    (void)fps;
    (void)pic_cnt;
    return -1;
}

int vmaf_write_output_json(VmafContext *vmaf, VmafFeatureCollector *fc,
                           FILE *outfile, unsigned subsample, double fps,
                           unsigned pic_cnt)
{
    (void)vmaf;
    (void)fc;
    (void)outfile;
    (void)subsample;
    (void)fps;
    (void)pic_cnt;
    return -1;
}

int vmaf_write_output_csv(VmafFeatureCollector *fc, FILE *outfile,
                          unsigned subsample)
{
    (void)fc;
    (void)outfile;
    (void)subsample;
    return -1;
}

int vmaf_write_output_sub(VmafFeatureCollector *fc, FILE *outfile,
                          unsigned subsample)
{
    (void)fc;
    (void)outfile;
    (void)subsample;
    return -1;
}

int mkdirp(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}
