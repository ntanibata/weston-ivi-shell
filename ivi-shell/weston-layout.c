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
#include <cairo.h>

#include "compositor.h"
#include "weston-layout.h"

enum weston_layout_surface_orientation {
    WESTON_LAYOUT_SURFACE_ORIENTATION_0_DEGREES   = 0,
    WESTON_LAYOUT_SURFACE_ORIENTATION_90_DEGREES  = 1,
    WESTON_LAYOUT_SURFACE_ORIENTATION_180_DEGREES = 2,
    WESTON_LAYOUT_SURFACE_ORIENTATION_270_DEGREES = 3,
};

enum weston_layout_surface_pixelformat {
    WESTON_LAYOUT_SURFACE_PIXELFORMAT_R_8       = 0,
    WESTON_LAYOUT_SURFACE_PIXELFORMAT_RGB_888   = 1,
    WESTON_LAYOUT_SURFACE_PIXELFORMAT_RGBA_8888 = 2,
    WESTON_LAYOUT_SURFACE_PIXELFORMAT_RGB_565   = 3,
    WESTON_LAYOUT_SURFACE_PIXELFORMAT_RGBA_5551 = 4,
    WESTON_LAYOUT_SURFACE_PIXELFORMAT_RGBA_6661 = 5,
    WESTON_LAYOUT_SURFACE_PIXELFORMAT_RGBA_4444 = 6,
    WESTON_LAYOUT_SURFACE_PIXELFORMAT_UNKNOWN   = 7,
};

struct link_layer {
    struct weston_layout_layer *layout_layer;
    struct wl_list link;
};

struct link_screen {
    struct weston_layout_screen *layout_screen;
    struct wl_list link;
};

struct link_layerPropertyNotification {
    layerPropertyNotificationFunc callback;
    void *userdata;
    struct wl_list link;
};

struct link_surfacePropertyNotification {
    surfacePropertyNotificationFunc callback;
    void *userdata;
    struct wl_list link;
};

struct link_layerCreateNotification {
    layerCreateNotificationFunc callback;
    void *userdata;
    struct wl_list link;
};

struct link_layerRemoveNotification {
    layerRemoveNotificationFunc callback;
    void *userdata;
    struct wl_list link;
};

struct link_surfaceCreateNotification {
    surfaceCreateNotificationFunc callback;
    void *userdata;
    struct wl_list link;
};

struct link_surfaceRemoveNotification {
    surfaceRemoveNotificationFunc callback;
    void *userdata;
    struct wl_list link;
};

struct link_surfaceConfigureNotification {
    surfaceConfigureNotificationFunc callback;
    void *userdata;
    struct wl_list link;
};

struct weston_layout;

struct weston_layout_surface {
    struct wl_list link;
    struct wl_list list_notification;
    struct wl_list list_layer;
    uint32_t update_count;
    uint32_t id_surface;

    struct weston_layout *layout;
    struct weston_surface *surface;
    struct weston_view *view;

    uint32_t buffer_width;
    uint32_t buffer_height;

    struct wl_listener surface_destroy_listener;
    struct weston_transform surface_rotation;
    struct weston_transform layer_rotation;
    struct weston_transform surface_pos;
    struct weston_transform layer_pos;
    struct weston_transform scaling;
    struct weston_layout_SurfaceProperties prop;
    int32_t pixelformat;
    uint32_t event_mask;

    struct {
        struct weston_layout_SurfaceProperties prop;
        struct wl_list link;
    } pending;

    struct {
        struct wl_list link;
        struct wl_list list_layer;
    } order;
};

struct weston_layout_layer {
    struct wl_list link;
    struct wl_list list_notification;
    struct wl_list list_screen;
    uint32_t id_layer;

    struct weston_layout *layout;
    struct weston_layer el;

    struct weston_layout_LayerProperties prop;
    uint32_t event_mask;

    struct {
        struct weston_layout_LayerProperties prop;
        struct wl_list list_surface;
        struct wl_list link;
    } pending;

    struct {
        struct wl_list list_surface;
        struct wl_list link;
    } order;
};

struct weston_layout_screen {
    struct wl_list link;
    uint32_t id_screen;

    struct weston_layout *layout;
    struct weston_output *output;

    uint32_t event_mask;

    struct {
        struct wl_list list_layer;
        struct wl_list link;
    } pending;

    struct {
        struct wl_list list_layer;
        struct wl_list link;
    } order;
};

struct weston_layout {
    struct weston_compositor *compositor;

    struct wl_list list_surface;
    struct wl_list list_layer;
    struct wl_list list_screen;

    struct {
        struct wl_list list_create;
        struct wl_list list_remove;
    } layer_notification;

    struct {
        struct wl_list list_create;
        struct wl_list list_remove;
        struct wl_list list_configure;
    } surface_notification;
};

struct weston_layout weston_layout = {0};

static void
add_ordersurface_to_layer(struct weston_layout_surface *layout_surface,
                          struct weston_layout_layer *layout_layer)
{
    struct link_layer *link_layer = NULL;

    link_layer = malloc(sizeof *link_layer);
    if (link_layer == NULL) {
        weston_log("fails to allocate memory\n");
        return;
    }

    link_layer->layout_layer = layout_layer;
    wl_list_init(&link_layer->link);
    wl_list_insert(&layout_surface->list_layer, &link_layer->link);
}

static void
remove_ordersurface_from_layer(struct weston_layout_surface *layout_surface)
{
    struct link_layer *link_layer = NULL;
    struct link_layer *next = NULL;

    wl_list_for_each_safe(link_layer, next, &layout_surface->list_layer, link) {
        if (!wl_list_empty(&link_layer->link)) {
            wl_list_remove(&link_layer->link);
        }
        free(link_layer);
        link_layer = NULL;
    }
    wl_list_init(&layout_surface->list_layer);
}

static void
add_orderlayer_to_screen(struct weston_layout_layer *layout_layer,
                         struct weston_layout_screen *layout_screen)
{
    struct link_screen *link_scrn = NULL;

    link_scrn = malloc(sizeof *link_scrn);
    if (link_scrn == NULL) {
        weston_log("fails to allocate memory\n");
        return;
    }

    link_scrn->layout_screen = layout_screen;
    wl_list_init(&link_scrn->link);
    wl_list_insert(&layout_layer->list_screen, &link_scrn->link);
}

static void
remove_orderlayer_from_screen(struct weston_layout_layer *layout_layer)
{
    struct link_screen *link_scrn = NULL;
    struct link_screen *next = NULL;

    wl_list_for_each_safe(link_scrn, next, &layout_layer->list_screen, link) {
        if (!wl_list_empty(&link_scrn->link)) {
            wl_list_remove(&link_scrn->link);
        }
        free(link_scrn);
        link_scrn = NULL;
    }
    wl_list_init(&layout_layer->list_screen);
}

static struct weston_layout_surface *
get_surface(struct wl_list *list_surf, uint32_t id_surface)
{
    struct weston_layout_surface *layout_surface;

    wl_list_for_each(layout_surface, list_surf, link) {
        if (layout_surface->id_surface == id_surface) {
            return layout_surface;
        }
    }

    return NULL;
}

static struct weston_layout_layer *
get_layer(struct wl_list *list_layer, uint32_t id_layer)
{
    struct weston_layout_layer *layout_layer;

    wl_list_for_each(layout_layer, list_layer, link) {
        if (layout_layer->id_layer == id_layer) {
            return layout_layer;
        }
    }

    return NULL;
}

static void
init_layerProperties(struct weston_layout_LayerProperties *prop,
                     int32_t width, int32_t height)
{
    memset(prop, 0, sizeof *prop);
    prop->opacity = wl_fixed_from_double(1.0);
    prop->sourceWidth = width;
    prop->sourceHeight = height;
    prop->destWidth = width;
    prop->destHeight = height;
}

static void
init_surfaceProperties(struct weston_layout_SurfaceProperties *prop)
{
    memset(prop, 0, sizeof *prop);
    prop->opacity = wl_fixed_from_double(1.0);
}

static void
update_opacity(struct weston_layout_layer *layout_layer,
               struct weston_layout_surface *layout_surface)
{
    double layer_alpha = wl_fixed_to_double(layout_layer->prop.opacity);
    double surf_alpha  = wl_fixed_to_double(layout_surface->prop.opacity);

    if ((layout_layer->event_mask & IVI_NOTIFICATION_OPACITY) ||
        (layout_surface->event_mask  & IVI_NOTIFICATION_OPACITY)) {
        if (layout_surface->view == NULL) {
            return;
        }
        layout_surface->view->alpha = layer_alpha * surf_alpha;
    }
}

static void
update_surface_orientation(struct weston_layout_layer *layout_layer,
                           struct weston_layout_surface *layout_surface)
{
    struct weston_view *view = layout_surface->view;
    struct weston_matrix  *matrix = &layout_surface->surface_rotation.matrix;
    float width  = 0.0f;
    float height = 0.0f;
    float v_sin  = 0.0f;
    float v_cos  = 0.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;

    if (view == NULL) {
        return;
    }

    if ((layout_layer->prop.destWidth == 0) ||
        (layout_layer->prop.destHeight == 0)) {
        return;
    }
    width  = (float)layout_layer->prop.destWidth;
    height = (float)layout_layer->prop.destHeight;

    switch (layout_surface->prop.orientation) {
    case WESTON_LAYOUT_SURFACE_ORIENTATION_0_DEGREES:
        v_sin = 0.0f;
        v_cos = 1.0f;
        break;
    case WESTON_LAYOUT_SURFACE_ORIENTATION_90_DEGREES:
        v_sin = 1.0f;
        v_cos = 0.0f;
        sx = width / height;
        sy = height / width;
        break;
    case WESTON_LAYOUT_SURFACE_ORIENTATION_180_DEGREES:
        v_sin = 0.0f;
        v_cos = -1.0f;
        break;
    case WESTON_LAYOUT_SURFACE_ORIENTATION_270_DEGREES:
    default:
        v_sin = -1.0f;
        v_cos = 0.0f;
        sx = width / height;
        sy = height / width;
        break;
    }
    wl_list_remove(&layout_surface->surface_rotation.link);
    weston_view_geometry_dirty(view);

    weston_matrix_init(matrix);
    cx = 0.5f * width;
    cy = 0.5f * height;
    weston_matrix_translate(matrix, -cx, -cy, 0.0f);
    weston_matrix_rotate_xy(matrix, v_cos, v_sin);
    weston_matrix_scale(matrix, sx, sy, 1.0);
    weston_matrix_translate(matrix, cx, cy, 0.0f);
    wl_list_insert(&view->geometry.transformation_list,
                   &layout_surface->surface_rotation.link);

    weston_view_set_transform_parent(view, NULL);
    weston_view_update_transform(view);
}

static void
update_layer_orientation(struct weston_layout_layer *layout_layer,
                         struct weston_layout_surface *layout_surface)
{
    struct weston_surface *es = layout_surface->surface;
    struct weston_view    *view = layout_surface->view;
    struct weston_matrix  *matrix = &layout_surface->layer_rotation.matrix;
    struct weston_output  *output = NULL;
    float width  = 0.0f;
    float height = 0.0f;
    float v_sin  = 0.0f;
    float v_cos  = 0.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;

    if (es == NULL || view == NULL) {
        return;
    }

    output = es->output;
    if (output == NULL) {
        return;
    }
    if ((output->width == 0) || (output->height == 0)) {
        return;
    }
    width = (float)output->width;
    height = (float)output->height;

    switch (layout_layer->prop.orientation) {
    case WESTON_LAYOUT_SURFACE_ORIENTATION_0_DEGREES:
        v_sin = 0.0f;
        v_cos = 1.0f;
        break;
    case WESTON_LAYOUT_SURFACE_ORIENTATION_90_DEGREES:
        v_sin = 1.0f;
        v_cos = 0.0f;
        sx = width / height;
        sy = height / width;
        break;
    case WESTON_LAYOUT_SURFACE_ORIENTATION_180_DEGREES:
        v_sin = 0.0f;
        v_cos = -1.0f;
        break;
    case WESTON_LAYOUT_SURFACE_ORIENTATION_270_DEGREES:
    default:
        v_sin = -1.0f;
        v_cos = 0.0f;
        sx = width / height;
        sy = height / width;
        break;
    }
    wl_list_remove(&layout_surface->layer_rotation.link);
    weston_view_geometry_dirty(view);

    weston_matrix_init(matrix);
    cx = 0.5f * width;
    cy = 0.5f * height;
    weston_matrix_translate(matrix, -cx, -cy, 0.0f);
    weston_matrix_rotate_xy(matrix, v_cos, v_sin);
    weston_matrix_scale(matrix, sx, sy, 1.0);
    weston_matrix_translate(matrix, cx, cy, 0.0f);
    wl_list_insert(&view->geometry.transformation_list,
                   &layout_surface->layer_rotation.link);

    weston_view_set_transform_parent(view, NULL);
    weston_view_update_transform(view);
}

static void
update_surface_position(struct weston_layout_surface *layout_surface)
{
    struct weston_view *view = layout_surface->view;
    float tx  = (float)layout_surface->prop.destX;
    float ty  = (float)layout_surface->prop.destY;
    struct weston_matrix *matrix = &layout_surface->surface_pos.matrix;

    if (view == NULL) {
        return;
    }

    wl_list_remove(&layout_surface->surface_pos.link);

    weston_matrix_init(matrix);
    weston_matrix_translate(matrix, tx, ty, 0.0f);
    wl_list_insert(&view->geometry.transformation_list,
                   &layout_surface->surface_pos.link);

    weston_view_set_transform_parent(view, NULL);
    weston_view_update_transform(view);

#if 0
    /* disable zoom animation */
    weston_zoom_run(es, 0.0, 1.0, NULL, NULL);
#endif

}

static void
update_layer_position(struct weston_layout_layer *layout_layer,
               struct weston_layout_surface *layout_surface)
{
    struct weston_view *view = layout_surface->view;
    struct weston_matrix *matrix = &layout_surface->layer_pos.matrix;
    float tx  = (float)layout_layer->prop.destX;
    float ty  = (float)layout_layer->prop.destY;

    if (view == NULL) {
        return;
    }

    wl_list_remove(&layout_surface->layer_pos.link);

    weston_matrix_init(matrix);
    weston_matrix_translate(matrix, tx, ty, 0.0f);
    wl_list_insert(
        &view->geometry.transformation_list,
        &layout_surface->layer_pos.link);

    weston_view_set_transform_parent(view, NULL);
    weston_view_update_transform(view);
}

static void
update_scale(struct weston_layout_layer *layout_layer,
               struct weston_layout_surface *layout_surface)
{
    struct weston_view *view = layout_surface->view;
    struct weston_matrix *matrix = &layout_surface->scaling.matrix;
    float sx = 0.0f;
    float sy = 0.0f;
    float lw = 0.0f;
    float sw = 0.0f;
    float lh = 0.0f;
    float sh = 0.0f;

    if (view == NULL) {
        return;
    }

    if (layout_surface->prop.sourceWidth == 0 && layout_surface->prop.sourceHeight == 0) {
        layout_surface->prop.sourceWidth  = layout_surface->buffer_width;
        layout_surface->prop.sourceHeight = layout_surface->buffer_height;

        if (layout_surface->prop.destWidth == 0 && layout_surface->prop.destHeight == 0) {
            layout_surface->prop.destWidth  = layout_surface->buffer_width;
            layout_surface->prop.destHeight = layout_surface->buffer_height;
        }
    }

    lw = ((float)layout_layer->prop.destWidth  / layout_layer->prop.sourceWidth );
    sw = ((float)layout_surface->prop.destWidth   / layout_surface->prop.sourceWidth  );
    lh = ((float)layout_layer->prop.destHeight / layout_layer->prop.sourceHeight);
    sh = ((float)layout_surface->prop.destHeight  / layout_surface->prop.sourceHeight );
    sx = sw * lw;
    sy = sh * lh;

    wl_list_remove(&layout_surface->scaling.link);
    weston_matrix_init(matrix);
    weston_matrix_scale(matrix, sx, sy, 1.0f);

    wl_list_insert(&view->geometry.transformation_list,
                   &layout_surface->scaling.link);

    weston_view_set_transform_parent(view, NULL);
    weston_view_update_transform(view);
}

static void
update_prop(struct weston_layout_layer *layout_layer,
            struct weston_layout_surface *layout_surface)
{
    if (layout_layer->event_mask | layout_surface->event_mask) {
        update_opacity(layout_layer, layout_surface);
        update_layer_orientation(layout_layer, layout_surface);
        update_layer_position(layout_layer, layout_surface);
        update_surface_position(layout_surface);
        update_surface_orientation(layout_layer, layout_surface);
        update_scale(layout_layer, layout_surface);

        layout_surface->update_count++;

        if (layout_surface->view != NULL) {
            weston_view_geometry_dirty(layout_surface->view);
        }

        if (layout_surface->surface != NULL) {
            weston_surface_damage(layout_surface->surface);
        }
    }
}

static void
send_surface_prop(struct weston_layout_surface *layout_surface)
{
    struct link_surfacePropertyNotification *notification = NULL;

    wl_list_for_each(notification, &layout_surface->list_notification, link) {
        notification->callback(layout_surface, &layout_surface->prop,
                               layout_surface->event_mask,
                               notification->userdata);
    }

    layout_surface->event_mask = 0;
}

static void
send_layer_prop(struct weston_layout_layer *layout_layer)
{
    struct link_layerPropertyNotification *notification = NULL;

    wl_list_for_each(notification, &layout_layer->list_notification, link) {
        notification->callback(layout_layer, &layout_layer->prop,
                               layout_layer->event_mask,
                               notification->userdata);
    }

    layout_layer->event_mask = 0;
}

static void
commit_changes(struct weston_layout *layout)
{
    struct weston_layout_screen  *layout_screen  = NULL;
    struct weston_layout_layer   *layout_layer = NULL;
    struct weston_layout_surface *layout_surface  = NULL;

    wl_list_for_each(layout_screen, &layout->list_screen, link) {
        wl_list_for_each(layout_layer, &layout_screen->order.list_layer, order.link) {
            wl_list_for_each(layout_surface, &layout_layer->order.list_surface, order.link) {
                update_prop(layout_layer, layout_surface);
            }
        }
    }
}

static void
send_prop(struct weston_layout *layout)
{
    struct weston_layout_layer   *layout_layer = NULL;
    struct weston_layout_surface *layout_surface  = NULL;

    wl_list_for_each(layout_layer, &layout->list_layer, link) {
        send_layer_prop(layout_layer);
    }

    wl_list_for_each(layout_surface, &layout->list_surface, link) {
        send_surface_prop(layout_surface);
    }
}

static int
is_surface_in_layer(struct weston_layout_surface *layout_surface,
                    struct weston_layout_layer *layout_layer)
{
    struct weston_layout_surface *surf = NULL;

    wl_list_for_each(surf, &layout_layer->pending.list_surface, pending.link) {
        if (surf->id_surface == layout_surface->id_surface) {
            return 1;
        }
    }

    return 0;
}

static int
is_layer_in_screen(struct weston_layout_layer *layout_layer,
                    struct weston_layout_screen *layout_screen)
{
    struct weston_layout_layer *layer = NULL;

    wl_list_for_each(layer, &layout_screen->pending.list_layer, pending.link) {
        if (layer->id_layer == layout_layer->id_layer) {
            return 1;
        }
    }

    return 0;
}

static void
commit_list_surface(struct weston_layout *layout)
{
    struct weston_layout_surface *layout_surface = NULL;

    wl_list_for_each(layout_surface, &layout->list_surface, link) {
        layout_surface->prop = layout_surface->pending.prop;
    }
}

static void
commit_list_layer(struct weston_layout *layout)
{
    struct weston_layout_layer   *layout_layer = NULL;
    struct weston_layout_surface *layout_surface  = NULL;
    struct weston_layout_surface *next     = NULL;

    wl_list_for_each(layout_layer, &layout->list_layer, link) {
        layout_layer->prop = layout_layer->pending.prop;

        if (!(layout_layer->event_mask & IVI_NOTIFICATION_ADD)) {
            continue;
        }

        wl_list_for_each_safe(layout_surface, next,
            &layout_layer->order.list_surface, order.link) {
            remove_ordersurface_from_layer(layout_surface);

            if (!wl_list_empty(&layout_surface->order.link)) {
                wl_list_remove(&layout_surface->order.link);
            }

            wl_list_init(&layout_surface->order.link);
        }

        wl_list_init(&layout_layer->order.list_surface);
        wl_list_for_each(layout_surface, &layout_layer->pending.list_surface,
                              pending.link) {
            wl_list_insert(&layout_layer->order.list_surface,
                           &layout_surface->order.link);
            add_ordersurface_to_layer(layout_surface, layout_layer);
        }
    }
}

static void
commit_list_screen(struct weston_layout *layout)
{
    struct weston_compositor  *ec = layout->compositor;
    struct weston_layout_screen  *layout_screen  = NULL;
    struct weston_layout_layer   *layout_layer = NULL;
    struct weston_layout_layer   *next     = NULL;
    struct weston_layout_surface *layout_surface  = NULL;

    wl_list_for_each(layout_screen, &layout->list_screen, link) {
        if (layout_screen->event_mask & IVI_NOTIFICATION_ADD) {
            wl_list_for_each_safe(layout_layer, next,
                     &layout_screen->order.list_layer, order.link) {
                remove_orderlayer_from_screen(layout_layer);

                if (!wl_list_empty(&layout_layer->order.link)) {
                    wl_list_remove(&layout_layer->order.link);
                }

                wl_list_init(&layout_layer->order.link);
            }

            wl_list_init(&layout_screen->order.list_layer);
            wl_list_for_each(layout_layer, &layout_screen->pending.list_layer,
                                  pending.link) {
                wl_list_insert(&layout_screen->order.list_layer,
                               &layout_layer->order.link);
                add_orderlayer_to_screen(layout_layer, layout_screen);
            }
            layout_screen->event_mask = 0;
        }

        /* For rendering */
        wl_list_init(&ec->layer_list);
        wl_list_for_each(layout_layer, &layout_screen->order.list_layer, order.link) {
            if (layout_layer->prop.visibility == 0) {
                continue;
            }

            wl_list_insert(&ec->layer_list, &layout_layer->el.link);
            wl_list_init(&layout_layer->el.view_list);

            wl_list_for_each(layout_surface, &layout_layer->order.list_surface, order.link) {
                if (layout_surface->prop.visibility == 0) {
                    continue;
                }

                if (layout_surface->surface == NULL || layout_surface->view == NULL) {
                    continue;
                }

                wl_list_insert(&layout_layer->el.view_list,
                               &layout_surface->view->layer_link);
                layout_surface->surface->output = layout_screen->output;
            }
        }

        break;
    }
}

static void
westonsurface_destroy_from_layout_surfaceace(struct wl_listener *listener, void *data)
{
    struct weston_layout_surface *layout_surface = NULL;

    layout_surface = container_of(listener, struct weston_layout_surface,
                           surface_destroy_listener);
    layout_surface->surface = NULL;
}

static struct weston_layout *
get_instance(void)
{
    return &weston_layout;
}

static void
create_screen(struct weston_compositor *ec)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_screen *layout_screen = NULL;
    struct weston_output *output = NULL;
    int32_t count = 0;

    wl_list_for_each(output, &ec->output_list, link) {
        layout_screen = calloc(1, sizeof *layout_screen);
        if (layout_screen == NULL) {
            weston_log("fails to allocate memory\n");
            continue;
        }

        wl_list_init(&layout_screen->link);
        layout_screen->layout = layout;

        layout_screen->id_screen = count;
        count++;

        layout_screen->output = output;
        layout_screen->event_mask = 0;

        wl_list_init(&layout_screen->pending.list_layer);
        wl_list_init(&layout_screen->pending.link);

        wl_list_init(&layout_screen->order.list_layer);
        wl_list_init(&layout_screen->order.link);

        wl_list_insert(&layout->list_screen, &layout_screen->link);
    }
}

WL_EXPORT struct weston_view *
weston_layout_get_weston_view(struct weston_layout_surface *surface)
{
    return (surface != NULL) ? surface->view : NULL;
}

WL_EXPORT void
weston_layout_initWithCompositor(struct weston_compositor *ec)
{
    struct weston_layout *layout = get_instance();

    layout->compositor = ec;

    wl_list_init(&layout->list_surface);
    wl_list_init(&layout->list_layer);
    wl_list_init(&layout->list_screen);

    wl_list_init(&layout->layer_notification.list_create);
    wl_list_init(&layout->layer_notification.list_remove);

    wl_list_init(&layout->surface_notification.list_create);
    wl_list_init(&layout->surface_notification.list_remove);
    wl_list_init(&layout->surface_notification.list_configure);

    create_screen(ec);
}

WL_EXPORT int32_t
weston_layout_setNotificationCreateLayer(layerCreateNotificationFunc callback,
                                           void *userdata)
{
    struct weston_layout *layout = get_instance();
    struct link_layerCreateNotification *notification = NULL;

    if (callback == NULL) {
        weston_log("weston_layout_setNotificationCreateLayer: invalid argument\n");
        return -1;
    }

    notification = malloc(sizeof *notification);
    if (notification == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    notification->callback = callback;
    notification->userdata = userdata;
    wl_list_init(&notification->link);
    wl_list_insert(&layout->layer_notification.list_create, &notification->link);

    return 0;
}

WL_EXPORT int32_t
weston_layout_setNotificationRemoveLayer(layerRemoveNotificationFunc callback,
                                           void *userdata)
{
    struct weston_layout *layout = get_instance();
    struct link_layerRemoveNotification *notification = NULL;

    if (callback == NULL) {
        weston_log("weston_layout_setNotificationRemoveLayer: invalid argument\n");
        return -1;
    }

    notification = malloc(sizeof *notification);
    if (notification == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    notification->callback = callback;
    notification->userdata = userdata;
    wl_list_init(&notification->link);
    wl_list_insert(&layout->layer_notification.list_remove, &notification->link);

    return 0;
}

WL_EXPORT int32_t
weston_layout_setNotificationCreateSurface(surfaceCreateNotificationFunc callback,
                                           void *userdata)
{
    struct weston_layout *layout = get_instance();
    struct link_surfaceCreateNotification *notification = NULL;

    if (callback == NULL) {
        weston_log("weston_layout_setNotificationCreateSurface: invalid argument\n");
        return -1;
    }

    notification = malloc(sizeof *notification);
    if (notification == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    notification->callback = callback;
    notification->userdata = userdata;
    wl_list_init(&notification->link);
    wl_list_insert(&layout->surface_notification.list_create, &notification->link);

    return 0;
}

WL_EXPORT int32_t
weston_layout_setNotificationRemoveSurface(surfaceRemoveNotificationFunc callback,
                                           void *userdata)
{
    struct weston_layout *layout = get_instance();
    struct link_surfaceRemoveNotification *notification = NULL;

    if (callback == NULL) {
        weston_log("weston_layout_setNotificationRemoveSurface: invalid argument\n");
        return -1;
    }

    notification = malloc(sizeof *notification);
    if (notification == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    notification->callback = callback;
    notification->userdata = userdata;
    wl_list_init(&notification->link);
    wl_list_insert(&layout->surface_notification.list_remove, &notification->link);

    return 0;
}

WL_EXPORT int32_t
weston_layout_setNotificationConfigureSurface(surfaceConfigureNotificationFunc callback,
                                           void *userdata)
{
    struct weston_layout *layout = get_instance();
    struct link_surfaceConfigureNotification *notification = NULL;

    if (callback == NULL) {
        weston_log("weston_layout_setNotificationConfigureSurface: invalid argument\n");
        return -1;
    }

    notification = malloc(sizeof *notification);
    if (notification == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    notification->callback = callback;
    notification->userdata = userdata;
    wl_list_init(&notification->link);
    wl_list_insert(&layout->surface_notification.list_configure, &notification->link);

    return 0;
}

WL_EXPORT uint32_t
weston_layout_getIdOfSurface(struct weston_layout_surface *layout_surface)
{
    return layout_surface->id_surface;
}

WL_EXPORT uint32_t
weston_layout_getIdOfLayer(struct weston_layout_layer *layout_layer)
{
    return layout_layer->id_layer;
}

WL_EXPORT struct weston_layout_layer *
weston_layout_getLayerFromId(uint32_t id_layer)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_layer *layout_layer = NULL;

    wl_list_for_each(layout_layer, &layout->list_layer, link) {
        if (layout_layer->id_layer == id_layer) {
            return layout_layer;
        }
    }

    return NULL;
}

WL_EXPORT struct weston_layout_surface *
weston_layout_getSurfaceFromId(uint32_t id_surface)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_surface *layout_surface = NULL;

    wl_list_for_each(layout_surface, &layout->list_surface, link) {
        if (layout_surface->id_surface == id_surface) {
            return layout_surface;
        }
    }

    return NULL;
}

WL_EXPORT struct weston_layout_screen *
weston_layout_getScreenFromId(uint32_t id_screen)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_screen *layout_screen = NULL;
    (void)id_screen;

    wl_list_for_each(layout_screen, &layout->list_screen, link) {
//FIXME : select layout_screen from list_screen by id_screen
        return layout_screen;
        break;
    }

    return NULL;
}

WL_EXPORT int32_t
weston_layout_getScreenResolution(struct weston_layout_screen *layout_screen,
                               uint32_t *pWidth, uint32_t *pHeight)
{
    struct weston_output *output = NULL;

    if (pWidth == NULL || pHeight == NULL) {
        weston_log("weston_layout_getScreenResolution: invalid argument\n");
        return -1;
    }

    output   = layout_screen->output;
    *pWidth  = output->current_mode->width;
    *pHeight = output->current_mode->height;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceAddNotification(struct weston_layout_surface *layout_surface,
                                  surfacePropertyNotificationFunc callback,
                                  void *userdata)
{
    struct link_surfacePropertyNotification *notification = NULL;

    if (layout_surface == NULL || callback == NULL) {
        weston_log("weston_layout_surfaceAddNotification: invalid argument\n");
        return -1;
    }

    notification = malloc(sizeof *notification);
    if (notification == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    notification->callback = callback;
    notification->userdata = userdata;
    wl_list_init(&notification->link);
    wl_list_insert(&layout_surface->list_notification, &notification->link);

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceRemoveNotification(struct weston_layout_surface *layout_surface)
{
    struct link_surfacePropertyNotification *notification = NULL;
    struct link_surfacePropertyNotification *next = NULL;

    if (layout_surface == NULL) {
        weston_log("weston_layout_surfaceRemoveNotification: invalid argument\n");
        return -1;
    }

    wl_list_for_each_safe(notification, next,
                          &layout_surface->list_notification, link) {
        if (!wl_list_empty(&notification->link)) {
            wl_list_remove(&notification->link);
        }
        free(notification);
        notification = NULL;
    }
    wl_list_init(&layout_surface->list_notification);

    return 0;
}

WL_EXPORT struct weston_layout_surface*
weston_layout_surfaceCreate(struct weston_surface *wl_surface,
                         uint32_t id_surface)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_surface *layout_surface = NULL;
    struct link_surfaceCreateNotification *notification = NULL;

    if (wl_surface == NULL) {
        weston_log("weston_layout_surfaceCreate: invalid argument\n");
        return NULL;
    }

    layout_surface = get_surface(&layout->list_surface, id_surface);
    if (layout_surface != NULL) {
//FIXME
        weston_log("id_surface is already created\n");
        layout_surface->surface = wl_surface;
        if (wl_surface != NULL) {
            layout_surface->surface_destroy_listener.notify =
                westonsurface_destroy_from_layout_surfaceace;
            wl_resource_add_destroy_listener(wl_surface->resource,
                          &layout_surface->surface_destroy_listener);

        }

        if (layout_surface->view != NULL) {
            weston_view_destroy(layout_surface->view);
        }
        layout_surface->view = NULL;

        if (wl_surface != NULL) {
            layout_surface->view = weston_view_create(wl_surface);
            if (layout_surface->view == NULL) {
                weston_log("fails to allocate memory\n");
            }
        }

        layout_surface->buffer_width  = 0;
        layout_surface->buffer_height = 0;

        wl_list_for_each(notification,
                &layout->surface_notification.list_create, link) {
            if (notification->callback != NULL) {
                notification->callback(layout_surface, notification->userdata);
            }
        }

        return NULL;
    }

    layout_surface = calloc(1, sizeof *layout_surface);
    if (layout_surface == NULL) {
        weston_log("fails to allocate memory\n");
        return NULL;
    }

    wl_list_init(&layout_surface->link);
    wl_list_init(&layout_surface->list_notification);
    wl_list_init(&layout_surface->list_layer);
    layout_surface->id_surface = id_surface;
    layout_surface->layout = layout;

    layout_surface->surface = wl_surface;
    if (wl_surface != NULL) {
        layout_surface->surface_destroy_listener.notify =
            westonsurface_destroy_from_layout_surfaceace;
        wl_resource_add_destroy_listener(wl_surface->resource,
                      &layout_surface->surface_destroy_listener);

        layout_surface->view = weston_view_create(wl_surface);
        if (layout_surface->view == NULL) {
            weston_log("fails to allocate memory\n");
        }
    }

    layout_surface->buffer_width  = 0;
    layout_surface->buffer_height = 0;

    wl_list_init(&layout_surface->surface_rotation.link);
    wl_list_init(&layout_surface->layer_rotation.link);
    wl_list_init(&layout_surface->surface_pos.link);
    wl_list_init(&layout_surface->layer_pos.link);
    wl_list_init(&layout_surface->scaling.link);

    init_surfaceProperties(&layout_surface->prop);
    layout_surface->pixelformat = WESTON_LAYOUT_SURFACE_PIXELFORMAT_RGBA_8888;
    layout_surface->event_mask = 0;

    layout_surface->pending.prop = layout_surface->prop;
    wl_list_init(&layout_surface->pending.link);

    wl_list_init(&layout_surface->order.link);
    wl_list_init(&layout_surface->order.list_layer);

    wl_list_insert(&layout->list_surface, &layout_surface->link);

    wl_list_for_each(notification,
            &layout->surface_notification.list_create, link) {
        if (notification->callback != NULL) {
            notification->callback(layout_surface, notification->userdata);
        }
    }

    return layout_surface;
}

WL_EXPORT void
weston_layout_surfaceConfigure(struct weston_layout_surface *layout_surface,
                               uint32_t width, uint32_t height)
{
    struct weston_layout *layout = get_instance();
    struct link_surfaceCreateNotification *notification = NULL;

    layout_surface->buffer_width  = width;
    layout_surface->buffer_height = height;

    wl_list_for_each(notification,
            &layout->surface_notification.list_configure, link) {
        if (notification->callback != NULL) {
            notification->callback(layout_surface, notification->userdata);
        }
    }
}

WL_EXPORT int32_t
weston_layout_surfaceRemove(struct weston_layout_surface *layout_surface)
{
    struct weston_layout *layout = get_instance();
    struct link_surfaceRemoveNotification *notification = NULL;

    if (layout_surface == NULL) {
        weston_log("weston_layout_surfaceRemove: invalid argument\n");
        return -1;
    }

    if (!wl_list_empty(&layout_surface->pending.link)) {
        wl_list_remove(&layout_surface->pending.link);
    }
    if (!wl_list_empty(&layout_surface->order.link)) {
        wl_list_remove(&layout_surface->order.link);
    }
    if (!wl_list_empty(&layout_surface->link)) {
        wl_list_remove(&layout_surface->link);
    }
    remove_ordersurface_from_layer(layout_surface);

    wl_list_for_each(notification,
            &layout->surface_notification.list_remove, link) {
        if (notification->callback != NULL) {
            notification->callback(layout_surface, notification->userdata);
        }
    }

    if (layout_surface->view != NULL) {
        weston_view_destroy(layout_surface->view);
    }

    free(layout_surface);
    layout_surface = NULL;

    return 0;
}

WL_EXPORT int32_t
weston_layout_UpdateInputEventAcceptanceOn(struct weston_layout_surface *layout_surface,
                                        uint32_t devices, uint32_t acceptance)
{
    /* not supported */
    (void)layout_surface;
    (void)devices;
    (void)acceptance;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceInitialize(struct weston_layout_surface **pSurfaceId)
{
    /* not supported */
    (void)pSurfaceId;
    return 0;
}

WL_EXPORT int32_t
weston_layout_getPropertiesOfLayer(struct weston_layout_layer *layout_layer,
                    struct weston_layout_LayerProperties *pLayerProperties)
{
    if (layout_layer == NULL || pLayerProperties == NULL) {
        weston_log("weston_layout_getPropertiesOfLayer: invalid argument\n");
        return -1;
    }

    *pLayerProperties = layout_layer->prop;

    return 0;
}

WL_EXPORT int32_t
weston_layout_getNumberOfHardwareLayers(uint32_t id_screen,
                              uint32_t *pNumberOfHardwareLayers)
{
    /* not supported */
    (void)id_screen;
    (void)pNumberOfHardwareLayers;

    return 0;
}

WL_EXPORT int32_t
weston_layout_getScreens(uint32_t *pLength, weston_layout_screen_ptr **ppArray)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_screen *layout_screen = NULL;
    uint32_t length = 0;
    uint32_t n = 0;

    if (pLength == NULL || ppArray == NULL) {
        weston_log("weston_layout_getScreens: invalid argument\n");
        return -1;
    }

    length = wl_list_length(&layout->list_screen);

    if (length != 0){
        /* the Array must be free by module which called this function */
        *ppArray = calloc(length, sizeof(weston_layout_screen_ptr));
        if (*ppArray == NULL) {
            weston_log("fails to allocate memory\n");
            return -1;
        }

        wl_list_for_each(layout_screen, &layout->list_screen, link) {
            (*ppArray)[n++] = layout_screen;
        }
    }

    *pLength = length;

    return 0;
}

WL_EXPORT int32_t
weston_layout_getScreensUnderLayer(struct weston_layout_layer *layout_layer,
                                   uint32_t *pLength,
                                   weston_layout_screen_ptr **ppArray)
{
    struct link_screen *link_scrn = NULL;
    uint32_t length = 0;
    uint32_t n = 0;

    if (layout_layer == NULL || pLength == NULL || ppArray == NULL) {
        weston_log("weston_layout_getScreensUnderLayer: invalid argument\n");
        return -1;
    }

    length = wl_list_length(&layout_layer->list_screen);

    if (length != 0){
        /* the Array must be free by module which called this function */
        *ppArray = calloc(length, sizeof(weston_layout_screen_ptr));
        if (*ppArray == NULL) {
            weston_log("fails to allocate memory\n");
            return -1;
        }

        wl_list_for_each(link_scrn, &layout_layer->list_screen, link) {
            (*ppArray)[n++] = link_scrn->layout_screen;
        }
    }

    *pLength = length;

    return 0;
}

WL_EXPORT int32_t
weston_layout_getLayers(uint32_t *pLength, weston_layout_layer_ptr **ppArray)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_layer *layout_layer = NULL;
    uint32_t length = 0;
    uint32_t n = 0;

    if (pLength == NULL || ppArray == NULL) {
        weston_log("weston_layout_getLayers: invalid argument\n");
        return -1;
    }

    length = wl_list_length(&layout->list_layer);

    if (length != 0){
        /* the Array must be free by module which called this function */
        *ppArray = calloc(length, sizeof(weston_layout_layer_ptr));
        if (*ppArray == NULL) {
            weston_log("fails to allocate memory\n");
            return -1;
        }

        wl_list_for_each(layout_layer, &layout->list_layer, link) {
            (*ppArray)[n++] = layout_layer;
        }
    }

    *pLength = length;

    return 0;
}

WL_EXPORT int32_t
weston_layout_getLayersOnScreen(struct weston_layout_screen *layout_screen,
                                uint32_t *pLength,
                                weston_layout_layer_ptr **ppArray)
{
    struct weston_layout_layer *layout_layer = NULL;
    uint32_t length = 0;
    uint32_t n = 0;

    if (layout_screen == NULL || pLength == NULL || ppArray == NULL) {
        weston_log("weston_layout_getLayersOnScreen: invalid argument\n");
        return -1;
    }

    length = wl_list_length(&layout_screen->order.list_layer);

    if (length != 0){
        /* the Array must be free by module which called this function */
        *ppArray = calloc(length, sizeof(weston_layout_layer_ptr));
        if (*ppArray == NULL) {
            weston_log("fails to allocate memory\n");
            return -1;
        }

        wl_list_for_each(layout_layer, &layout_screen->order.list_layer, link) {
            (*ppArray)[n++] = layout_layer;
        }
    }

    *pLength = length;

    return 0;
}

WL_EXPORT int32_t
weston_layout_getLayersUnderSurface(struct weston_layout_surface *layout_surface,
                                    uint32_t *pLength,
                                    weston_layout_layer_ptr **ppArray)
{
    struct link_layer *link_layer = NULL;
    uint32_t length = 0;
    uint32_t n = 0;

    if (layout_surface == NULL || pLength == NULL || ppArray == NULL) {
        weston_log("weston_layout_getLayers: invalid argument\n");
        return -1;
    }

    length = wl_list_length(&layout_surface->list_layer);

    if (length != 0){
        /* the Array must be free by module which called this function */
        *ppArray = calloc(length, sizeof(weston_layout_layer_ptr));
        if (*ppArray == NULL) {
            weston_log("fails to allocate memory\n");
            return -1;
        }

        wl_list_for_each(link_layer, &layout_surface->list_layer, link) {
            (*ppArray)[n++] = link_layer->layout_layer;
        }
    }

    *pLength = length;

    return 0;
}

WL_EXPORT int32_t
weston_layout_getSurfaces(uint32_t *pLength, weston_layout_surface_ptr **ppArray)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_surface *layout_surface = NULL;
    uint32_t length = 0;
    uint32_t n = 0;

    if (pLength == NULL || ppArray == NULL) {
        weston_log("weston_layout_getSurfaces: invalid argument\n");
        return -1;
    }

    length = wl_list_length(&layout->list_surface);

    if (length != 0){
        /* the Array must be free by module which called this function */
        *ppArray = calloc(length, sizeof(weston_layout_surface_ptr));
        if (*ppArray == NULL) {
            weston_log("fails to allocate memory\n");
            return -1;
        }

        wl_list_for_each(layout_surface, &layout->list_surface, link) {
            (*ppArray)[n++] = layout_surface;
        }
    }

    *pLength = length;

    return 0;
}

WL_EXPORT int32_t
weston_layout_getSurfacesOnLayer(struct weston_layout_layer *layout_layer,
                                 uint32_t *pLength,
                                 weston_layout_surface_ptr **ppArray)
{
    struct weston_layout_surface *layout_surface = NULL;
    uint32_t length = 0;
    uint32_t n = 0;

    if (layout_layer == NULL || pLength == NULL || ppArray == NULL) {
        weston_log("weston_layout_getSurfaceIDsOnLayer: invalid argument\n");
        return -1;
    }

    length = wl_list_length(&layout_layer->order.list_surface);

    if (length != 0) {
        /* the Array must be free by module which called this function */
        *ppArray = calloc(length, sizeof(weston_layout_surface_ptr));
        if (*ppArray == NULL) {
            weston_log("fails to allocate memory\n");
            return -1;
        }

        wl_list_for_each(layout_surface, &layout_layer->order.list_surface, link) {
            (*ppArray)[n++] = layout_surface;
        }
    }

    *pLength = length;

    return 0;
}

WL_EXPORT struct weston_layout_layer *
weston_layout_layerCreateWithDimension(uint32_t id_layer,
                                       uint32_t width, uint32_t height)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_layer *layout_layer = NULL;
    struct link_layerCreateNotification *notification = NULL;

    layout_layer = get_layer(&layout->list_layer, id_layer);
    if (layout_layer != NULL) {
        weston_log("id_layer is already created\n");
        return layout_layer;
    }

    layout_layer = calloc(1, sizeof *layout_layer);
    if (layout_layer == NULL) {
        weston_log("fails to allocate memory\n");
        return NULL;
    }

    wl_list_init(&layout_layer->link);
    wl_list_init(&layout_layer->list_notification);
    wl_list_init(&layout_layer->list_screen);
    layout_layer->layout = layout;
    layout_layer->id_layer = id_layer;

    init_layerProperties(&layout_layer->prop, width, height);
    layout_layer->event_mask = 0;

    wl_list_init(&layout_layer->pending.list_surface);
    wl_list_init(&layout_layer->pending.link);
    layout_layer->pending.prop = layout_layer->prop;

    wl_list_init(&layout_layer->order.list_surface);
    wl_list_init(&layout_layer->order.link);

    wl_list_insert(&layout->list_layer, &layout_layer->link);

    wl_list_for_each(notification,
            &layout->layer_notification.list_create, link) {
        if (notification->callback != NULL) {
            notification->callback(layout_layer, notification->userdata);
        }
    }

    return layout_layer;
}

WL_EXPORT int32_t
weston_layout_layerRemove(struct weston_layout_layer *layout_layer)
{
    struct weston_layout *layout = get_instance();
    struct link_layerRemoveNotification *notification = NULL;

    if (layout_layer == NULL) {
        weston_log("weston_layout_layerRemove: invalid argument\n");
        return -1;
    }

    wl_list_for_each(notification,
            &layout->layer_notification.list_remove, link) {
        if (notification->callback != NULL) {
            notification->callback(layout_layer, notification->userdata);
        }
    }

    if (!wl_list_empty(&layout_layer->pending.link)) {
        wl_list_remove(&layout_layer->pending.link);
    }
    if (!wl_list_empty(&layout_layer->order.link)) {
        wl_list_remove(&layout_layer->order.link);
    }
    if (!wl_list_empty(&layout_layer->link)) {
        wl_list_remove(&layout_layer->link);
    }
    remove_orderlayer_from_screen(layout_layer);

    free(layout_layer);
    layout_layer = NULL;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerGetType(struct weston_layout_layer *layout_layer,
                        uint32_t *pLayerType)
{
    /* not supported */
    (void)layout_layer;
    (void)pLayerType;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetVisibility(struct weston_layout_layer *layout_layer,
                              uint32_t newVisibility)
{
    struct weston_layout_LayerProperties *prop = NULL;

    if (layout_layer == NULL) {
        weston_log("weston_layout_layerSetVisibility: invalid argument\n");
        return -1;
    }

    prop = &layout_layer->pending.prop;
    prop->visibility = newVisibility;

    layout_layer->event_mask |= IVI_NOTIFICATION_VISIBILITY;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerGetVisibility(struct weston_layout_layer *layout_layer, uint32_t *pVisibility)
{
    if (layout_layer == NULL || pVisibility == NULL) {
        weston_log("weston_layout_layerGetVisibility: invalid argument\n");
        return -1;
    }

    *pVisibility = layout_layer->prop.visibility;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetOpacity(struct weston_layout_layer *layout_layer,
                           float opacity)
{
    struct weston_layout_LayerProperties *prop = NULL;

    if (layout_layer == NULL) {
        weston_log("weston_layout_layerSetOpacity: invalid argument\n");
        return -1;
    }

    prop = &layout_layer->pending.prop;
    prop->opacity = opacity;

    layout_layer->event_mask |= IVI_NOTIFICATION_OPACITY;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerGetOpacity(struct weston_layout_layer *layout_layer,
                           float *pOpacity)
{
    if (layout_layer == NULL || pOpacity == NULL) {
        weston_log("weston_layout_layerGetOpacity: invalid argument\n");
        return -1;
    }

    *pOpacity = layout_layer->prop.opacity;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetSourceRectangle(struct weston_layout_layer *layout_layer,
                            uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height)
{
    struct weston_layout_LayerProperties *prop = NULL;

    if (layout_layer == NULL) {
        weston_log("weston_layout_layerSetSourceRectangle: invalid argument\n");
        return -1;
    }

    prop = &layout_layer->pending.prop;
    prop->sourceX = x;
    prop->sourceY = y;
    prop->sourceWidth = width;
    prop->sourceHeight = height;

    layout_layer->event_mask |= IVI_NOTIFICATION_SOURCE_RECT;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetDestinationRectangle(struct weston_layout_layer *layout_layer,
                                 int32_t x, int32_t y,
                                 uint32_t width, uint32_t height)
{
    struct weston_layout_LayerProperties *prop = NULL;

    if (layout_layer == NULL) {
        weston_log("weston_layout_layerSetDestinationRectangle: invalid argument\n");
        return -1;
    }

    prop = &layout_layer->pending.prop;
    prop->destX = x;
    prop->destY = y;
    prop->destWidth = width;
    prop->destHeight = height;

    layout_layer->event_mask |= IVI_NOTIFICATION_DEST_RECT;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerGetDimension(struct weston_layout_layer *layout_layer,
                             uint32_t *pDimension)
{
    if (layout_layer == NULL || &pDimension[0] == NULL || &pDimension[1] == NULL) {
        weston_log("weston_layout_layerGetDimension: invalid argument\n");
        return -1;
    }

    pDimension[0] = layout_layer->prop.destX;
    pDimension[1] = layout_layer->prop.destY;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetDimension(struct weston_layout_layer *layout_layer,
                             uint32_t *pDimension)
{
    struct weston_layout_LayerProperties *prop = NULL;

    if (layout_layer == NULL || &pDimension[0] == NULL || &pDimension[1] == NULL) {
        weston_log("weston_layout_layerSetDimension: invalid argument\n");
        return -1;
    }

    prop = &layout_layer->pending.prop;

    prop->destWidth  = pDimension[0];
    prop->destHeight = pDimension[1];

    layout_layer->event_mask |= IVI_NOTIFICATION_DIMENSION;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerGetPosition(struct weston_layout_layer *layout_layer, int32_t *pPosition)
{
    if (layout_layer == NULL || pPosition == NULL) {
        weston_log("weston_layout_layerGetPosition: invalid argument\n");
        return -1;
    }

    pPosition[0] = layout_layer->prop.destX;
    pPosition[1] = layout_layer->prop.destY;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetPosition(struct weston_layout_layer *layout_layer, int32_t *pPosition)
{
    struct weston_layout_LayerProperties *prop = NULL;

    if (layout_layer == NULL || pPosition == NULL) {
        weston_log("weston_layout_layerSetPosition: invalid argument\n");
        return -1;
    }

    prop = &layout_layer->pending.prop;
    prop->destX = pPosition[0];
    prop->destY = pPosition[1];

    layout_layer->event_mask |= IVI_NOTIFICATION_POSITION;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetOrientation(struct weston_layout_layer *layout_layer,
                               uint32_t orientation)
{
    struct weston_layout_LayerProperties *prop = NULL;

    if (layout_layer == NULL) {
        weston_log("weston_layout_layerSetOrientation: invalid argument\n");
        return -1;
    }

    prop = &layout_layer->pending.prop;
    prop->orientation = orientation;

    layout_layer->event_mask |= IVI_NOTIFICATION_ORIENTATION;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerGetOrientation(struct weston_layout_layer *layout_layer,
                               uint32_t *pOrientation)
{
    if (layout_layer == NULL || pOrientation == NULL) {
        weston_log("weston_layout_layerGetOrientation: invalid argument\n");
        return -1;
    }

    *pOrientation = layout_layer->prop.orientation;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetChromaKey(struct weston_layout_layer *layout_layer, uint32_t* pColor)
{
    /* not supported */
    (void)layout_layer;
    (void)pColor;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerSetRenderOrder(struct weston_layout_layer *layout_layer,
                        struct weston_layout_surface **pSurface,
                        uint32_t number)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_surface *layout_surface = NULL;
    uint32_t *id_surface = NULL;
    uint32_t i = 0;

    if (layout_layer == NULL) {
        weston_log("weston_layout_layerSetRenderOrder: invalid argument\n");
        return -1;
    }

    wl_list_init(&layout_layer->pending.list_surface);

    if (pSurface == NULL) {
        return 0;
    }

    for (i = 0; i < number; i++) {
        id_surface = &pSurface[i]->id_surface;

        wl_list_for_each(layout_surface, &layout->list_surface, link) {
            if (*id_surface != layout_surface->id_surface) {
                continue;
            }

            if (!wl_list_empty(&layout_surface->pending.link)) {
                wl_list_remove(&layout_surface->pending.link);
            }
            wl_list_init(&layout_surface->pending.link);
            wl_list_insert(&layout_layer->pending.list_surface,
                           &layout_surface->pending.link);
            break;
        }
    }

    layout_layer->event_mask |= IVI_NOTIFICATION_ADD;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerGetCapabilities(struct weston_layout_layer *layout_layer,
                                uint32_t *pCapabilities)
{
    /* not supported */
    (void)layout_layer;
    (void)pCapabilities;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerTypeGetCapabilities(uint32_t layerType,
                                    uint32_t *pCapabilities)
{
    /* not supported */
    (void)layerType;
    (void)pCapabilities;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceSetVisibility(struct weston_layout_surface *layout_surface,
                                uint32_t newVisibility)
{
    struct weston_layout_SurfaceProperties *prop = NULL;

    if (layout_surface == NULL) {
        weston_log("weston_layout_surfaceSetVisibility: invalid argument\n");
        return -1;
    }

    prop = &layout_surface->pending.prop;
    prop->visibility = newVisibility;

    layout_surface->event_mask |= IVI_NOTIFICATION_VISIBILITY;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceGetVisibility(struct weston_layout_surface *layout_surface,
                                uint32_t *pVisibility)
{
    if (layout_surface == NULL || pVisibility == NULL) {
        weston_log("weston_layout_surfaceGetVisibility: invalid argument\n");
        return -1;
    }

    *pVisibility = layout_surface->prop.visibility;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceSetOpacity(struct weston_layout_surface *layout_surface,
                             float opacity)
{
    struct weston_layout_SurfaceProperties *prop = NULL;

    if (layout_surface == NULL) {
        weston_log("weston_layout_surfaceSetOpacity: invalid argument\n");
        return -1;
    }

    prop = &layout_surface->pending.prop;
    prop->opacity = opacity;

    layout_surface->event_mask |= IVI_NOTIFICATION_OPACITY;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceGetOpacity(struct weston_layout_surface *layout_surface,
                             float *pOpacity)
{
    if (layout_surface == NULL || pOpacity == NULL) {
        weston_log("weston_layout_surfaceGetOpacity: invalid argument\n");
        return -1;
    }

    *pOpacity = layout_surface->prop.opacity;

    return 0;
}

WL_EXPORT int32_t
weston_layout_SetKeyboardFocusOn(struct weston_layout_surface *layout_surface)
{
    /* not supported */
    (void)layout_surface;

    return 0;
}

WL_EXPORT int32_t
weston_layout_GetKeyboardFocusSurfaceId(struct weston_layout_surface **pSurfaceId)
{
    /* not supported */
    (void)pSurfaceId;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceSetDestinationRectangle(struct weston_layout_surface *layout_surface,
                                          int32_t x, int32_t y,
                                          uint32_t width, uint32_t height)
{
    struct weston_layout_SurfaceProperties *prop = NULL;

    if (layout_surface == NULL) {
        weston_log("weston_layout_surfaceSetDestinationRectangle: invalid argument\n");
        return -1;
    }

    prop = &layout_surface->pending.prop;
    prop->destX = x;
    prop->destY = y;
    prop->destWidth = width;
    prop->destHeight = height;

    layout_surface->event_mask |= IVI_NOTIFICATION_DEST_RECT;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceSetDimension(struct weston_layout_surface *layout_surface, uint32_t *pDimension)
{
    struct weston_layout_SurfaceProperties *prop = NULL;

    if (layout_surface == NULL || &pDimension[0] == NULL || &pDimension[1] == NULL) {
        weston_log("weston_layout_surfaceSetDimension: invalid argument\n");
        return -1;
    }

    prop = &layout_surface->pending.prop;
    prop->destWidth  = pDimension[0];
    prop->destHeight = pDimension[1];

    layout_surface->event_mask |= IVI_NOTIFICATION_DIMENSION;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceGetDimension(struct weston_layout_surface *layout_surface,
                               uint32_t *pDimension)
{
    if (layout_surface == NULL || &pDimension[0] == NULL || &pDimension[1] == NULL) {
        weston_log("weston_layout_surfaceGetDimension: invalid argument\n");
        return -1;
    }

    pDimension[0] = layout_surface->prop.destWidth;
    pDimension[1] = layout_surface->prop.destHeight;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceSetPosition(struct weston_layout_surface *layout_surface,
                              int32_t *pPosition)
{
    struct weston_layout_SurfaceProperties *prop = NULL;

    if (layout_surface == NULL || pPosition == NULL) {
        weston_log("weston_layout_surfaceSetPosition: invalid argument\n");
        return -1;
    }

    prop = &layout_surface->pending.prop;
    prop->destX = pPosition[0];
    prop->destY = pPosition[1];

    layout_surface->event_mask |= IVI_NOTIFICATION_POSITION;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceGetPosition(struct weston_layout_surface *layout_surface,
                              int32_t *pPosition)
{
    if (layout_surface == NULL || pPosition == NULL) {
        weston_log("weston_layout_surfaceGetPosition: invalid argument\n");
        return -1;
    }

    pPosition[0] = layout_surface->prop.destX;
    pPosition[1] = layout_surface->prop.destY;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceSetOrientation(struct weston_layout_surface *layout_surface,
                                 uint32_t orientation)
{
    struct weston_layout_SurfaceProperties *prop = NULL;

    if (layout_surface == NULL) {
        weston_log("weston_layout_surfaceSetOrientation: invalid argument\n");
        return -1;
    }

    prop = &layout_surface->pending.prop;
    prop->orientation = orientation;

    layout_surface->event_mask |= IVI_NOTIFICATION_ORIENTATION;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceGetOrientation(struct weston_layout_surface *layout_surface,
                                 uint32_t *pOrientation)
{
    if (layout_surface == NULL || pOrientation == NULL) {
        weston_log("weston_layout_surfaceGetOrientation: invalid argument\n");
        return -1;
    }

    *pOrientation = layout_surface->prop.orientation;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceGetPixelformat(struct weston_layout_layer *layout_surface, uint32_t *pPixelformat)
{
    /* not supported */
    (void)layout_surface;
    (void)pPixelformat;

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceSetChromaKey(struct weston_layout_surface *layout_surface, uint32_t* pColor)
{
    /* not supported */
    (void)layout_surface;
    (void)pColor;

    return 0;
}

WL_EXPORT int32_t
weston_layout_screenAddLayer(struct weston_layout_screen *layout_screen,
                          struct weston_layout_layer *addlayer)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_layer *layout_layer = NULL;
    struct weston_layout_layer *next = NULL;
    int is_layer_in_scrn = 0;

    if (layout_screen == NULL || addlayer == NULL) {
        weston_log("weston_layout_screenAddLayer: invalid argument\n");
        return -1;
    }

    is_layer_in_scrn = is_layer_in_screen(addlayer, layout_screen);
    if (is_layer_in_scrn == 1) {
        weston_log("weston_layout_screenAddLayer: addlayer is allready available\n");
        return 0;
    }

    wl_list_for_each_safe(layout_layer, next, &layout->list_layer, link) {
        if (layout_layer->id_layer == addlayer->id_layer) {
            if (!wl_list_empty(&layout_layer->pending.link)) {
                wl_list_remove(&layout_layer->pending.link);
            }
            wl_list_init(&layout_layer->pending.link);
            wl_list_insert(&layout_screen->pending.list_layer,
                           &layout_layer->pending.link);
            break;
        }
    }

    layout_screen->event_mask |= IVI_NOTIFICATION_ADD;

    return 0;
}

WL_EXPORT int32_t
weston_layout_screenSetRenderOrder(struct weston_layout_screen *layout_screen,
                                struct weston_layout_layer **pLayer,
                                const uint32_t number)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_layer *layout_layer = NULL;
    uint32_t *id_layer = NULL;
    uint32_t i = 0;

    if (layout_screen == NULL) {
        weston_log("weston_layout_screenSetRenderOrder: invalid argument\n");
        return -1;
    }

    wl_list_init(&layout_screen->pending.list_layer);

    if (pLayer == NULL) {
        return 0;
    }

    for (i = 0; i < number; i++) {
        id_layer = &pLayer[i]->id_layer;
        wl_list_for_each(layout_layer, &layout->list_layer, link) {
            if (*id_layer == layout_layer->id_layer) {
                continue;
            }

            if (!wl_list_empty(&layout_layer->pending.link)) {
                wl_list_remove(&layout_layer->pending.link);
            }
            wl_list_init(&layout_layer->pending.link);
            wl_list_insert(&layout_screen->pending.list_layer,
                           &layout_layer->pending.link);
            break;
        }
    }

    layout_screen->event_mask |= IVI_NOTIFICATION_ADD;

    return 0;
}

WL_EXPORT int32_t
weston_layout_takeScreenshot(struct weston_layout_screen *layout_screen,
                          const char *filename)
{
    struct weston_output *output = NULL;
    cairo_surface_t *cairo_surf = NULL;
    int32_t i = 0;
    int32_t width  = 0;
    int32_t height = 0;
    int32_t stride = 0;
    uint8_t *readpixs = NULL;
    uint8_t *writepixs = NULL;
    uint8_t *d = NULL;
    uint8_t *s = NULL;

    if (layout_screen == NULL || filename == NULL) {
        weston_log("weston_layout_takeScreenshot: invalid argument\n");
        return -1;
    }

    output = layout_screen->output;
    output->disable_planes--;

    width = output->current_mode->width;
    height = output->current_mode->height;
    stride = width * (PIXMAN_FORMAT_BPP(output->compositor->read_format) / 8);

    readpixs = malloc(stride * height);
    if (readpixs == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }
    writepixs = malloc(stride * height);
    if (writepixs == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    output->compositor->renderer->read_pixels(output,
                             output->compositor->read_format, readpixs,
                             0, 0, width, height);

    s = readpixs;
    d = writepixs + stride * (height - 1);

    for (i = 0; i < height; i++) {
        memcpy(d, s, stride);
        d -= stride;
        s += stride;
    }

    cairo_surf = cairo_image_surface_create_for_data(writepixs,
                                                  CAIRO_FORMAT_ARGB32,
                                                  width, height, stride);
    cairo_surface_write_to_png(cairo_surf, filename);
    cairo_surface_destroy(cairo_surf);
    free(writepixs);
    free(readpixs);
    writepixs = NULL;
    readpixs = NULL;

    return 0;
}

WL_EXPORT int32_t
weston_layout_takeLayerScreenshot(const char *filename, struct weston_layout_layer *layout_layer)
{
    /* not supported */
    (void)filename;
    (void)layout_layer;

    return 0;
}

WL_EXPORT int32_t
weston_layout_takeSurfaceScreenshot(const char *filename,
                                 struct weston_layout_surface *layout_surface)
{
    struct weston_layout *layout = get_instance();
    struct weston_compositor *ec = layout->compositor;
    cairo_surface_t *cairo_surf;
    int32_t width;
    int32_t height;
    int32_t stride;
    uint8_t *pixels;

    if (filename == NULL || layout_surface == NULL) {
        weston_log("weston_layout_takeSurfaceScreenshot: invalid argument\n");
        return -1;
    }

    width  = layout_surface->prop.destWidth;
    height = layout_surface->prop.destHeight;
    stride = width * (PIXMAN_FORMAT_BPP(ec->read_format) / 8);

    pixels = malloc(stride * height);
    if (pixels == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    ec->renderer->read_surface_pixels(layout_surface->surface,
                             ec->read_format, pixels,
                             0, 0, width, height);

    cairo_surf = cairo_image_surface_create_for_data(pixels,
                                                  CAIRO_FORMAT_ARGB32,
                                                  width, height, stride);
    cairo_surface_write_to_png(cairo_surf, filename);
    cairo_surface_destroy(cairo_surf);

    free(pixels);
    pixels = NULL;

    return 0;
}

WL_EXPORT int32_t
weston_layout_SetOptimizationMode(uint32_t id, uint32_t mode)
{
    /* not supported */
    (void)id;
    (void)mode;

    return 0;
}

WL_EXPORT int32_t
weston_layout_GetOptimizationMode(uint32_t id, uint32_t *pMode)
{
    /* not supported */
    (void)id;
    (void)pMode;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerAddNotification(struct weston_layout_layer *layout_layer,
                                layerPropertyNotificationFunc callback,
                                void *userdata)
{
    struct link_layerPropertyNotification *notification = NULL;

    if (layout_layer == NULL || callback == NULL) {
        weston_log("weston_layout_layerAddNotification: invalid argument\n");
        return -1;
    }

    notification = malloc(sizeof *notification);
    if (notification == NULL) {
        weston_log("fails to allocate memory\n");
        return -1;
    }

    notification->callback = callback;
    notification->userdata = userdata;
    wl_list_init(&notification->link);
    wl_list_insert(&layout_layer->list_notification, &notification->link);

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerRemoveNotification(struct weston_layout_layer *layout_layer)
{
    struct link_layerPropertyNotification *notification = NULL;
    struct link_layerPropertyNotification *next = NULL;

    if (layout_layer == NULL) {
        weston_log("weston_layout_layerRemoveNotification: invalid argument\n");
        return -1;
    }

    wl_list_for_each_safe(notification, next,
                          &layout_layer->list_notification, link) {
        if (!wl_list_empty(&notification->link)) {
            wl_list_remove(&notification->link);
        }
        free(notification);
        notification = NULL;
    }
    wl_list_init(&layout_layer->list_notification);

    return 0;
}

WL_EXPORT int32_t
weston_layout_getPropertiesOfSurface(struct weston_layout_surface *layout_surface,
                    struct weston_layout_SurfaceProperties *pSurfaceProperties)
{
    if (layout_surface == NULL || pSurfaceProperties == NULL) {
        weston_log("weston_layout_getPropertiesOfSurface: invalid argument\n");
        return -1;
    }

    *pSurfaceProperties = layout_surface->prop;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerAddSurface(struct weston_layout_layer *layout_layer,
                           struct weston_layout_surface *addsurf)
{
    struct weston_layout *layout = get_instance();
    struct weston_layout_surface *layout_surface = NULL;
    struct weston_layout_surface *next = NULL;
    int is_surf_in_layer = 0;

    if (layout_layer == NULL || addsurf == NULL) {
        weston_log("weston_layout_layerAddSurface: invalid argument\n");
        return -1;
    }

    is_surf_in_layer = is_surface_in_layer(addsurf, layout_layer);
    if (is_surf_in_layer == 1) {
        weston_log("weston_layout_layerAddSurface: addsurf is allready available\n");
        return 0;
    }

    wl_list_for_each_safe(layout_surface, next, &layout->list_surface, link) {
        if (layout_surface->id_surface == addsurf->id_surface) {
            if (!wl_list_empty(&layout_surface->pending.link)) {
                wl_list_remove(&layout_surface->pending.link);
            }
            wl_list_init(&layout_surface->pending.link);
            wl_list_insert(&layout_layer->pending.list_surface,
                           &layout_surface->pending.link);
            break;
        }
    }

    layout_layer->event_mask |= IVI_NOTIFICATION_ADD;

    return 0;
}

WL_EXPORT int32_t
weston_layout_layerRemoveSurface(struct weston_layout_layer *layout_layer,
                              struct weston_layout_surface *remsurf)
{
    struct weston_layout_surface *layout_surface = NULL;
    struct weston_layout_surface *next = NULL;

    if (layout_layer == NULL || remsurf == NULL) {
        weston_log("weston_layout_layerRemoveSurface: invalid argument\n");
        return -1;
    }

    wl_list_for_each_safe(layout_surface, next,
                          &layout_layer->pending.list_surface, pending.link) {
        if (layout_surface->id_surface == remsurf->id_surface) {
            if (!wl_list_empty(&layout_surface->pending.link)) {
                wl_list_remove(&layout_surface->pending.link);
            }
            wl_list_init(&layout_surface->pending.link);
            break;
        }
    }

    return 0;
}

WL_EXPORT int32_t
weston_layout_surfaceSetSourceRectangle(struct weston_layout_surface *layout_surface,
                                     int32_t x, int32_t y,
                                     uint32_t width, uint32_t height)
{
    struct weston_layout_SurfaceProperties *prop = NULL;

    if (layout_surface == NULL) {
        weston_log("weston_layout_surfaceSetSourceRectangle: invalid argument\n");
        return -1;
    }

    prop = &layout_surface->pending.prop;
    prop->sourceX = x;
    prop->sourceY = y;
    prop->sourceWidth = width;
    prop->sourceHeight = height;

    layout_surface->event_mask |= IVI_NOTIFICATION_SOURCE_RECT;

    return 0;
}

WL_EXPORT int32_t
weston_layout_commitChanges(void)
{
    struct weston_layout *layout = get_instance();

    commit_list_surface(layout);
    commit_list_layer(layout);
    commit_list_screen(layout);

    commit_changes(layout);
    send_prop(layout);
    weston_compositor_schedule_repaint(layout->compositor);

    return 0;
}
