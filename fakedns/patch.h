#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>

#define PATCH_FUNC(ret_type, name, ...) \
extern "C" ret_type name(__VA_ARGS__) { \
    static ret_type (*orig_func)(__VA_ARGS__); \
    if (!orig_func) { \
        orig_func = (ret_type (*)(__VA_ARGS__))dlsym(RTLD_NEXT, #name); \
        if (!orig_func) { \
            fprintf(stderr, "Error: could not find original function %s\n", #name); \
            exit(EXIT_FAILURE); \
        } \
    }
#define PATCH_END }

// example
// PATCH_FUNC(int, open64, const char *pathname, int flags, mode_t mode)
//     // printf("Custom open64 called with pathname: %s\n", pathname);
//     if (strcmp(pathname, "/dat/research/sim/release/sim-2.0.1/templates/config.xml") == 0 ||
//         strcmp(pathname, "/dat_ghead0/research/sim/release/sim-2.0.1/templates/config.xml") == 0) {
//         pathname = "/home/yli/workspace/datasets/config.xml";
//     }
//     return orig_func(pathname, flags, mode);
// PATCH_END