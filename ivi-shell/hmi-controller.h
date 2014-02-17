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

#ifndef _HMI_CONTROLLER_H_
#define _HMI_CONTROLLER_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <wayland-util.h>

struct
hmi_controller_srfInfo {
    uint32_t    id;
    char        *filePath;
    uint32_t    color;
};

struct
hmi_controller_workspace {
    struct wl_array     launcher_id_array;
    struct wl_list      link;
};

struct
hmi_controller_launcher {
    uint32_t            icon_surface_id;
    uint32_t            workspace_id;
    char*               icon;
    char*               path;
    struct wl_array     setid_window_titles;
    struct wl_list      link;
};

struct
hmi_controller_setting {
    uint32_t    desktop_layer_id;
    uint32_t    application_layer_id;
    uint32_t    workspace_backgound_layer_id;
    uint32_t    workspace_layer_id;
    uint32_t    cursor_layer_id;
    struct hmi_controller_srfInfo  background;
    struct hmi_controller_srfInfo  panel;
    struct hmi_controller_srfInfo  tiling;
    struct hmi_controller_srfInfo  sidebyside;
    struct hmi_controller_srfInfo  fullscreen;
    struct hmi_controller_srfInfo  random;
    struct hmi_controller_srfInfo  home;
    struct hmi_controller_srfInfo  cursor;
    struct hmi_controller_srfInfo  workspace_background;
    char*       surface_creator_path;

    struct wl_list workspace_list;
    struct wl_list launcher_list;
};

int hmi_client_start(void);

#ifdef __cplusplus
} /**/
#endif /* __cplusplus */

#endif /* _HMI_CONTROLLER_H_ */
