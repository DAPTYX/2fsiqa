// Part of the 2fsiqa. Copyright (C) 2026 DAPTYX (AGPL)
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sanitize_stem(const char* path, char* out, size_t out_size)
{
    const char* base = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }

    size_t i = 0;
    if (base[0] >= '0' && base[0] <= '9') {
        if (i + 1 < out_size) {
            out[i++] = '_';
        }
    }

    for (const char* p = base; *p && i + 1 < out_size; ++p) {
        if (*p == '.') {
            break;
        }
        if (isalnum((unsigned char)*p)) {
            out[i++] = *p;
        } else {
            out[i++] = '_';
        }
    }
    out[i] = '\0';
}

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: %s <input> <output.cpp> [stem]\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];

    char stem[256];
    if (argc == 4) {
        strncpy(stem, argv[3], sizeof(stem) - 1);
        stem[sizeof(stem) - 1] = '\0';
    } else {
        sanitize_stem(input_path, stem, sizeof(stem));
    }

    FILE* input = fopen(input_path, "rb");
    if (!input) {
        fprintf(stderr, "cannot open input: %s\n", input_path);
        return 1;
    }

    FILE* output = fopen(output_path, "wb");
    if (!output) {
        fclose(input);
        fprintf(stderr, "cannot open output: %s\n", output_path);
        return 1;
    }

    fprintf(output, "#include <cstddef>\n");
    fprintf(output, "#include <cstdint>\n");
    fprintf(output, "namespace embedded {\n");
    fprintf(output, "extern const unsigned char %s_data[] = {\n", stem);

    unsigned int length = 0;
    unsigned char byte;
    int column = 0;
    while (fread(&byte, 1, 1, input) == 1) {
        if (column == 0) {
            fprintf(output, "  ");
        }
        fprintf(output, "0x%02x,", byte);
        ++length;
        ++column;
        if (column >= 12) {
            fprintf(output, "\n");
            column = 0;
        } else {
            fprintf(output, " ");
        }
    }

    if (column != 0) {
        fprintf(output, "\n");
    }

    fprintf(output, "};\n");
    fprintf(output, "extern const size_t %s_size = %u;\n", stem, length);
    fprintf(output, "}\n");

    fclose(output);

    if (ferror(input) || !feof(input)) {
        fclose(input);
        return 1;
    }
    fclose(input);
    return 0;
}
