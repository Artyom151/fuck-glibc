#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

typedef int (*SDL_Init_t)(unsigned int flags);
typedef const char* (*SDL_GetError_t)(void);
typedef void (*SDL_Quit_t)(void);
typedef int (*SDL_GetNumVideoDrivers_t)(void);
typedef const char* (*SDL_GetVideoDriver_t)(int index);

int main(void) {
    printf("\n============================================\n");
    printf("   GLIBC SDL2 & Gaming Subsystem Tester     \n");
    printf("============================================\n");

    printf("[+] Dynamically loading libSDL2-2.0.so.0...\n");
    void *sdl_handle = dlopen("libSDL2-2.0.so.0", RTLD_NOW);
    if (!sdl_handle) {
        printf("[-] dlopen failed: %s\n", dlerror());
        return 1;
    }
    printf("[+] libSDL2-2.0.so.0 loaded successfully via glibc dlopen!\n");

    SDL_GetNumVideoDrivers_t get_num_drivers = (SDL_GetNumVideoDrivers_t)dlsym(sdl_handle, "SDL_GetNumVideoDrivers");
    SDL_GetVideoDriver_t get_driver = (SDL_GetVideoDriver_t)dlsym(sdl_handle, "SDL_GetVideoDriver");

    if (get_num_drivers && get_driver) {
        int num = get_num_drivers();
        printf("[+] Available SDL2 Video Drivers (%d found):\n", num);
        for (int i = 0; i < num; i++) {
            printf("    [%d] %s\n", i, get_driver(i));
        }
    }

    printf("\n[+] Dynamically loading libvulkan.so.1 (Vulkan API)...\n");
    void *vk_handle = dlopen("libvulkan.so.1", RTLD_NOW);
    if (vk_handle) {
        printf("[+] libvulkan.so.1 loaded successfully!\n");
        dlclose(vk_handle);
    } else {
        printf("[-] libvulkan.so.1 not available: %s\n", dlerror());
    }

    printf("\n[+] Dynamically loading libGL.so.1 (OpenGL API)...\n");
    void *gl_handle = dlopen("libGL.so.1", RTLD_NOW);
    if (gl_handle) {
        printf("[+] libGL.so.1 loaded successfully!\n");
        dlclose(gl_handle);
    } else {
        printf("[-] libGL.so.1 not available: %s\n", dlerror());
    }

    dlclose(sdl_handle);
    printf("\n[SUCCESS] Gaming subsystem test finished successfully!\n\n");
    return 0;
}
