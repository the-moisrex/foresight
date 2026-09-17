// Created for unstretch_tablet: runtime-loaded libdrm implementation.

module;
#include <dlfcn.h>

module fs8.compositor.drm_loader;

void fs8::compositor::drm_lib_load(drm_lib& lib) noexcept {
    lib.handle = dlopen("libdrm.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!lib.handle) {
        lib.handle = dlopen("libdrm.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (!lib.handle) {
        return;
    }

    lib.get_resources  = reinterpret_cast<fn_drmModeGetResources>(dlsym(lib.handle, "drmModeGetResources"));
    lib.free_resources = reinterpret_cast<fn_drmModeFreeResources>(dlsym(lib.handle, "drmModeFreeResources"));
    lib.get_connector  = reinterpret_cast<fn_drmModeGetConnector>(dlsym(lib.handle, "drmModeGetConnector"));
    lib.free_connector = reinterpret_cast<fn_drmModeFreeConnector>(dlsym(lib.handle, "drmModeFreeConnector"));
    lib.get_crtc       = reinterpret_cast<fn_drmModeGetCrtc>(dlsym(lib.handle, "drmModeGetCrtc"));
    lib.free_crtc      = reinterpret_cast<fn_drmModeFreeCrtc>(dlsym(lib.handle, "drmModeFreeCrtc"));
    lib.get_encoder    = reinterpret_cast<fn_drmModeGetEncoder>(dlsym(lib.handle, "drmModeGetEncoder"));
    lib.free_encoder   = reinterpret_cast<fn_drmModeFreeEncoder>(dlsym(lib.handle, "drmModeFreeEncoder"));
}

void fs8::compositor::drm_lib_unload(drm_lib& lib) noexcept {
    if (lib.handle) {
        dlclose(lib.handle);
        lib.handle = nullptr;
    }
}

void fs8::compositor::drm_lib_move(drm_lib& dst, drm_lib& src) noexcept {
    dst.handle = src.handle;
    src.handle = nullptr;

    dst.get_resources  = src.get_resources;
    src.get_resources  = nullptr;
    dst.free_resources = src.free_resources;
    src.free_resources = nullptr;
    dst.get_connector  = src.get_connector;
    src.get_connector  = nullptr;
    dst.free_connector = src.free_connector;
    src.free_connector = nullptr;
    dst.get_crtc       = src.get_crtc;
    src.get_crtc       = nullptr;
    dst.free_crtc      = src.free_crtc;
    src.free_crtc      = nullptr;
    dst.get_encoder    = src.get_encoder;
    src.get_encoder    = nullptr;
    dst.free_encoder   = src.free_encoder;
    src.free_encoder   = nullptr;
}

[[nodiscard]] bool fs8::compositor::drm_lib_is_loaded(drm_lib const& lib) noexcept {
    return lib.handle
           && lib.get_resources
           && lib.free_resources
           && lib.get_connector
           && lib.free_connector
           && lib.get_crtc
           && lib.free_crtc
           && lib.get_encoder
           && lib.free_encoder;
}
