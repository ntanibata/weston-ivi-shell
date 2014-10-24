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

#ifndef _ivi_layout_PRIVATE_H_
#define _ivi_layout_PRIVATE_H_

#include "compositor.h"
#include "ivi-layout.h"
#include "ivi-layout-transition.h"

struct ivi_layout_surface {
    struct wl_list link;
    struct wl_signal property_changed;
    struct wl_list list_layer;
    int32_t update_count;
    uint32_t id_surface;

    struct ivi_layout *layout;
    struct weston_surface *surface;

    struct wl_listener surface_destroy_listener;
    struct weston_transform surface_rotation;
    struct weston_transform layer_rotation;
    struct weston_transform surface_pos;
    struct weston_transform layer_pos;
    struct weston_transform scaling;
    struct ivi_layout_SurfaceProperties prop;
    int32_t pixelformat;
    uint32_t event_mask;

    struct {
        struct ivi_layout_SurfaceProperties prop;
        struct wl_list link;
    } pending;

    struct {
        struct wl_list link;
        struct wl_list list_layer;
    } order;

    struct {
        ivi_controller_surface_content_callback callback;
        void* userdata;
    } content_observer;

    struct wl_signal configured;
};

struct ivi_layout_layer {
    struct wl_list link;
    struct wl_signal property_changed;
    struct wl_list list_screen;
    struct wl_list link_to_surface;
    uint32_t id_layer;

    struct ivi_layout *layout;

    struct ivi_layout_LayerProperties prop;
    uint32_t event_mask;

    struct {
        struct ivi_layout_LayerProperties prop;
        struct wl_list list_surface;
        struct wl_list link;
    } pending;

    struct {
        struct wl_list list_surface;
        struct wl_list link;
    } order;
};

/*
struct ivi_layout_surface {
    struct wl_list link;
    struct wl_list list_notification;
    struct wl_list list_layer;
    uint32_t update_count;
    uint32_t id_surface;

    struct ivi_layout *layout;
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
    struct ivi_layout_SurfaceProperties prop;
    int32_t pixelformat;
    uint32_t event_mask;

    struct {
        struct ivi_layout_SurfaceProperties prop;
        struct wl_list link;
    } pending;

    struct {
        struct wl_list link;
        struct wl_list list_layer;
    } order;

    struct {
        ivi_controller_surface_content_callback callback;
        void* userdata;
    } content_observer;
};

struct ivi_layout_layer {
    struct wl_list link;
    struct wl_list list_notification;
    struct wl_list list_screen;
    struct wl_list link_to_surface;
    uint32_t id_layer;

    struct ivi_layout *layout;

    struct ivi_layout_LayerProperties prop;
    uint32_t event_mask;

    struct {
        struct ivi_layout_LayerProperties prop;
        struct wl_list list_surface;
        struct wl_list link;
    } pending;

    struct {
        struct wl_list list_surface;
        struct wl_list link;
    } order;
};
*/

struct ivi_layout {
    struct weston_compositor *compositor;

    struct wl_list list_surface;
    struct wl_list list_layer;
    struct wl_list list_screen;

    struct {
        struct wl_signal created;
        struct wl_signal removed;
    } layer_notification;

    struct {
        struct wl_signal created;
        struct wl_signal removed;
        struct wl_signal configure_changed;
    } surface_notification;

    struct weston_layer layout_layer;
    struct wl_signal warning_signal;

    struct ivi_layout_transition_set* transitions;
    struct wl_list pending_transition_list;

    struct wl_listener seat_create_listener;
};
/*
struct ivi_layout {
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

    struct weston_layer layout_layer;

    struct ivi_layout_transition_set* transitions;
    struct wl_list pending_transition_list;
};
*/
struct ivi_layout *get_instance(void);

#endif
