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

#ifndef _WESTON_LAYOUT_TRANSITION_H_
#define _WESTON_LAYOUT_TRANSITION_H_

#include "ivi-layout.h"

struct ivi_layout_transition;

struct ivi_layout_transition_set {
    struct wl_event_source  *event_source;
    struct wl_list          transition_list;
};

typedef void (*ivi_layout_transition_destroy_user_func)(void* user_data);

struct ivi_layout_transition_set *
ivi_layout_transition_set_create(struct weston_compositor* ec);

void
ivi_layout_transition_move_resize_view(struct ivi_layout_surface* surface,
                                          int32_t dest_x, int32_t dest_y,
                                          uint32_t dest_width, uint32_t dest_height,
                                          uint32_t duration);

void
ivi_layout_transition_visibility_on(struct ivi_layout_surface* surface,
                                       uint32_t duration);

void
ivi_layout_transition_visibility_off(struct ivi_layout_surface* surface,
                                        uint32_t duration);


void
ivi_layout_transition_move_layer(struct ivi_layout_layer* layer,
                                    int32_t dest_x, int32_t dest_y,
                                    uint32_t duration);

void
ivi_layout_transition_move_layer_cancel(struct ivi_layout_layer* layer);

void
ivi_layout_transition_fade_layer(struct ivi_layout_layer* layer,
                                    int32_t is_fade_in,
                                    double start_alpha, double end_alpha,
                                    void* user_data,
                                    ivi_layout_transition_destroy_user_func destroy_func,
                                    uint32_t duration);

void
ivi_layout_transition_layer_render_order(struct ivi_layout_layer* layer,
                                           struct ivi_layout_surface** new_order,
                                            uint32_t surface_num,
                                            uint32_t duration);

WL_EXPORT int32_t
is_surface_transition(struct ivi_layout_surface* surface);

#endif
