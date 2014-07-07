/*
 * Copyright (C) 2014 DENSO CORPORATION
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

#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "ivi-layout.h"
#include "ivi-layout-export.h"
#include "ivi-layout-transition.h"
#include "ivi-layout-private.h"

struct ivi_layout_transition;
typedef void (*ivi_layout_transition_frame_func)(struct ivi_layout_transition *transition);
typedef void (*ivi_layout_transition_destroy_func)(struct ivi_layout_transition* transition);
typedef int32_t (*ivi_layout_transition_identifire_func)(void* private_data, void* id);

enum ivi_layout_inner_transition_type{
    IVI_LAYOUT_INNER_TRANSITION_NONE,
    IVI_LAYOUT_INNER_TRANSITION_VIEW_MOVE_RESIZE,
    IVI_LAYOUT_INNER_TRANSITION_VIEW_RESIZE,
    IVI_LAYOUT_INNER_TRANSITION_VIEW_FADE,
    IVI_LAYOUT_INNER_TRANSITION_LAYER_FADE,
    IVI_LAYOUT_INNER_TRANSITION_LAYER_MOVE,
    IVI_LAYOUT_INNER_TRANSITION_LAYER_VIEW_ORDER,
    IVI_LAYOUT_INNER_TRANSITION_MAX,
};

struct ivi_layout_transition {
    enum ivi_layout_inner_transition_type type;
    void *private_data;
    void *user_data;

    uint32_t time_start;
    uint32_t time_duration;
    uint32_t time_elapsed;
    uint32_t  is_done;
    ivi_layout_transition_identifire_func id_func;
    ivi_layout_transition_frame_func frame_func;
    ivi_layout_transition_destroy_func destroy_func;
};

struct transition_node {
    struct ivi_layout_transition *transition;
    struct wl_list link;
};

static void layout_transition_destroy(struct ivi_layout_transition* transition);

static struct ivi_layout_transition*
get_transition_from_type_and_id(enum ivi_layout_inner_transition_type type, void* id_data)
{
    struct ivi_layout* layout = get_instance();
    struct transition_node *node=NULL;
    wl_list_for_each(node, &layout->transitions->transition_list, link){
        if(node->transition->type == type)
            if(node->transition->id_func(node->transition->private_data, id_data))
                return node->transition;
    }
    return NULL;
}

static void
tick_transition(struct ivi_layout_transition *transition, uint32_t timestamp)
{
    const double t = timestamp - transition->time_start;

    if (transition->time_duration <= t) {
        transition->time_elapsed = transition->time_duration;
        transition->is_done = 1;
    } else {
        transition->time_elapsed = t;
    }
}

static float time_to_nowpos(struct ivi_layout_transition* transition)
{
    return sin((float)transition->time_elapsed / (float)transition->time_duration * M_PI_2);
}

static void
do_transition_frame(
    struct ivi_layout_transition *transition, uint32_t timestamp)
{
    if (0 == transition->time_start) {
        transition->time_start = timestamp;
    }

    tick_transition(transition, timestamp);
    transition->frame_func(transition);

    if(transition->is_done){
        layout_transition_destroy(transition);
    }
}

static int32_t
layout_transition_frame(void* data)
{
    struct ivi_layout_transition_set *transitions = data;
    uint32_t fps = 30;

    if (wl_list_empty(&transitions->transition_list)) {
        wl_event_source_timer_update(transitions->event_source, 0);
        return 1;
    }

    wl_event_source_timer_update(transitions->event_source, 1000 / fps);

    struct timespec timestamp = {0};
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    uint32_t msec = (1e+3 * timestamp.tv_sec + 1e-6 * timestamp.tv_nsec);

    struct transition_node *node = NULL;
    struct transition_node *next = NULL;

    wl_list_for_each_safe(node, next, &transitions->transition_list, link) {
        do_transition_frame(node->transition, msec);
    }

    ivi_layout_commitChanges();
    return 1;
}

WL_EXPORT struct ivi_layout_transition_set *
ivi_layout_transition_set_create(struct weston_compositor* ec)
{
    struct ivi_layout_transition_set *transitions = malloc(sizeof(*transitions));
    assert(transitions);

    wl_list_init(&transitions->transition_list);

    struct wl_event_loop *loop = wl_display_get_event_loop(ec->wl_display);
    transitions->event_source = wl_event_loop_add_timer(loop, layout_transition_frame, transitions);
    wl_event_source_timer_update(transitions->event_source, 0);

    return transitions;
}

static void
layout_transition_register(struct ivi_layout_transition *trans)
{
    struct ivi_layout* layout = get_instance();
    struct transition_node *node = NULL;

    node = malloc(sizeof(*node));
    assert(node);

    node->transition = trans;
    wl_list_insert(&layout->pending_transition_list, &node->link);
}

static void
remove_transition(struct ivi_layout* layout,
                 struct ivi_layout_transition *trans)
{
    struct transition_node *node = NULL;
    struct transition_node *next = NULL;

    wl_list_for_each_safe(node, next, &layout->transitions->transition_list, link) {
        if (node->transition == trans) {
            wl_list_remove(&node->link);
            free(node);
            return;
        }
    }

    wl_list_for_each_safe(node, next, &layout->pending_transition_list, link) {
        if (node->transition == trans) {
            wl_list_remove(&node->link);
            free(node);
            return;
        }
    }

    return;
}

static void
layout_transition_destroy(struct ivi_layout_transition *transition)
{
    struct ivi_layout* layout = get_instance();

    remove_transition(layout, transition);
    if(transition->destroy_func)
        transition->destroy_func(transition);
    free(transition);
}

static struct ivi_layout_transition*
create_layout_transition(void)
{
    struct ivi_layout_transition* transition = malloc(sizeof(*transition));
    assert(transition);

    transition->type = IVI_LAYOUT_INNER_TRANSITION_MAX;
    transition->time_start   = 0;
    transition->time_duration     = 300; // 300ms
    transition->time_elapsed = 0;

    transition->is_done = 0;

    transition->private_data    = NULL;
    transition->user_data       = NULL;

    transition->frame_func = NULL;
    transition->destroy_func    = NULL;

    return transition;
}

/* move and resize view transition */

struct move_resize_view_data {
    struct ivi_layout_surface* surface;
    int32_t start_x;
    int32_t start_y;
    int32_t end_x;
    int32_t end_y;
    uint32_t start_width;
    uint32_t start_height;
    uint32_t end_width;
    uint32_t end_height;
};

static void
transition_move_resize_view_destroy(struct ivi_layout_transition* transition)
{
    if(transition->private_data){
        free(transition->private_data);
        transition->private_data = NULL;
    }
}

static void
transition_move_resize_view_user_frame(struct ivi_layout_transition *transition)
{
    struct move_resize_view_data* private_data = transition->private_data;
    struct ivi_layout_surface *surface = private_data->surface;

    const double current = time_to_nowpos(transition);
    const int32_t destx = private_data->start_x + (private_data->end_x - private_data->start_x)*current;
    const int32_t desty = private_data->start_y + (private_data->end_y - private_data->start_y)*current;

    const uint32_t dest_width  = (int32_t)private_data->start_width  +
        ((int32_t)private_data->end_width  - (int32_t)private_data->start_width) *current;

    const uint32_t dest_height = (int32_t)private_data->start_height +
        ((int32_t)private_data->end_height - (int32_t)private_data->start_height)*current;

    ivi_layout_surfaceSetDestinationRectangle(surface,
                                                 destx, desty, dest_width, dest_height);
}

static int32_t
transition_move_resize_view_identifire(struct move_resize_view_data* data,
                                      struct ivi_layout_surface* view)
{
    return data->surface == view;
}

static struct ivi_layout_transition *
create_move_resize_view_transition(
    struct ivi_layout_surface* surface,
    int32_t start_x, int32_t start_y,
    int32_t end_x, int32_t end_y,
    uint32_t start_width, uint32_t start_height,
    uint32_t end_width, uint32_t end_height,
    ivi_layout_transition_frame_func frame_func,
    ivi_layout_transition_destroy_func destroy_func,
    uint32_t duration)
{
    struct ivi_layout_transition* transition = create_layout_transition();
    struct move_resize_view_data* data = malloc(sizeof(*data));

    transition->type = IVI_LAYOUT_INNER_TRANSITION_VIEW_MOVE_RESIZE;
    transition->id_func = (ivi_layout_transition_identifire_func)transition_move_resize_view_identifire;

    transition->frame_func = frame_func;
    transition->destroy_func = destroy_func;
    transition->private_data = data;

    if(duration != 0){
        transition->time_duration = duration;
    }

    data->surface = surface;
    data->start_x = start_x;
    data->start_y = start_y;
    data->end_x   = end_x;
    data->end_y   = end_y;

    data->start_width  = start_width;
    data->start_height = start_height;
    data->end_width    = end_width;
    data->end_height   = end_height;

    return transition;
}

WL_EXPORT void
ivi_layout_transition_move_resize_view(struct ivi_layout_surface* surface,
                                          int32_t dest_x, int32_t dest_y,
                                          uint32_t dest_width, uint32_t dest_height,
                                          uint32_t duration)
{
    int32_t start_pos[2] = {surface->pending.prop.startX,surface->pending.prop.startY};

    uint32_t start_size[2] = {surface->pending.prop.startWidth,surface->pending.prop.startHeight};

    struct ivi_layout_transition* transition = NULL;

    transition = get_transition_from_type_and_id(IVI_LAYOUT_INNER_TRANSITION_VIEW_MOVE_RESIZE,
                                                 surface);
    if(transition){

        transition->time_start = 0;
        transition->time_duration = duration;

        struct move_resize_view_data* data = transition->private_data;
        data->start_x = start_pos[0];
        data->start_y = start_pos[1];
        data->end_x   = dest_x;
        data->end_y   = dest_y;

        data->start_width  = start_size[0];
        data->start_height = start_size[1];
        data->end_width    = dest_width;
        data->end_height   = dest_height;
        return;
    }

    transition = create_move_resize_view_transition(
        surface,
        start_pos[0], start_pos[1],
        dest_x, dest_y,
        start_size[0], start_size[1],
        dest_width, dest_height,
        transition_move_resize_view_user_frame,
        transition_move_resize_view_destroy,
        duration);

    layout_transition_register(transition);

}

/* fade transition */
struct fade_view_data {
    struct ivi_layout_surface* surface;
    float start_alpha;
    float end_alpha;
};

struct store_alpha{
    float alpha;
};

static void
fade_view_user_frame(struct ivi_layout_transition *transition)
{
    struct fade_view_data* private_data = transition->private_data;
    struct ivi_layout_surface *surface = private_data->surface;

    const double current = time_to_nowpos(transition);
    const float alpha = private_data->start_alpha + (private_data->end_alpha - private_data->start_alpha)*current;

    ivi_layout_surfaceSetOpacity(surface, wl_fixed_from_double(alpha));
    ivi_layout_surfaceSetVisibility(surface, 1);
}

static int32_t
transition_fade_view_identifire(struct fade_view_data* data,
                               struct ivi_layout_surface* view)
{
    return data->surface == view;
}

static struct ivi_layout_transition*
create_fade_view_transition(
    struct ivi_layout_surface* surface,
    float start_alpha, float end_alpha,
    ivi_layout_transition_frame_func frame_func,
    void* user_data,
    ivi_layout_transition_destroy_func destroy_func,
    uint32_t duration)
{
    struct ivi_layout_transition* transition = create_layout_transition();
    struct fade_view_data* data = malloc(sizeof(*data));
    assert(data);

    transition->type = IVI_LAYOUT_INNER_TRANSITION_VIEW_FADE;
    transition->id_func = (ivi_layout_transition_identifire_func)transition_fade_view_identifire;

    transition->user_data = user_data;
    transition->private_data = data;
    transition->frame_func = frame_func;
    transition->destroy_func = destroy_func;

    if(duration != 0){
        transition->time_duration = duration;
    }

    data->surface = surface;
    data->start_alpha = start_alpha;
    data->end_alpha   = end_alpha;

    return transition;
}

static void
create_visibility_transition(struct ivi_layout_surface* surface,
                            float start_alpha,
                            float dest_alpha,
                            void* user_data,
                             ivi_layout_transition_destroy_func destroy_func,
                             uint32_t duration)
{
    struct ivi_layout_transition* transition = NULL;

    transition = create_fade_view_transition(
        surface,
        start_alpha, dest_alpha,
        fade_view_user_frame,
        user_data,
        destroy_func,
        duration);

    layout_transition_register(transition);

}

static void
visibility_on_transition_destroy(struct ivi_layout_transition* transition)
{
    struct fade_view_data *data = transition->private_data;
    ivi_layout_surfaceSetVisibility(data->surface, 1);

    free(data);
    transition->private_data = NULL;

    struct store_alpha *user_data = transition->user_data;

    free(user_data);
    transition->user_data = NULL;

}

WL_EXPORT void
ivi_layout_transition_visibility_on(struct ivi_layout_surface* surface,
                                       uint32_t duration)
{

    struct ivi_layout_transition* transition = NULL;

    transition = get_transition_from_type_and_id(IVI_LAYOUT_INNER_TRANSITION_VIEW_FADE,
                                                 surface);
    if(transition){
        transition->time_start = 0;
        transition->time_duration = duration;
        transition->destroy_func = visibility_on_transition_destroy;

        float start_alpha=0.0;
        ivi_layout_surfaceGetOpacity(surface, &start_alpha);

        const struct store_alpha* user_data = transition->user_data;
        struct fade_view_data* data = transition->private_data;
        data->start_alpha = wl_fixed_to_double(start_alpha);
        data->end_alpha = user_data->alpha;
        return;
    }

    int32_t is_visible = 0;
    ivi_layout_surfaceGetVisibility(surface, &is_visible);
    if(is_visible){
        return;
    }

    float dest_alpha = 0;
    ivi_layout_surfaceGetOpacity(surface, &dest_alpha);

    struct store_alpha* user_data = malloc(sizeof(*user_data));
    user_data->alpha = wl_fixed_to_double(dest_alpha);

    create_visibility_transition(surface,
                                 0.0, // start_alpha
                                 wl_fixed_to_double(dest_alpha),
                                 user_data,
                                 visibility_on_transition_destroy,
                                 duration);
}

static void
visibility_off_transition_destroy(struct ivi_layout_transition* transition)
{
    struct fade_view_data *data = transition->private_data;

    ivi_layout_surfaceSetVisibility(data->surface, 0);

    struct store_alpha* user_data = transition->user_data;
    ivi_layout_surfaceSetOpacity(data->surface, wl_fixed_from_double(user_data->alpha));

    free(data);
    transition->private_data = NULL;

    free(user_data);
    transition->user_data= NULL;

}

WL_EXPORT void
ivi_layout_transition_visibility_off(struct ivi_layout_surface* surface,
                                        uint32_t duration)
{

    struct ivi_layout_transition* transition = NULL;

    transition = get_transition_from_type_and_id(IVI_LAYOUT_INNER_TRANSITION_VIEW_FADE,
                                                 surface);
    if(transition){
        transition->time_start = 0;
        transition->time_duration = duration;
        transition->destroy_func = visibility_off_transition_destroy;

        float start_alpha=0.0;
        ivi_layout_surfaceGetOpacity(surface, &start_alpha);

        struct fade_view_data* data = transition->private_data;
        data->start_alpha = wl_fixed_to_double(start_alpha);
        data->end_alpha = 0;
        return;
    }

    float start_alpha=0;
    ivi_layout_surfaceGetOpacity(surface, &start_alpha);

    struct store_alpha* user_data = malloc(sizeof(*user_data));
    user_data->alpha = wl_fixed_to_double(start_alpha);

    create_visibility_transition(surface,
                                 wl_fixed_to_double(start_alpha),
                                 0.0f, // dest_alpha
                                 user_data,
                                 visibility_off_transition_destroy,
                                 duration);
}

/* move layer transition */

struct move_layer_data {
    struct ivi_layout_layer* layer;
    int32_t start_x;
    int32_t start_y;
    int32_t end_x;
    int32_t end_y;
    ivi_layout_transition_destroy_user_func destroy_func;
};

static void
transition_move_layer_user_frame(struct ivi_layout_transition* transition)
{
    struct move_layer_data* data = transition->private_data;
    struct ivi_layout_layer* layer = data->layer;

    const float  current = time_to_nowpos(transition);

    const int32_t dest_x = data->start_x + (data->end_x - data->start_x) * current;
    const int32_t dest_y = data->start_y + (data->end_y - data->start_y) * current;

    int32_t pos[2] = {dest_x, dest_y};

    ivi_layout_layerSetPosition(layer, pos);

}

static void
transition_move_layer_destroy(struct ivi_layout_transition* transition)
{

    struct move_layer_data* data = transition->private_data;

    if(data->destroy_func)
        data->destroy_func(transition->user_data);

    free(data);
    transition->private_data = NULL;

}

static int32_t
transition_move_layer_identifire(struct move_layer_data* data, struct ivi_layout_layer* layer)
{
    return data->layer == layer;
}


static struct ivi_layout_transition*
create_move_layer_transition(
    struct ivi_layout_layer* layer,
    int32_t start_x, int32_t start_y,
    int32_t end_x, int32_t end_y,
    void* user_data,
    ivi_layout_transition_destroy_user_func destroy_user_func,
    uint32_t duration)
{
    struct ivi_layout_transition* transition = create_layout_transition();
    struct move_layer_data* data = malloc(sizeof(*data));

    transition->type = IVI_LAYOUT_INNER_TRANSITION_LAYER_MOVE;
    transition->id_func = (ivi_layout_transition_identifire_func)transition_move_layer_identifire;

    transition->frame_func = transition_move_layer_user_frame;
    transition->destroy_func = transition_move_layer_destroy;
    transition->private_data = data;
    transition->user_data = user_data;

    if(duration != 0)
        transition->time_duration = duration;

    data->layer = layer;
    data->start_x = start_x;
    data->start_y = start_y;
    data->end_x   = end_x;
    data->end_y   = end_y;
    data->destroy_func = destroy_user_func;

    return transition;
}

WL_EXPORT void
ivi_layout_transition_move_layer(struct ivi_layout_layer* layer,
                                    int32_t dest_x, int32_t dest_y,
                                    uint32_t duration)
{
    int32_t start_pos[2] = {};
    ivi_layout_layerGetPosition(layer, start_pos);

    struct ivi_layout_transition* transition = NULL;
    transition = create_move_layer_transition(
        layer,
        start_pos[0], start_pos[1],
        dest_x, dest_y,
        NULL, NULL,
        duration);

    layout_transition_register(transition);

    return;
}

WL_EXPORT void
ivi_layout_transition_move_layer_cancel(struct ivi_layout_layer* layer)
{
    struct ivi_layout_transition* transition =
        get_transition_from_type_and_id(IVI_LAYOUT_INNER_TRANSITION_LAYER_MOVE, layer);
    if(transition){
        layout_transition_destroy(transition);
    }
}

/* fade layer transition */
struct fade_layer_data {
    struct ivi_layout_layer* layer;
    int32_t is_fade_in;
    double start_alpha;
    double end_alpha;
    ivi_layout_transition_destroy_user_func destroy_func;
};

static void
transition_fade_layer_destroy(struct ivi_layout_transition* transition)
{
    struct fade_layer_data* data = transition->private_data;
    transition->private_data = NULL;

    free(data);
}

static void
transition_fade_layer_user_frame(struct ivi_layout_transition *transition)
{
    double current = time_to_nowpos(transition);
    struct fade_layer_data* data = transition->private_data;
    double alpha = data->start_alpha + (data->end_alpha - data->start_alpha) * current;
    int32_t fixed_alpha = wl_fixed_from_double(alpha);

    int32_t is_done = transition->is_done;
    int32_t is_visible = !is_done || data->is_fade_in;

    ivi_layout_layerSetOpacity(data->layer, fixed_alpha);
    ivi_layout_layerSetVisibility(data->layer, is_visible);
}

static int32_t
transition_fade_layer_identifire(struct fade_layer_data* data, struct ivi_layout_layer* layer)
{
    return data->layer == layer;
}

WL_EXPORT void
ivi_layout_transition_fade_layer(struct ivi_layout_layer* layer,
                                    int32_t is_fade_in,
                                    double start_alpha, double end_alpha,
                                    void* user_data,
                                    ivi_layout_transition_destroy_user_func destroy_func,
                                    uint32_t duration)
{
    struct ivi_layout_transition* transition = NULL;

    transition = get_transition_from_type_and_id(IVI_LAYOUT_INNER_TRANSITION_LAYER_FADE, layer);
    if(transition){
        /* transition update */
        struct fade_layer_data* data = transition->private_data;

        float fixed_opacity=0.0; //FIXME
        ivi_layout_layerGetOpacity(layer, &fixed_opacity);
        const double now_opacity = wl_fixed_to_double(fixed_opacity);

        data->is_fade_in = is_fade_in;
        data->start_alpha = now_opacity;
        data->end_alpha = end_alpha;

        float remain = is_fade_in? 1.0 - now_opacity : now_opacity;
        transition->time_start = 0;
        transition->time_elapsed = 0;
        transition->time_duration = duration * remain;

        return;
    }

    transition = create_layout_transition();
    struct fade_layer_data* data = malloc(sizeof(*data));
    assert(data);

    transition->type = IVI_LAYOUT_INNER_TRANSITION_LAYER_FADE;
    transition->id_func = (ivi_layout_transition_identifire_func)transition_fade_layer_identifire;

    transition->private_data = data;
    transition->user_data = user_data;

    transition->frame_func = transition_fade_layer_user_frame;
    transition->destroy_func = transition_fade_layer_destroy;

    if(duration != 0){
        transition->time_duration = duration;
    }

    data->layer = layer;
    data->is_fade_in = is_fade_in;
    data->start_alpha = start_alpha;
    data->end_alpha = end_alpha;
    data->destroy_func = destroy_func;

    layout_transition_register(transition);

    return;
}

/* render order transition */
struct surface_reorder{
    uint32_t id_surface;
    uint32_t new_index;
};

struct change_order_data{
    struct ivi_layout_layer* layer;
    uint32_t surface_num;
    struct surface_reorder* reorder;
};

struct surf_with_index{
    uint32_t id_surface;
    float surface_index;
};

static int cmp_order_asc(const void* lhs, const void* rhs)
{
    return ((struct surf_with_index*)lhs)->surface_index > ((struct surf_with_index*)rhs)->surface_index;
}

/*
render oerder transition

index   0      1      2
old   surfA, surfB, surfC
new   surfB, surfC, surfA
       (-1)  (-1)   (+2)

after 10% of time elapsed
       0.2    0.9    1.9
      surfA, surfB, surfC

after 50% of time elapsed
       0.5    1.0    1.5
      surfB, surfA, surfC
*/

static void
transition_change_order_user_frame(struct ivi_layout_transition *transition)
{
    uint32_t i, old_index;
    double current = time_to_nowpos(transition);
    struct change_order_data* data = transition->private_data;

    struct surf_with_index* swi = malloc(sizeof(*swi)*data->surface_num);

    for(old_index=0; old_index<data->surface_num; old_index++){
        swi[old_index].id_surface = data->reorder[old_index].id_surface;
        swi[old_index].surface_index = (float)old_index +
            ((float)data->reorder[old_index].new_index - (float)old_index) * current;
    }

    qsort(swi, data->surface_num, sizeof(*swi), cmp_order_asc);

/*
    fprintf(stderr, "=========\n");
    for(i=0; i<data->surface_num;i++){
        fprintf(stderr, "surface %d: %f\n", swi[i].id_surface, swi[i].surface_index);
    }
*/

    struct ivi_layout_surface** new_surface_order =
        malloc(sizeof(*new_surface_order) * data->surface_num);

    uint32_t surface_num = 0;
    for(i=0; i<data->surface_num; i++){
        struct ivi_layout_surface* surf = ivi_layout_getSurfaceFromId(swi[i].id_surface);
        if(surf)
            new_surface_order[surface_num++] = surf;
    }

    ivi_layout_layerSetRenderOrder(data->layer, new_surface_order, surface_num);

    free(new_surface_order);
    free(swi);
}

static void
transition_change_order_destroy(struct ivi_layout_transition* transition)
{
    struct change_order_data* data = transition->private_data;

    free(data->reorder);
    free(data);
}

static int32_t find_surface(struct ivi_layout_surface** surfaces,
                            uint32_t surface_num,
                            struct ivi_layout_surface* target)
{
    uint32_t i=0;
    for(i=0; i<surface_num; i++){
        if(surfaces[i] == target)
            return i;
    }

    return -1;
}

static int32_t
transition_change_order_identifire(struct change_order_data* data, struct ivi_layout_layer* layer)
{
    return data->layer == layer;
}

WL_EXPORT void
ivi_layout_transition_layer_render_order(struct ivi_layout_layer* layer,
                                            struct ivi_layout_surface** new_order,
                                            uint32_t surface_num,
                                            uint32_t duration)
{
    struct surface_reorder* reorder = malloc(sizeof(*reorder)*surface_num);
    struct ivi_layout_surface* surf=NULL;
    uint32_t old_index = 0;

    wl_list_for_each(surf, &layer->order.list_surface, order.link){
        int32_t new_index = find_surface(new_order, surface_num, surf);
        if(new_index < 0){
            fprintf(stderr, "invalid render order!!!\n");
            return;
        }

        const uint32_t id = ivi_layout_getIdOfSurface(surf);

        reorder[old_index].id_surface = id;
        reorder[old_index].new_index = new_index;
        old_index++;
    }

    struct ivi_layout_transition* transition = NULL;

    transition = get_transition_from_type_and_id(IVI_LAYOUT_INNER_TRANSITION_LAYER_VIEW_ORDER, layer);
    if(transition){
        /* update transition */
        transition->time_start = 0; /* timer reset */

        if(duration != 0){
            transition->time_duration = duration;
        }

        struct change_order_data* data = transition->private_data;
        free(data->reorder);
        data->reorder = reorder;
        return;
    }

    transition = create_layout_transition();
    struct change_order_data* data = malloc(sizeof(*data));
    assert(data);

    transition->type = IVI_LAYOUT_INNER_TRANSITION_LAYER_VIEW_ORDER;
    transition->id_func = (ivi_layout_transition_identifire_func)transition_change_order_identifire;

    transition->private_data = data;
    transition->frame_func = transition_change_order_user_frame;
    transition->destroy_func = transition_change_order_destroy;

    if(duration != 0){
        transition->time_duration = duration;
    }

    data->layer = layer;
    data->reorder = reorder;
    data->surface_num = old_index;

    layout_transition_register(transition);

}

WL_EXPORT int32_t
ivi_layout_surfaceSetTransition(struct ivi_layout_surface *ivisurf,
                                enum ivi_layout_transition_type type,
                                uint32_t duration)
{
	struct ivi_layout_SurfaceProperties *prop = NULL;

	if (ivisurf == NULL) {
	    weston_log("ivi_layout_surfaceSetTransition: invalid argument\n");
	    return -1;
	}

	prop = &ivisurf->pending.prop;
	prop->transitionType = type;
	prop->transitionDuration = duration;
	return 0;
}

WL_EXPORT int32_t
ivi_layout_surfaceSetTransitionDuration(struct ivi_layout_surface *ivisurf,uint32_t duration)
{
    struct ivi_layout_SurfaceProperties *prop = NULL;

    if (ivisurf == NULL) {
        weston_log("ivi_layout_surfaceSetTransitionDuration: invalid argument\n");
        return -1;
    }

    prop = &ivisurf->pending.prop;
    prop->transitionDuration = duration*10;
    return 0;
}

WL_EXPORT int32_t
ivi_layout_layerSetTransition(struct ivi_layout_layer *ivilayer,
                              enum ivi_layout_transition_type type,
                              uint32_t duration)
{
    if (ivilayer == NULL) {
        weston_log("ivi_layout_layerSetTransitionType: invalid argument\n");
        return -1;
    }

    ivilayer->pending.prop.transitionType = type;
    ivilayer->pending.prop.transitionDuration = duration;

    return 0;
}

WL_EXPORT int32_t
ivi_layout_layerSetFadeInfo(struct ivi_layout_layer* ivilayer,
                                    uint32_t is_fade_in,
                                    double start_alpha, double end_alpha)
{
    if (ivilayer == NULL) {
        weston_log("ivi_layout_layerSetFadeInfo: invalid argument\n");
        return -1;
    }

    ivilayer->pending.prop.isFadeIn = is_fade_in;
    ivilayer->pending.prop.startAlpha = start_alpha;
    ivilayer->pending.prop.endAlpha = end_alpha;

    return 0;
}
