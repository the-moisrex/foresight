// Created for unstretch_tablet: runtime-loaded libdrm interface.

module;
#include <cstdint>

export module fs8.compositor.drm_loader;

export namespace fs8::compositor {

    // ── Function pointer types (void* returns; structs defined in .cxx) ─

    using fn_drmModeGetResources  = void* (*) (int fd);
    using fn_drmModeFreeResources = void (*)(void* resources);
    using fn_drmModeGetConnector  = void* (*) (int fd, uint32_t connectorId);
    using fn_drmModeFreeConnector = void (*)(void* connector);
    using fn_drmModeGetCrtc       = void* (*) (int fd, uint32_t crtcId);
    using fn_drmModeFreeCrtc      = void (*)(void* crtc);
    using fn_drmModeGetEncoder    = void* (*) (int fd, uint32_t encoderId);
    using fn_drmModeFreeEncoder   = void (*)(void* encoder);

    /// Runtime-loaded libdrm data. Call drm_lib_load() to populate.
    struct [[nodiscard]] drm_lib {
        void* handle = nullptr;

        fn_drmModeGetResources  get_resources  = nullptr;
        fn_drmModeFreeResources free_resources = nullptr;
        fn_drmModeGetConnector  get_connector  = nullptr;
        fn_drmModeFreeConnector free_connector = nullptr;
        fn_drmModeGetCrtc       get_crtc       = nullptr;
        fn_drmModeFreeCrtc      free_crtc      = nullptr;
        fn_drmModeGetEncoder    get_encoder    = nullptr;
        fn_drmModeFreeEncoder   free_encoder   = nullptr;
    };

    /// Populate a drm_lib by dlopening libdrm and resolving symbols.
    void drm_lib_load(drm_lib& lib) noexcept;

    /// Release resources held by a drm_lib.
    void drm_lib_unload(drm_lib& lib) noexcept;

    /// Move contents from src into dst, leaving src empty.
    void drm_lib_move(drm_lib& dst, drm_lib& src) noexcept;

    [[nodiscard]] bool drm_lib_is_loaded(drm_lib const& lib) noexcept;

} // namespace fs8::compositor
