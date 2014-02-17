/*
 * Copyright (C) 2013 DENSO CORPORATION
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>

#include "ivi-shell.h"
#include "compositor.h"
#include "ivi-application-server-protocol.h"
#include "weston-layout.h"

struct ivi_shell;

struct ivi_shell_surface
{
    struct ivi_shell *shell;
    struct weston_layout_surface *layout_surface;

    struct weston_surface *surface;
    uint32_t id_surface;

    int32_t width;
    int32_t height;

    struct wl_list link;
};

struct ping_timer
{
    struct wl_event_source *source;
    uint32_t serial;
};

struct shell_surface
{
    struct wl_resource *resource;

    struct weston_surface *surface;
    struct weston_view *view;
    struct wl_listener surface_destroy_listener;

    struct ivi_shell *shell;
    struct ping_timer *ping_timer;

    char *class;
    char *title;

    int32_t width;
    int32_t height;

    pid_t pid;

    const struct weston_shell_client *client;
    struct weston_output *output;

    struct wl_list link;
};

struct link_weston_surface
{
    struct wl_listener destroy_listener;
    struct weston_surface *surface;
    struct wl_list link;
};

struct ivi_shell
{
    struct wl_resource *resource;
    struct wl_listener destroy_listener;

    struct weston_compositor *compositor;
    struct weston_seat *seat;

    struct wl_list list_surface;
    struct wl_list list_weston_surface;
    struct wl_list list_shell_surface;
};

/* ------------------------------------------------------------------------- */
/* common functions                                                          */
/* ------------------------------------------------------------------------- */
static struct ivi_shell *
get_instance(void)
{
    static struct ivi_shell  *shell = NULL;

    if (NULL == shell) {
        shell = calloc(1, sizeof(*shell));
    }

    return shell;
}

static void
configure(struct weston_view *view, float x, float y)
{
    if (view != NULL) {
        weston_view_set_position(view, x, y);
        weston_view_update_transform(view);
    }
}

/* ------------------------------------------------------------------------- */
/* shell_surface functions                                                   */
/* ------------------------------------------------------------------------- */

static void
shell_surface_configure(struct weston_surface *, int32_t, int32_t);

static struct shell_surface *
get_shell_surface(struct weston_surface *surface)
{
    if (surface->configure == shell_surface_configure) {
        return surface->configure_private;
    } else {
        return NULL;
    }
}

static void
ping_timer_destroy(struct shell_surface *shsurf)
{
    if (!shsurf || !shsurf->ping_timer) {
        return;
    }

    if (shsurf->ping_timer->source) {
        wl_event_source_remove(shsurf->ping_timer->source);
    }

    free(shsurf->ping_timer);
    shsurf->ping_timer = NULL;
}

static void
destroy_shell_surface(struct shell_surface *shsurf)
{
    wl_list_remove(&shsurf->surface_destroy_listener.link);

#if 0
    shsurf->surface->configure = NULL;
#endif

    ping_timer_destroy(shsurf);

    free(shsurf->title);
    shsurf->title = NULL;

    wl_list_remove(&shsurf->link);
    free(shsurf);
    shsurf = NULL;
}

static void
shell_handle_surface_destroy(struct wl_listener *listener, void *data)
{
    struct shell_surface *shsurf = NULL;

    shsurf = container_of(listener,
                          struct shell_surface,
                          surface_destroy_listener);

    if (wl_resource_get_client(shsurf->resource)) {
        wl_resource_destroy(shsurf->resource);
    } else {
        wl_resource_destroy(shsurf->resource);
        destroy_shell_surface(shsurf);
    }
}

static void
shell_surface_configure(struct weston_surface *es,
                        int32_t sx, int32_t sy)
{
    struct shell_surface *shsurf = get_shell_surface(es);
    float from_x = 0.0f;
    float from_y = 0.0f;
    float to_x = 0.0f;
    float to_y = 0.0f;

    if ((es == NULL) || (shsurf == NULL)) {
        return;
    }

    if (shsurf->width != es->width || shsurf->height != es->height) {

        shsurf->width = es->width;
        shsurf->height = es->height;

        weston_view_to_global_float(shsurf->view, 0, 0, &from_x, &from_y);
        weston_view_to_global_float(shsurf->view, sx, sy, &to_x, &to_y);
        configure(shsurf->view,
                  shsurf->view->geometry.x + to_x - from_x,
                  shsurf->view->geometry.y + to_y - from_y);
    }
}

static void
send_configure(struct weston_surface *surface,
               uint32_t edges, int32_t width, int32_t height)
{
    struct shell_surface *shsurf = get_shell_surface(surface);

    wl_shell_surface_send_configure(shsurf->resource,
                                    edges, width, height);
}

static const struct weston_shell_client shell_client = {
    send_configure
};

static void
shell_destroy_shell_surface(struct wl_resource *resource)
{
    struct shell_surface *shsurf = wl_resource_get_user_data(resource);

    destroy_shell_surface(shsurf);
}

static void
shell_surface_pong(struct wl_client *client,
                   struct wl_resource *resource, uint32_t serial)
{
    struct shell_surface *shsurf = wl_resource_get_user_data(resource);

    if (shsurf->ping_timer == NULL) {
        return;
    }

    if (shsurf->ping_timer->serial == serial) {
        ping_timer_destroy(shsurf);
    }
}

static void
shell_surface_move(struct wl_client *client, struct wl_resource *resource,
                   struct wl_resource *seat_resource, uint32_t serial)
{
    /* not supported */
    (void)client;
    (void)resource;
    (void)seat_resource;
    (void)serial;
}

static void
shell_surface_resize(struct wl_client *client, struct wl_resource *resource,
                     struct wl_resource *seat_resource, uint32_t serial,
                     uint32_t edges)
{
    /* not supported */
    (void)client;
    (void)resource;
    (void)seat_resource;
    (void)serial;
    (void)edges;
}

static void
shell_surface_set_toplevel(struct wl_client *client,
                           struct wl_resource *resource)
{
    /* not supported */
    (void)client;
    (void)resource;
}

static void
shell_surface_set_transient(struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *parent_resource,
                            int x, int y, uint32_t flags)
{
    /* not supported */
    (void)client;
    (void)resource;
    (void)parent_resource;
    (void)x;
    (void)y;
    (void)flags;
}

static void
shell_surface_set_fullscreen(struct wl_client *client,
                             struct wl_resource *resource,
                             uint32_t method,
                             uint32_t framerate,
                             struct wl_resource *output_resource)
{
    /* not supported */
    (void)client;
    (void)resource;
    (void)method;
    (void)framerate;
    (void)output_resource;
}

static void
shell_surface_set_popup(struct wl_client *client,
                        struct wl_resource *resource,
                        struct wl_resource *seat_resource,
                        uint32_t serial,
                        struct wl_resource *parent_resource,
                        int32_t x, int32_t y, uint32_t flags)
{
    /* not supported */
    (void)client;
    (void)resource;
    (void)seat_resource;
    (void)serial;
    (void)parent_resource;
    (void)x;
    (void)y;
    (void)flags;
}

static void
shell_surface_set_maximized(struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *output_resource)
{
    /* not supported */
    (void)client;
    (void)resource;
    (void)output_resource;
}

static void
shell_surface_set_title(struct wl_client *client,
                        struct wl_resource *resource, const char *title)
{
    struct shell_surface *shsurf = wl_resource_get_user_data(resource);

    free(shsurf->title);
    if (title != NULL) {
        shsurf->title = strdup(title);
    } else {
        shsurf->title = strdup("");
    }
}

static void
shell_surface_set_class(struct wl_client *client,
                        struct wl_resource *resource, const char *class)
{
    struct shell_surface *shsurf = wl_resource_get_user_data(resource);

    free(shsurf->class);
    shsurf->class = strdup(class);
}

static const struct wl_shell_surface_interface shell_surface_implementation = {
    shell_surface_pong,
    shell_surface_move,
    shell_surface_resize,
    shell_surface_set_toplevel,
    shell_surface_set_transient,
    shell_surface_set_fullscreen,
    shell_surface_set_popup,
    shell_surface_set_maximized,
    shell_surface_set_title,
    shell_surface_set_class
};

static void
shell_weston_surface_destroy(struct wl_listener *listener, void *data)
{
    struct link_weston_surface *lsurf = NULL;

    lsurf = container_of(listener,
                         struct link_weston_surface,
                         destroy_listener);

    wl_list_remove(&lsurf->link);
    free(lsurf);
    lsurf = NULL;
}

static struct shell_surface *
create_shell_surface(struct ivi_shell *shell,
                     struct wl_client *client,
                     uint32_t id_wl_shell,
                     struct weston_surface *surface,
                     const struct weston_shell_client *shell_client)
{
    struct shell_surface *shsurf = NULL;

#if 0
    if (surface->configure) {
        weston_log("surface->configure already set\n");
        return NULL;
    }
#endif

    shsurf = calloc(1, sizeof *shsurf);
    if (shsurf == NULL) {
        weston_log("fails to allocate memory\n");
        return NULL;
    }

#if 0
    surface->configure = shell_surface_configure;
    surface->configure_private = shsurf;
#endif

    shsurf->shell = shell;
    shsurf->surface = surface;
    shsurf->ping_timer = NULL;
    shsurf->title = strdup("");

    /* init link so its safe to always remove it in destroy_shell_surface */
    wl_list_init(&shsurf->link);

    shsurf->client = shell_client;
    shsurf->resource = wl_resource_create(client, &wl_shell_surface_interface,
                                          1, id_wl_shell);

    wl_resource_set_implementation(shsurf->resource,
                                   &shell_surface_implementation,
                                   shsurf, shell_destroy_shell_surface);

    shsurf->surface_destroy_listener.notify = shell_handle_surface_destroy;
    wl_resource_add_destroy_listener(surface->resource,
                                     &shsurf->surface_destroy_listener);

    return shsurf;
}

static void
create_link_weston_surface(struct ivi_shell *shell,
                           struct weston_surface *surface)
{
    struct link_weston_surface *lsurf = NULL;

    lsurf = calloc(1, sizeof *lsurf);
    if (lsurf == NULL) {
        weston_log("fails to allocate memory\n");
        return;
    }

    lsurf->surface = surface;

    wl_list_init(&lsurf->link);
    wl_list_insert(&shell->list_weston_surface, &lsurf->link);

    lsurf->destroy_listener.notify = shell_weston_surface_destroy;
    wl_resource_add_destroy_listener(surface->resource,
                                     &lsurf->destroy_listener);
}

static void
shell_get_shell_surface(struct wl_client   *client,
                        struct wl_resource *resource,
                        uint32_t id_wl_shell,
                        struct wl_resource *surface_resource)
{
    struct ivi_shell *shell = wl_resource_get_user_data(resource);
    struct weston_surface *surface = NULL;
    struct shell_surface  *shsurf  = NULL;
    pid_t pid = 0;
    uid_t uid = 0;
    gid_t gid = 0;

    surface = wl_resource_get_user_data(surface_resource);
    if (get_shell_surface(surface)) {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "get_shell_surface already requested");
        return;
    }

    shsurf = create_shell_surface(shell, client, id_wl_shell, surface, &shell_client);
    if (!shsurf) {
        wl_resource_post_error(surface_resource,
                               WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "surface->configure already set");
        return;
    }

    create_link_weston_surface(shell, surface);

    wl_client_get_credentials(client, &pid, &uid, &gid);

    shsurf->pid = pid;
    wl_list_insert(&shell->list_shell_surface, &shsurf->link);
}

static const struct wl_shell_interface shell_implementation = {
    shell_get_shell_surface
};

static void
bind_shell(struct wl_client *client, void *data,
           uint32_t version, uint32_t id)
{
    struct ivi_shell *shell = data;
    struct wl_resource *resource = NULL;

    resource = wl_resource_create(client, &wl_shell_interface, 1, id);
    wl_resource_set_implementation(resource,
                                   &shell_implementation,
                                   shell, NULL);
}

/* ------------------------------------------------------------------------- */
/* ivi_surface functions                                                     */
/* ------------------------------------------------------------------------- */

static void
ivi_shell_surface_configure(struct weston_surface *, int32_t, int32_t);

static struct ivi_shell_surface *
get_ivi_shell_surface(struct weston_surface *surface)
{
    if (surface->configure == ivi_shell_surface_configure) {
        return surface->configure_private;
    } else {
        return NULL;
    }
}

static void
ivi_shell_surface_configure(struct weston_surface *es,
                        int32_t sx, int32_t sy)
{
    struct ivi_shell_surface *ivisurf = get_ivi_shell_surface(es);
    struct weston_view *view = NULL;
    float from_x = 0.0f;
    float from_y = 0.0f;
    float to_x = 0.0f;
    float to_y = 0.0f;

    if ((es->width == 0) || (es->height == 0) || (ivisurf == NULL)) {
        return;
    }

    view = weston_layout_get_weston_view(ivisurf->layout_surface);
    if (view == NULL) {
        return;
    }

    if (ivisurf->width != es->width || ivisurf->height != es->height) {

        ivisurf->width  = es->width;
        ivisurf->height = es->height;

        weston_view_to_global_float(view, 0, 0, &from_x, &from_y);
        weston_view_to_global_float(view, sx, sy, &to_x, &to_y);
        configure(view,
                  view->geometry.x + to_x - from_x,
                  view->geometry.y + to_y - from_y);

        weston_layout_surfaceConfigure(ivisurf->layout_surface, es->width, es->height);
    }
}

static void
surface_destroy(struct wl_client   *client,
                struct wl_resource *resource)
{
    struct ivi_shell_surface *ivisurf = wl_resource_get_user_data(resource);
    struct ivi_shell *shell = ivisurf->shell;

    weston_layout_surfaceRemove(ivisurf->layout_surface);

    ivisurf->surface->configure = NULL;
    ivisurf->surface->configure_private = NULL;

    if (!wl_list_empty(&ivisurf->link)) {
        wl_list_remove(&ivisurf->link);
    }
    wl_list_init(&shell->list_surface);

    free(ivisurf);
    ivisurf = NULL;
}

static const struct ivi_surface_interface surface_implementation = {
    surface_destroy,
};

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
application_surface_create(struct wl_client   *client,
                           struct wl_resource *resource,
                           uint32_t id_surface,
                           struct wl_resource *surface_resource,
                           uint32_t id)
{
    struct ivi_shell *shell = wl_resource_get_user_data(resource);
    struct weston_layout_surface *layout_surface = NULL;
    struct ivi_shell_surface  *ivisurf = NULL;
    struct weston_surface     *es = NULL;

    es = wl_resource_get_user_data(surface_resource);
    if (es == NULL) {
        weston_log("application_surface_create: invalid surface\n");
        return;
    }

    ivisurf = is_surf_in_surfaces(&shell->list_surface, id_surface);

    layout_surface = weston_layout_surfaceCreate(es, id_surface);
    if (layout_surface == NULL) {
//FIXME
        weston_log("id_surface is already created\n");

        ivisurf->surface = es;
        ivisurf->width  = 0;
        ivisurf->height = 0;

        es->configure = ivi_shell_surface_configure;
        es->configure_private = ivisurf;

        resource = wl_resource_create(client, &ivi_surface_interface, 1, id);
        if (resource == NULL) {
            weston_log("couldn't get surface object");
            return;
        }

        wl_resource_set_implementation(resource,
                                       &surface_implementation,
                                       ivisurf, NULL);
        return;
    }

    ivisurf = calloc(1, sizeof *ivisurf);
    if (ivisurf == NULL) {
        weston_log("fails to allocate memory\n");
        return;
    }

    wl_list_init(&ivisurf->link);
    ivisurf->shell = shell;
    ivisurf->layout_surface = layout_surface;
    ivisurf->surface = es;
    ivisurf->id_surface = id_surface;
    ivisurf->width  = 0;
    ivisurf->height = 0;

    es->configure = ivi_shell_surface_configure;
    es->configure_private = ivisurf;

    wl_list_insert(&shell->list_surface, &ivisurf->link);

    resource = wl_resource_create(client, &ivi_surface_interface, 1, id);
    if (resource == NULL) {
        weston_log("couldn't get surface object");
        return;
    }

    wl_resource_set_implementation(resource,
                                   &surface_implementation,
                                   ivisurf, NULL);
ivi_shell_surface_configure(es, 0, 0);
}

static const struct ivi_application_interface application_implementation = {
    application_surface_create
};

static void
bind_ivi_application(struct wl_client *client,
                void *data, uint32_t version, uint32_t id)
{
    struct ivi_shell *shell = data;
    struct wl_resource *resource = NULL;

    resource = wl_resource_create(client, &ivi_application_interface, 1, id);

    wl_resource_set_implementation(resource,
                                   &application_implementation,
                                   shell, NULL);
}

static void
shell_destroy(struct wl_listener *listener, void *data)
{
    struct ivi_shell *shell = NULL;

    shell = container_of(listener, struct ivi_shell, destroy_listener);

    free(shell);
    shell = NULL;
}

static void
init_ivi_shell(struct weston_compositor *ec, struct ivi_shell *shell)
{
    shell->compositor = ec;

    wl_list_init(&ec->layer_list);
    wl_list_init(&shell->list_surface);
    wl_list_init(&shell->list_weston_surface);
    wl_list_init(&shell->list_shell_surface);
}

/* ------------------------------------------------------------------------- */
/* export functions                                                          */
/* ------------------------------------------------------------------------- */
WL_EXPORT void
ivi_shell_get_shell_surfaces(struct wl_array *surfaces)
{
    struct ivi_shell *shell = get_instance();
    wl_array_init(surfaces);

    struct shell_surface *surface = NULL;
    wl_list_for_each(surface, &shell->list_shell_surface, link) {
        struct shell_surface **add = wl_array_add(surfaces, sizeof(surface));
        *add = surface;
    }
}

WL_EXPORT uint32_t
shell_surface_get_process_id(struct shell_surface *surface)
{
    return surface->pid;
}

WL_EXPORT char*
shell_surface_get_title(struct shell_surface* surface)
{
    return surface->title;
}

WL_EXPORT struct weston_surface *
shell_surface_get_surface(struct shell_surface* surface)
{
    return surface->surface;
}

WL_EXPORT int
module_init(struct weston_compositor *ec,
            int *argc, char *argv[])
{
    struct ivi_shell  *shell = get_instance();
    struct weston_seat *seat = NULL;

    init_ivi_shell(ec, shell);
    weston_layout_initWithCompositor(ec);

    shell->destroy_listener.notify = shell_destroy;
    wl_signal_add(&ec->destroy_signal, &shell->destroy_listener);

    if (wl_global_create(ec->wl_display, &wl_shell_interface, 1,
                         shell, bind_shell) == NULL) {
        return -1;
    }

    if (wl_global_create(ec->wl_display, &ivi_application_interface, 1,
                         shell, bind_ivi_application) == NULL) {
        return -1;
    }

    wl_list_for_each(seat, &ec->seat_list, link) {
        if (seat) {
            shell->seat = seat;
        }
    }

    return 0;
}
