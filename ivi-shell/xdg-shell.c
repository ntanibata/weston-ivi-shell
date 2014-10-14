#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wformat-security"

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <linux/input.h>
#include "config.h"
#include "xdg-shell.h"
#include "ivi-shell.h"
#include "ivi-shell-private.h"
#include "ivi-layout.h"
#include "xdg-shell-server-protocol.h"

#ifndef static_assert
#define static_assert(cond, msg)
#endif


#if 1
#define VDEBUG \
    printf("[%s(%d)] ", __func__, __LINE__); \
    printf
#else
#define __sl() /
#define VDEBUG __sl()__sl()
#endif

static const uint32_t XDG_SURFACE_ID = 40000;

enum shell_surface_type {
    SHELL_SURFACE_NONE,
    SHELL_SURFACE_TOPLEVEL,
    SHELL_SURFACE_POPUP,
    SHELL_SURFACE_XWAYLAND
};

struct shell_seat {
    struct weston_seat *seat;
    struct wl_listener seat_destroy_listener;
    struct weston_surface *focused_surface;

    struct wl_listener caps_changed_listener;
    struct wl_listener pointer_focus_listener;
    struct wl_listener keyboard_focus_listener;

    struct {
        struct weston_pointer_grab grab;
        struct wl_list surfaces_list;
        struct wl_client *client;
        int32_t initial_up;
    } popup_grab;
};

struct shell_client {
    struct wl_resource *resource;
    struct wl_client *client;
    struct ivi_shell *shell;
    struct wl_listener destroy_listener;
    struct wl_event_source *ping_timer;
    uint32_t ping_serial;
    int unresponsive;
};

struct xdg_shell_surface {
    struct ivi_shell_surface base;
	struct wl_signal destroy_signal;
	struct shell_client *owner;

	struct weston_view *view;
	int32_t last_width, last_height;
	struct wl_listener surface_destroy_listener;
	struct wl_listener resource_destroy_listener;

	struct weston_surface *parent;
	struct wl_list children_list;  /* child surfaces of this one */
	struct wl_list children_link;  /* sibling surfaces of this one */

	enum shell_surface_type type;
	char *title, *class;
	int32_t saved_x, saved_y;
	int32_t saved_width, saved_height;
	bool saved_position_valid;
	bool saved_size_valid;
	bool saved_rotation_valid;
	int unresponsive, grabbed;
	uint32_t resize_edges;

	struct {
		struct weston_transform transform;
		struct weston_matrix rotation;
	} rotation;

	struct {
		struct wl_list grab_link;
		int32_t x, y;
		struct shell_seat *shseat;
		uint32_t serial;
	} popup;

	struct {
		int32_t x, y;
		uint32_t flags;
	} transient;

	struct {
		enum wl_shell_surface_fullscreen_method type;
		struct weston_transform transform; /* matrix from x, y */
		uint32_t framerate;
		struct weston_view *black_view;
	} fullscreen;

	struct weston_transform workspace_transform;

	struct weston_output *fullscreen_output;
	struct weston_output *output;
	struct wl_list link;

	const struct weston_shell_client *client;

	struct surface_state {
		bool maximized;
		bool fullscreen;
		bool relative;
		bool lowered;
	} state, next_state, requested_state; /* surface states */
	bool state_changed;
	bool state_requested;

	struct {
		int32_t x, y, width, height;
	} geometry, next_geometry;
	bool has_set_geometry, has_next_geometry;

	int focus_count;
};

static struct ivi_layout_interface *ivi_layout;

/* begin xdg-surface interface */
static void
xdg_surface_destroy(struct wl_client *client, struct wl_resource *resource);
static void
xdg_surface_set_parent(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *parent_resource);
static void
xdg_surface_set_transient_for(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *parent_resource);
static void
xdg_surface_set_margin(struct wl_client *client,
                       struct wl_resource *resource,
                       int32_t left,
                       int32_t right,
                       int32_t top,
                       int32_t bottom);
static void
xdg_surface_set_title(struct wl_client *client,
                      struct wl_resource *resource,
                      const char *title);
static void
xdg_surface_set_app_id(struct wl_client *client,
                       struct wl_resource *resource,
                       const char *app_id);
static void
xdg_surface_show_window_menu(struct wl_client *client,
                             struct wl_resource *surface_resource,
                             struct wl_resource *seat_resource,
                             uint32_t serial,
                             int32_t x,
                             int32_t y);
static void
xdg_surface_move(struct wl_client *client,
                 struct wl_resource *resource,
                 struct wl_resource *seat_resource,
                 uint32_t serial);
static void
xdg_surface_resize(struct wl_client *client,
                   struct wl_resource *resource,
                   struct wl_resource *seat_resource,
                   uint32_t serial,
                   uint32_t edges);
static void
xdg_surface_ack_configure(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t serial);
static void
xdg_surface_set_window_geometry(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t x,
                                int32_t y,
                                int32_t width,
                                int32_t height);
static void
xdg_surface_set_maximized(struct wl_client *client,
                          struct wl_resource *resource);
static void
xdg_surface_unset_maximized(struct wl_client *client,
                            struct wl_resource *resource);
static void
xdg_surface_set_fullscreen(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *output_resource);
static void
xdg_surface_unset_fullscreen(struct wl_client *client,
                             struct wl_resource *resource);
static void
xdg_surface_request_change_state(struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t state,
                                 uint32_t value,
                                 uint32_t serial);
static void
xdg_surface_ack_change_state(struct wl_client *client,
                             struct wl_resource *resource,
                             uint32_t state,
                             uint32_t value,
                             uint32_t serial);
static void
xdg_surface_set_minimized(struct wl_client *client,
                          struct wl_resource *resource);

static const struct xdg_surface_interface xdg_surface_implementation = {
    xdg_surface_destroy,
    xdg_surface_set_parent,
    xdg_surface_set_title,
    xdg_surface_set_app_id,
    xdg_surface_show_window_menu,
    xdg_surface_move,
    xdg_surface_resize,
    xdg_surface_ack_configure,
    xdg_surface_set_window_geometry,
    xdg_surface_set_maximized,
    xdg_surface_unset_maximized,
    xdg_surface_set_fullscreen,
    xdg_surface_unset_fullscreen,
    xdg_surface_set_minimized
};

static void
xdg_send_configure(struct weston_surface *surface,
                   int32_t width,
                   int32_t height);

static const struct weston_shell_client xdg_client = {
    xdg_send_configure
};
/* end xdg-surface interface */

/* begin xdg-popup interface */
static void
xdg_popup_destroy(struct wl_client *client, struct wl_resource *resource);

static const struct xdg_popup_interface xdg_popup_implementation = {
    xdg_popup_destroy,
};
static void
xdg_popup_send_configure(struct weston_surface *surface,
                         int32_t width,
                         int32_t height);

static const struct weston_shell_client xdg_popup_client = {
    xdg_popup_send_configure
};
/* end xdg-popup interface */

static void
xdg_shell_surface_configure(struct weston_surface *, int32_t, int32_t);
static void
get_output_work_area(struct ivi_shell *shell,
                     struct weston_output *output,
                     pixman_rectangle32_t *area);

static struct ivi_shell_surface *
cast_to_ivi_shell_surface(struct xdg_shell_surface *xdgsurf)
{
    return &xdgsurf->base;
}

static struct xdg_shell_surface *
cast_to_xdg_shell_surface(struct ivi_shell_surface *ivisurf)
{
    return (struct xdg_shell_surface *)ivisurf;
}

static struct xdg_shell_surface *
get_xdg_shell_surface(struct weston_surface *surface)
{
    if (surface->configure == xdg_shell_surface_configure) {
        return surface->configure_private;
    } else {
        return NULL;
    }
}

static struct ivi_shell_surface *
get_ivi_shell_surface(struct weston_surface *surface)
{
    if (surface->configure == xdg_shell_surface_configure) {
        return surface->configure_private;
    } else {
        return NULL;
    }
}

static struct xdg_shell_surface *
get_shell_surface(struct weston_surface *surface)
{
    return get_xdg_shell_surface(surface);
}

static void
shell_surface_set_parent(struct xdg_shell_surface *shsurf,
                         struct weston_surface *parent)
{
    shsurf->parent = parent;

    wl_list_remove(&shsurf->children_link);
    wl_list_init(&shsurf->children_link);

    /* insert into the parent surface’s child list. */
    if (parent != NULL) {
        struct xdg_shell_surface *parent_shsurf = get_shell_surface(parent);
        if (parent_shsurf != NULL)
            wl_list_insert(&parent_shsurf->children_list,
                           &shsurf->children_link);
    }
}

static struct ivi_shell *
shell_surface_get_shell(struct xdg_shell_surface *shsurf)
{
    return shsurf->base.shell;
}

static void
set_window_geometry(struct xdg_shell_surface *shsurf,
                    int32_t x,
                    int32_t y,
                    int32_t width,
                    int32_t height)
{
    shsurf->next_geometry.x = x;
    shsurf->next_geometry.y = y;
    shsurf->next_geometry.width = width;
    shsurf->next_geometry.height = height;
    shsurf->has_next_geometry = true;
}

static struct weston_output *
get_focused_output(struct weston_compositor *compositor)
{
    struct weston_seat *seat;
    struct weston_output *output = NULL;

    wl_list_for_each(seat, &compositor->seat_list, link) {
        /* Priority has touch focus, then pointer and
         * then keyboard focus. We should probably have
         * three for loops and check frist for touch,
         * then for pointer, etc. but unless somebody has some
         * objections, I think this is sufficient. */
        if (seat->touch && seat->touch->focus)
            output = seat->touch->focus->output;
        else if (seat->pointer && seat->pointer->focus)
            output = seat->pointer->focus->output;
        else if (seat->keyboard && seat->keyboard->focus)
            output = seat->keyboard->focus->output;

        if (output)
            break;
    }

    return output;
}

static struct weston_output *
get_default_output(struct weston_compositor *compositor)
{
    return container_of(compositor->output_list.next, struct weston_output, link);
}

static void
shell_surface_set_output(struct xdg_shell_surface *shsurf,
                         struct weston_output *output)
{
}

static void
send_configure_for_surface(struct xdg_shell_surface *shsurf)
{
    int32_t width, height;
    struct surface_state *state;

    if (shsurf->state_requested)
        state = &shsurf->requested_state;
    else if (shsurf->state_changed)
        state = &shsurf->next_state;
    else
        state = &shsurf->state;

    if (state->fullscreen) {
        width = shsurf->output->width;
        height = shsurf->output->height;
    } else if (state->maximized) {
        struct ivi_shell *shell;
        pixman_rectangle32_t area = {};

        shell = shell_surface_get_shell(shsurf);
        get_output_work_area(shell, shsurf->output, &area);

        width = area.width;
        height = area.height;
    } else {
        width = 0;
        height = 0;
    }

    shsurf->client->send_configure(shsurf->base.surface, width, height);
}

static void
get_output_panel_size(struct ivi_shell *shell,
                      struct weston_output *output,
                      int *width,
                      int *height)
{
}

static void
get_output_work_area(struct ivi_shell *shell,
                     struct weston_output *output,
                     pixman_rectangle32_t *area)
{
}

static void
set_minimized(struct weston_surface *surface, uint32_t is_true)
{
}

/* begin xdg-shell interface implementations */
static void
xdg_use_unstable_version(struct wl_client *client,
                         struct wl_resource *resource,
                         int32_t version)
{
    if (version > 1) {
        wl_resource_post_error(resource,
                               1,
                               "xdg-shell:: version not implemented yet.");
        return;
    }
}

static void
xdg_shell_surface_configure(struct weston_surface *surface,
                            int32_t sx,
                            int32_t sy)
{
    struct ivi_shell_surface *ivisurf = get_ivi_shell_surface(surface);
    struct weston_view *view = NULL;
    float from_x = 0.0f;
    float from_y = 0.0f;
    float to_x = 0.0f;
    float to_y = 0.0f;

    if ((surface->width == 0) || (surface->height == 0) || (ivisurf == NULL)) {
        return;
    }

    view = ivi_layout->get_weston_view(ivisurf->layout_surface);
    if (view == NULL) {
        return;
    }

    if (ivisurf->width != surface->width || ivisurf->height != surface->height) {

        ivisurf->width  = surface->width;
        ivisurf->height = surface->height;

        weston_view_to_global_float(view, 0, 0, &from_x, &from_y);
        weston_view_to_global_float(view, sx, sy, &to_x, &to_y);

        weston_view_set_position(view,
                  view->geometry.x + to_x - from_x,
                  view->geometry.y + to_y - from_y);
        weston_view_update_transform(view);

        ivi_layout->surfaceConfigure(ivisurf->layout_surface, surface->width, surface->height);
    }
}

static struct ivi_shell_surface *
is_surf_in_surfaces(struct wl_list *list_surf, uint32_t id_surface)
{
    struct ivi_shell_surface *ivisurf;

    wl_list_for_each(ivisurf, list_surf, link) {
        if (ivisurf->id_surface == id_surface) {
            return ivisurf;
        }
    }

    return NULL;
}

static void
surface_configure_notify(struct wl_listener *listener, void *data)
{
    struct ivi_layout_surface* layout_surf =
        (struct ivi_layout_surface*) data;

    struct ivi_shell_surface *shell_surf =
        container_of(listener,
                     struct ivi_shell_surface,
                     configured_listener);

    int32_t dim[2] = {};
    ivi_layout->get_surface_dimension(layout_surf, dim);

    if(shell_surf->resource)
        xdg_send_configure(shell_surf->surface, dim[0], dim[1]);
}

static const struct {
    enum ivi_layout_warning_flag flag;
    const char *message;
} warning_strings[] = {
    { IVI_WARNING_INVALID_WL_SURFACE, "wl_surface is invalid" },
    { IVI_WARNING_IVI_ID_IN_USE, "surface_id is already assigned by another app" },
};

static void
xdg_get_xdg_surface(struct wl_client *client,
                    struct wl_resource *resource,
                    uint32_t id,
                    struct wl_resource *surface_resource)
{
    struct shell_client *sc = wl_resource_get_user_data(resource);
    struct ivi_shell *shell = sc->shell;
    struct xdg_shell_surface *xdgsurf = NULL;
    struct ivi_shell_surface *ivisurf = NULL;
    struct ivi_layout_surface *layout_surface = NULL;
    struct weston_surface *weston_surface = wl_resource_get_user_data(surface_resource);
    struct wl_resource *res;
    int32_t warn_idx = -1;
    pid_t pid;
    uint32_t id_surface;

    wl_client_get_credentials(client, &pid, NULL, NULL);
    id_surface = XDG_SURFACE_ID + pid;

    if (weston_surface != NULL) {

    /* check if a surface already has another role*/
    if (weston_surface->configure) {
        wl_resource_post_error(weston_surface->resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "surface->configure already "
                               "set");
        return;
    }

    layout_surface = ivi_layout->surfaceCreate(weston_surface, id_surface);

    if (layout_surface == NULL)
        warn_idx = 1;
    } else {
        warn_idx = 0;
    }

    res = wl_resource_create(client, &xdg_surface_interface, 1, id);
    if (res == NULL) {
        wl_client_post_no_memory(client);
        return;
    }

    if (warn_idx >= 0) {
        wl_resource_set_implementation(res, &xdg_surface_implementation,
                                       NULL, NULL);

        ivi_layout->emitWarningSignal(id_surface, warning_strings[warn_idx].flag);

        wl_resource_post_error(res,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               warning_strings[warn_idx].message);
        return;
    }

    ivisurf = is_surf_in_surfaces(&shell->ivi_surface_list, id_surface);
    xdgsurf = cast_to_xdg_shell_surface(ivisurf);
    if (xdgsurf == NULL) {
        xdgsurf = zalloc(sizeof *xdgsurf);
        if (xdgsurf == NULL) {
            wl_resource_post_no_memory(res);
            return;
        }

        ivisurf = cast_to_ivi_shell_surface(xdgsurf);
        wl_list_init(&ivisurf->link);
        wl_list_insert(&shell->ivi_surface_list, &ivisurf->link);

        ivisurf->shell = shell;
        ivisurf->id_surface = id_surface;
    }

    ivisurf->resource = res;
    ivisurf->width = 0;
    ivisurf->height = 0;
    ivisurf->layout_surface = layout_surface;
    ivisurf->configured_listener.notify = surface_configure_notify;
    ivi_layout->add_surface_configured_listener(layout_surface, &ivisurf->configured_listener);

    ivisurf->surface = weston_surface;

    weston_surface->configure = xdg_shell_surface_configure;
    weston_surface->configure_private = xdgsurf;

    wl_resource_set_implementation(res, &xdg_surface_implementation,
                                   xdgsurf, NULL);
}

static void
xdg_get_xdg_popup(struct wl_client *client,
                  struct wl_resource *resource,
                  uint32_t id,
                  struct wl_resource *surface_resource,
                  struct wl_resource *parent_resource,
                  struct wl_resource *seat_resource,
                  uint32_t serial,
                  int32_t x,
                  int32_t y,
                  uint32_t flags)
{
}

static int
xdg_ping_timeout_handler(void *data)
{
    return 1;
}

static void
handle_xdg_ping(struct xdg_shell_surface *shsurf, uint32_t serial)
{
}

static void
xdg_pong(struct wl_client *client,
         struct wl_resource *resource,
         uint32_t serial)
{
}

static const struct xdg_shell_interface xdg_implementation = {
    xdg_use_unstable_version,
    xdg_get_xdg_surface,
    xdg_get_xdg_popup,
    xdg_pong
};

/* end xdg-shell interface implementations */

/* begin xdg-surface interface implementations */
static void
xdg_surface_destroy(struct wl_client *client,
                    struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}
static void
xdg_surface_set_parent(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *parent_resource)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);
    struct weston_surface *parent;

    if (parent_resource)
        parent = wl_resource_get_user_data(parent_resource);
    else
        parent = NULL;

    shell_surface_set_parent(shsurf, parent);
}

static void
xdg_surface_set_transient_for(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *parent_resource)
{
}

static void
xdg_surface_set_margin(struct wl_client *client,
                       struct wl_resource *resource,
                       int32_t left,
                       int32_t right,
                       int32_t top,
                       int32_t bottom)
{
}

static void
xdg_surface_set_title(struct wl_client *client,
                      struct wl_resource *resource,
                      const char *title)
{
}

static void
xdg_surface_set_app_id(struct wl_client *client,
                       struct wl_resource *resource,
                       const char *app_id)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);

    free(shsurf->class);
    shsurf->class = strdup(app_id);
}

static void
xdg_surface_show_window_menu(struct wl_client *client,
                             struct wl_resource *surface_resource,
                             struct wl_resource *seat_resource,
                             uint32_t serial,
                             int32_t x,
                             int32_t y)
{
}

static void
xdg_surface_move(struct wl_client *client,
                 struct wl_resource *resource,
                 struct wl_resource *seat_resource,
                 uint32_t serial)
{
}

static void
xdg_surface_resize(struct wl_client *client,
                   struct wl_resource *resource,
                   struct wl_resource *seat_resource,
                   uint32_t serial,
                   uint32_t edges)
{
}

static void
xdg_surface_ack_configure(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t serial)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);

    if (shsurf->state_requested) {
        shsurf->next_state = shsurf->requested_state;
        shsurf->state_changed = true;
        shsurf->state_requested = false;
    }
}

static void
xdg_surface_set_window_geometry(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t x,
                                int32_t y,
                                int32_t width,
                                int32_t height)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);
    set_window_geometry(shsurf, x, y, width, height);
}

static void
xdg_surface_set_maximized(struct wl_client *client,
                          struct wl_resource *resource)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);
    struct weston_output *output;

    shsurf->state_requested = true;
    shsurf->requested_state.maximized = true;

    if (!weston_surface_is_mapped(shsurf->base.surface))
        output = get_focused_output(shsurf->base.surface->compositor);
    else
        output = shsurf->base.surface->output;

    shell_surface_set_output(shsurf, output);
    send_configure_for_surface(shsurf);
}

static void
xdg_surface_unset_maximized(struct wl_client *client,
                            struct wl_resource *resource)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);

    shsurf->state_requested = true;
    shsurf->requested_state.maximized = false;
    send_configure_for_surface(shsurf);
}

static void
xdg_surface_set_fullscreen(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *output_resource)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);
    struct weston_output *output;

    shsurf->state_requested = true;
    shsurf->requested_state.fullscreen = true;

    if (output_resource)
        output = wl_resource_get_user_data(output_resource);
    else
        output = NULL;

    /* handle clients launching in fullscreen */
    if (output == NULL && !weston_surface_is_mapped(shsurf->base.surface)) {
        /* Set the output to the one that has focus currently. */
        assert(shsurf->base.surface);
        output = get_focused_output(shsurf->base.surface->compositor);
    }

    shell_surface_set_output(shsurf, output);
    shsurf->fullscreen_output = shsurf->output;

    send_configure_for_surface(shsurf);
}

static void
xdg_surface_unset_fullscreen(struct wl_client *client,
                             struct wl_resource *resource)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);

    shsurf->state_requested = true;
    shsurf->requested_state.fullscreen = false;
    send_configure_for_surface(shsurf);
}

static void
xdg_surface_set_minimized(struct wl_client *client,
                          struct wl_resource *resource)
{
    struct xdg_shell_surface *shsurf = wl_resource_get_user_data(resource);

    if (shsurf->type != SHELL_SURFACE_TOPLEVEL)
        return;

     /* apply compositor's own minimization logic (hide) */
    set_minimized(shsurf->base.surface, 1);
}

static void
xdg_send_configure(struct weston_surface *surface,
                   int32_t width,
                   int32_t height)
{
}
/* end xdg-surface interface implementations */

/* begin xdg-popup interface implementations */
static void
xdg_popup_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void
xdg_popup_send_configure(struct weston_surface *surface,
                         int32_t width,
                         int32_t height)
{

}
/* end xdg-popup interface implementations */

static int
xdg_shell_unversioned_dispatch(const void *implementation,
                               void *target,
                               uint32_t opcode,
                               const struct wl_message *message,
                               union wl_argument *args)
{
    struct wl_resource *resource = target;
    struct shell_client *sc = wl_resource_get_user_data(resource);

    if (opcode != 0) {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "must call use_unstable_version first");
        return 0;

    }

    static const int XDG_SERVER_VERSION = 4;

    static_assert(XDG_SERVER_VERSION == XDG_SHELL_VERSION_CURRENT,
                  "shell implementation doesn't match protocol version");

    if (args[0].i != XDG_SERVER_VERSION) {
        wl_resource_post_error(resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "incompatible version, server is %d "
                               "client was %d",
                               XDG_SERVER_VERSION, args[0].i);
        return 0;
    }

    wl_resource_set_implementation(resource, &xdg_implementation, sc, NULL);
    return 1;
}

static void
handle_shell_client_destroy(struct wl_listener *listener, void *data)
{
    struct shell_client *sc =
        container_of(listener, struct shell_client, destroy_listener);

    if (sc->ping_timer) {
        wl_event_source_remove(sc->ping_timer);
    }

    free(sc);
}

static struct shell_client *
shell_client_create(struct wl_client *client,
                    struct ivi_shell *shell,
                    const struct wl_interface *interface,
                    uint32_t id)
{
    struct shell_client *sc;

    sc = zalloc(sizeof *sc);
    if (sc == NULL) {
        wl_client_post_no_memory(client);
        return NULL;
    }

    sc->resource = wl_resource_create(client, interface, 1, id);
    if (sc->resource == NULL) {
        free(sc);
        wl_client_post_no_memory(client);
        return NULL;
    }

    sc->client = client;
    sc->shell = shell;
    sc->destroy_listener.notify = handle_shell_client_destroy;
    wl_client_add_destroy_listener(client, &sc->destroy_listener);

    return sc;
}

struct wl_global *
xdg_global_create(struct wl_display *display,
                  int version,
                  void *data)
{
    return wl_global_create(display, &xdg_shell_interface, version, data, bind_xdg_shell);
}

void
bind_xdg_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct ivi_shell *shell = data;
    struct shell_client *sc;

    sc = shell_client_create(client, shell, &xdg_shell_interface, id);

    if (sc != NULL) {
        wl_resource_set_dispatcher(sc->resource,
                                   xdg_shell_unversioned_dispatch,
                                   NULL,
                                   sc,
                                   NULL);
    }
}

void
set_ivi_layout_interface(struct ivi_layout_interface *interface)
{
    ivi_layout = interface;
}
