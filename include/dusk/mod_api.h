#ifndef DUSK_MOD_API_H
#define DUSK_MOD_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DUSK_MOD_API_VERSION 1

#if defined(_WIN32)
#  define DUSK_MOD_EXPORT __declspec(dllexport)
#else
#  define DUSK_MOD_EXPORT __attribute__((visibility("default")))
#endif

typedef struct DuskModAPI {
    uint32_t    api_version;
    const char* mod_dir;

    void (*log_info) (const char* fmt, ...);
    void (*log_warn) (const char* fmt, ...);
    void (*log_error)(const char* fmt, ...);

    void* (*load_resource)(const char* relative_path, size_t* out_size);
    void  (*free_resource)(void* data);

    void (*register_tab_content)(void (*draw_fn)(void* userdata), void* userdata);
    void (*register_menu_item)  (void (*draw_fn)(void* userdata), void* userdata);

    void (*hook_install)(void* fn_addr, void* tramp_fn, void** orig_store);
    void (*hook_pre)    (void* fn_addr, int32_t (*fn)(void* args));
    void (*hook_post)   (void* fn_addr, void    (*fn)(void* args));
    void (*hook_replace)(void* fn_addr, void    (*fn)(void* args));

    bool (*hook_dispatch_pre) (void* fn_addr, void* args);
    void (*hook_dispatch_post)(void* fn_addr, void* args);
} DuskModAPI;

void mod_init(DuskModAPI* api);
void mod_tick(DuskModAPI* api);

#ifdef __cplusplus
}
#endif

#endif
