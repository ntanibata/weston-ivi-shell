#ifndef IVI_SHELL_PRIVATE_H
#define IVI_SHELL_PRIVATE_H

struct ivi_shell_surface
{
    struct wl_resource* resource;
    struct ivi_shell *shell;
    struct ivi_layout_surface *layout_surface;

    struct weston_surface *surface;
    uint32_t id_surface;

    int32_t width;
    int32_t height;

    struct wl_list link;

    struct wl_listener configured_listener;
};

#endif /* IVI_SHELL_PRIVATE_H */
