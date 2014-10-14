#ifndef XDG_SHELL_H
#define XDG_SHELL_H

#include <stdint.h>
#include <wayland-server.h>

struct wl_client;
struct ivi_layout_interface;

struct wl_global *
xdg_global_create(struct wl_display *display,
                  int version,
                  void *data);
void
bind_xdg_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id);

void
set_ivi_layout_interface(struct ivi_layout_interface *interface);

#endif /* XDG_SHELL_H */
