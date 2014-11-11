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

/**
 * A reference implementation how to use ivi-layout APIs in order to manage
 * layout of ivi_surfaces/ivi_layers. Layout change is triggered by ivi-hmi-controller
 * protocol, ivi-hmi-controller.xml. A reference how to use the protocol, see
 * hmi-controller-homescreen.
 *
 * In-Vehicle Infotainment system usually manage properties of ivi_surfaces/ivi_layers
 * by only a central component which decide where ivi_surfaces/ivi_layers shall be. This
 * reference show examples to implement the central component as a module of weston.
 *
 * Default Scene graph of UI is defined in hmi_controller_create. It consists of
 * - In the bottom, a base ivi_layer to group ivi_surfaces of background, panel,
 *   and buttons
 * - Next, a application ivi_layer to show application ivi_surfaces.
 * - Workspace background ivi_layer to show a ivi_surface of background image.
 * - Workspace ivi_layer to show launcher to launch application with icons. Paths to
 *   binary and icon are defined in weston.ini. The width of this ivi_layer is longer
 *   than the size of ivi_screen because a workspace has several pages and is controlled
 *   by motion of input.
 *
 * TODO: animation method shall be refined
 * TODO: support fade-in when UI is ready
 */

#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>
#include <assert.h>
#include <time.h>

#include "ivi-layout-export.h"
#include "ivi-hmi-controller-server-protocol.h"

/*****************************************************************************
 *  structure, globals
 ****************************************************************************/
struct hmi_controller_layer {
    struct ivi_layout_layer  *ivilayer;
    uint32_t id_layer;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

struct link_layer {
    struct ivi_layout_layer *layout_layer;
    struct wl_list link;
};

struct hmi_controller_fade {
    uint32_t is_fade_in;
    struct wl_list layer_list;
};

struct
hmi_server_setting {
    uint32_t    base_layer_id;
    uint32_t    application_layer_id;
    uint32_t    workspace_background_layer_id;
    uint32_t    workspace_layer_id;
    int32_t    panel_height;
    uint32_t    transition_duration;
    char       *ivi_homescreen;
};

struct hmi_controller
{
    struct hmi_server_setting          *hmi_setting;
    struct hmi_controller_layer         base_layer;
    struct hmi_controller_layer         application_layer;
    struct hmi_controller_layer         workspace_background_layer;
    struct hmi_controller_layer         workspace_layer;
    enum ivi_hmi_controller_layout_mode layout_mode;

    struct hmi_controller_fade              workspace_fade;

    int32_t                                 workspace_count;
    struct wl_array                     ui_widgets;
    int32_t                             is_initialized;

    struct weston_compositor           *compositor;
    struct wl_listener                  destroy_listener;
};

struct launcher_info
{
    uint32_t surface_id;
    uint32_t workspace_id;
    int32_t index;
};

/*****************************************************************************
 *  local functions
 ****************************************************************************/
static void *
fail_on_null(void *p, size_t size, char* file, int32_t line)
{
    if (size && !p) {
        weston_log("%s(%d) %zd: out of memory\n", file, line, size);
        exit(EXIT_FAILURE);
    }

    return p;
}

static void *
mem_alloc(size_t size, char* file, int32_t line)
{
    return fail_on_null(calloc(1, size), size, file, line);
}

#define MEM_ALLOC(s) mem_alloc((s),__FILE__,__LINE__)

static int32_t
is_surf_in_ui_widget(struct hmi_controller *hmi_ctrl,
                         struct ivi_layout_surface *ivisurf)
{
    uint32_t id = ivi_layout_get_id_of_surface(ivisurf);

    uint32_t *ui_widget_id = NULL;
    wl_array_for_each (ui_widget_id, &hmi_ctrl->ui_widgets) {
        if (*ui_widget_id == id) {
            return 1;
        }
    }

    return 0;
}

static int
compare_launcher_info(const void *lhs, const void *rhs)
{
    const struct launcher_info *left = (const struct launcher_info *)lhs;
    const struct launcher_info *right = (const struct launcher_info *)rhs;

    if (left->workspace_id < right->workspace_id) {
        return -1;
    }

    if (left->workspace_id > right->workspace_id) {
        return 1;
    }

    if (left->index < right->index) {
        return -1;
    }

    if (left->index > right->index) {
        return 1;
    }

    return 0;
}

/**
 * Internal methods called by mainly ivi_hmi_controller_switch_mode
 * This reference shows 4 examples how to use ivi_layout APIs.
 */
static void
mode_divided_into_tiling(struct hmi_controller *hmi_ctrl,
                        struct ivi_layout_surface **pp_surface,
                        int32_t surface_length,
                        struct hmi_controller_layer *layer)
{
    const float surface_width  = (float)layer->width * 0.25;
    const float surface_height = (float)layer->height * 0.5;
    int32_t surface_x = 0;
    int32_t surface_y = 0;
    struct ivi_layout_surface *ivisurf  = NULL;

    struct ivi_layout_surface **surfaces = MEM_ALLOC(sizeof(*surfaces) * surface_length);
    struct ivi_layout_surface **new_order = MEM_ALLOC(sizeof(*surfaces) * surface_length);

    const uint32_t duration = hmi_ctrl->hmi_setting->transition_duration;

    int32_t i = 0;
    int32_t surf_num = 0;
    uint32_t num = 1;

    for (i = 0; i < surface_length; i++) {
        ivisurf = pp_surface[i];

        /* skip ui widgets */
        if (is_surf_in_ui_widget(hmi_ctrl, ivisurf)) {
            continue;
        }

        surfaces[surf_num++] = ivisurf;
    }

    for(i=0; i < surf_num; i++){

        ivisurf = surfaces[i];
        new_order[i] = ivisurf;

        if (num <= 8) {
            if (num < 5) {
                surface_x = (int32_t)((num - 1) * (surface_width));
                surface_y = 0;
            }
            else {
                surface_x = (int32_t)((num - 5) * (surface_width));
                surface_y = (int32_t)surface_height;
            }

            ivi_layout_surface_set_transition(ivisurf,IVI_LAYOUT_TRANSITION_VIEW_DEFAULT, duration);
            ivi_layout_surface_set_visibility(ivisurf, true);
            ivi_layout_surface_set_destination_rectangle(ivisurf, surface_x, surface_y,
                    (int32_t)surface_width, (int32_t)surface_height);

            num++;
            continue;
        }
        ivi_layout_surface_set_visibility(ivisurf, false);
    }

    if(surf_num > 0){
        ivi_layout_layer_set_transition(layer->ivilayer,IVI_LAYOUT_TRANSITION_LAYER_VIEW_ORDER,duration);
        //TODO: implement IVI_LAYOUT_TRANSITION_LAYER_VIEW_ORDER later.
        ivi_layout_transition_layer_render_order(layer->ivilayer,
                                                    new_order,
                                                    surf_num,
                                                    duration);
    }

    free(surfaces);
    free(new_order);
}

static void
mode_divided_into_sidebyside(struct hmi_controller *hmi_ctrl,
                      struct ivi_layout_surface **pp_surface,
                      int32_t surface_length,
                      struct hmi_controller_layer *layer)
{
    int32_t surface_width  = layer->width / 2;
    int32_t surface_height = layer->height;
    struct ivi_layout_surface *ivisurf  = NULL;

    const uint32_t duration = hmi_ctrl->hmi_setting->transition_duration;
    int32_t i = 0;
    int32_t num = 1;
    for (i = 0; i < surface_length; i++) {
        ivisurf = pp_surface[i];

        /* skip ui widgets */
        if (is_surf_in_ui_widget(hmi_ctrl, ivisurf)) {
            continue;
        }

        if (num == 1) {
            ivi_layout_surface_set_transition(ivisurf,IVI_LAYOUT_TRANSITION_VIEW_DEFAULT,duration);
            ivi_layout_surface_set_visibility(ivisurf, true);
            ivi_layout_surface_set_destination_rectangle(ivisurf, 0, 0,
                                                         surface_width, surface_height);

            num++;
            continue;
        }
        else if (num == 2) {
            ivi_layout_surface_set_transition(ivisurf,IVI_LAYOUT_TRANSITION_VIEW_DEFAULT,duration);
            ivi_layout_surface_set_visibility(ivisurf, true);
            ivi_layout_surface_set_destination_rectangle(ivisurf, surface_width, 0,
                                                         surface_width, surface_height);

            num++;
            continue;
        }
        ivi_layout_surface_set_transition(ivisurf,IVI_LAYOUT_TRANSITION_VIEW_FADE_ONLY,duration);
        ivi_layout_surface_set_visibility(ivisurf, false);
    }
}

static void
mode_fullscreen_someone(struct hmi_controller *hmi_ctrl,
                      struct ivi_layout_surface **pp_surface,
                      int32_t surface_length,
                      struct hmi_controller_layer *layer)
{
    const int32_t  surface_width  = layer->width;
    const int32_t  surface_height = layer->height;
    struct ivi_layout_surface *ivisurf  = NULL;

    int32_t i = 0;
    const uint32_t duration = hmi_ctrl->hmi_setting->transition_duration;
    for (i = 0; i < surface_length; i++) {
        ivisurf = pp_surface[i];

        /* skip ui widgets */
        if (is_surf_in_ui_widget(hmi_ctrl, ivisurf)) {
            continue;
        }
        ivi_layout_surface_set_transition(ivisurf,IVI_LAYOUT_TRANSITION_VIEW_DEFAULT,duration);
        ivi_layout_surface_set_visibility(ivisurf, true);
        ivi_layout_surface_set_destination_rectangle(ivisurf, 0, 0,
                                                     surface_width, surface_height);
    }

}

static void
mode_random_replace(struct hmi_controller *hmi_ctrl,
                    struct ivi_layout_surface **pp_surface,
                    int32_t surface_length,
                    struct hmi_controller_layer *layer)
{
    const int32_t surface_width  = (int32_t)(layer->width * 0.25f);
    const int32_t surface_height = (int32_t)(layer->height * 0.25f);
    int32_t surface_x = 0;
    int32_t surface_y = 0;


    struct ivi_layout_surface *ivisurf  = NULL;

    const uint32_t duration = hmi_ctrl->hmi_setting->transition_duration;

    int32_t i = 0;
    for (i = 0; i < surface_length; i++) {
        ivisurf = pp_surface[i];

        /* skip ui widgets */
        if (is_surf_in_ui_widget(hmi_ctrl, ivisurf)) {
            continue;
        }

        ivi_layout_surface_set_transition(ivisurf,IVI_LAYOUT_TRANSITION_VIEW_DEFAULT,duration);
        ivi_layout_surface_set_visibility(ivisurf, true);
        surface_x = rand() % (layer->width - surface_width);
        surface_y = rand() % (layer->height - surface_height);

        ivi_layout_surface_set_destination_rectangle(ivisurf, surface_x, surface_y,
                surface_width, surface_height);
    }

}

static int32_t
has_application_surface(struct hmi_controller *hmi_ctrl,
                        struct ivi_layout_surface **pp_surface,
                        int32_t surface_length)
{
    struct ivi_layout_surface *ivisurf  = NULL;
    int32_t i = 0;

    for (i = 0; i < surface_length; i++) {
        ivisurf = pp_surface[i];

        /* skip ui widgets */
        if (is_surf_in_ui_widget(hmi_ctrl, ivisurf)) {
            continue;
        }

        return 1;
    }

    return 0;
}

/**
 * Supports 4 example to layout of application ivi_surfaces;
 * tiling, side by side, fullscreen, and random.
 */
static void
switch_mode(struct hmi_controller *hmi_ctrl,
            enum ivi_hmi_controller_layout_mode layout_mode)
{
    struct hmi_controller_layer *layer = &hmi_ctrl->application_layer;
    struct ivi_layout_surface **pp_surface = NULL;
    int32_t surface_length = 0;
    int32_t ret = 0;

    if (!hmi_ctrl->is_initialized) {
        return;
    }

    hmi_ctrl->layout_mode = layout_mode;

    ret = ivi_layout_get_surfaces(&surface_length, &pp_surface);
    assert(!ret);

    if (!has_application_surface(hmi_ctrl, pp_surface, surface_length)) {
        free(pp_surface);
        pp_surface = NULL;
        return;
    }

    switch (layout_mode) {
    case IVI_HMI_CONTROLLER_LAYOUT_MODE_TILING:
        mode_divided_into_tiling(hmi_ctrl, pp_surface, surface_length, layer);
        break;
    case IVI_HMI_CONTROLLER_LAYOUT_MODE_SIDE_BY_SIDE:
        mode_divided_into_sidebyside(hmi_ctrl, pp_surface, surface_length, layer);
        break;
    case IVI_HMI_CONTROLLER_LAYOUT_MODE_FULL_SCREEN:
        mode_fullscreen_someone(hmi_ctrl, pp_surface, surface_length, layer);
        break;
    case IVI_HMI_CONTROLLER_LAYOUT_MODE_RANDOM:
        mode_random_replace(hmi_ctrl, pp_surface, surface_length, layer);
        break;
    }

    ivi_layout_commit_changes();
    free(pp_surface);
}

/**
 * Internal method for transition
 */
static void
hmi_controller_fade_run(struct hmi_controller* hmi_ctrl, uint32_t is_fade_in, struct hmi_controller_fade *fade)
{
    double tint = is_fade_in ? 1.0 : 0.0;
    struct link_layer* linklayer = NULL;
    const uint32_t duration = hmi_ctrl->hmi_setting->transition_duration;

    fade->is_fade_in = is_fade_in;

    wl_list_for_each(linklayer, &fade->layer_list, link){
        ivi_layout_layer_set_transition(linklayer->layout_layer,IVI_LAYOUT_TRANSITION_LAYER_FADE,duration);
        ivi_layout_layer_set_fade_info(linklayer->layout_layer,is_fade_in,1.0 - tint, tint);
    }
}

/**
 * Internal method to create ivi_layer with hmi_controller_layer and add to a ivi_screen
 */
static void
create_layer(struct ivi_layout_screen  *iviscrn,
             struct hmi_controller_layer *layer)
{
    int32_t ret = 0;

    layer->ivilayer = ivi_layout_layer_create_with_dimension(layer->id_layer,
                                                layer->width, layer->height);
    assert(layer->ivilayer != NULL);

    ret = ivi_layout_screen_add_layer(iviscrn, layer->ivilayer);
    assert(!ret);

    ret = ivi_layout_layer_set_destination_rectangle(layer->ivilayer, layer->x, layer->y,
                                                  layer->width, layer->height);
    assert(!ret);

    ret = ivi_layout_layer_set_visibility(layer->ivilayer, true);
    assert(!ret);
}

/**
 * Internal set notification
 */
static void
set_notification_create_surface(struct ivi_layout_surface *ivisurf,
                                void *userdata)
{
    struct hmi_controller* hmi_ctrl = userdata;
    struct ivi_layout_layer *application_layer = hmi_ctrl->application_layer.ivilayer;
    int32_t ret = 0;

    /* skip ui widgets */
    if (is_surf_in_ui_widget(hmi_ctrl, ivisurf)) {
        return;
    }

    ret = ivi_layout_layer_add_surface(application_layer, ivisurf);
    assert(!ret);
}

static void
set_notification_remove_surface(struct ivi_layout_surface *ivisurf,
                                void *userdata)
{
    (void)ivisurf;
    struct hmi_controller* hmi_ctrl = userdata;
    switch_mode(hmi_ctrl, hmi_ctrl->layout_mode);
}

static void
set_notification_configure_surface(struct ivi_layout_surface *ivisurf,
                                   void *userdata)
{
    (void)ivisurf;
    struct hmi_controller* hmi_ctrl = userdata;
    switch_mode(hmi_ctrl, hmi_ctrl->layout_mode);
}

/**
 * A hmi_controller used 4 ivi_layers to manage ivi_surfaces. The IDs of corresponding ivi_layer
 * are defined in weston.ini. Default scene graph of ivi_layers are initialized in
 * hmi_controller_create
 */
static struct hmi_server_setting *
hmi_server_setting_create(struct weston_compositor *ec)
{
    struct hmi_server_setting* setting = MEM_ALLOC(sizeof(*setting));

    struct weston_config *config = ec->config;

    struct weston_config_section *shell_section = NULL;
    shell_section = weston_config_get_section(config, "ivi-shell", NULL, NULL);

    weston_config_section_get_uint(
        shell_section, "base-layer-id", &setting->base_layer_id, 1000);

    weston_config_section_get_uint(
        shell_section, "workspace-background-layer-id", &setting->workspace_background_layer_id, 2000);

    weston_config_section_get_uint(
        shell_section, "workspace-layer-id", &setting->workspace_layer_id, 3000);

    weston_config_section_get_uint(
        shell_section, "application-layer-id", &setting->application_layer_id, 4000);

    weston_config_section_get_uint(
        shell_section, "transition-duration",
        &setting->transition_duration, 300);

    setting->panel_height = 70;

    weston_config_section_get_string(
            shell_section, "ivi-shell-user-interface", &setting->ivi_homescreen, NULL);

    return setting;
}

/**
 * This is a starting method called from module_init.
 * This sets up scene graph of ivi_layers; base, application, workspace background,
 * and workspace. These ivi_layers are created/added to ivi_screen in create_layer
 *
 * base: to group ivi_surfaces of panel and background
 * application: to group ivi_surfaces of ivi_applications
 * workspace background: to group a ivi_surface of background in workspace
 * workspace: to group ivi_surfaces for launching ivi_applications
 *
 * ivi_layers of workspace background and workspace is set to invisible at first.
 * The properties of it is updated with animation when ivi_hmi_controller_home is
 * requested.
 */
static struct hmi_controller *
hmi_controller_create(struct weston_compositor *ec)
{
    struct ivi_layout_screen **pp_screen = NULL;
    struct ivi_layout_screen *iviscrn  = NULL;
    int32_t screen_length  = 0;
    int32_t screen_width   = 0;
    int32_t screen_height  = 0;
    struct link_layer *tmp_link_layer = NULL;
    int32_t panel_height = 0;

    struct hmi_controller *hmi_ctrl = MEM_ALLOC(sizeof(*hmi_ctrl));
    wl_array_init(&hmi_ctrl->ui_widgets);
    hmi_ctrl->layout_mode = IVI_HMI_CONTROLLER_LAYOUT_MODE_TILING;
    hmi_ctrl->hmi_setting = hmi_server_setting_create(ec);
    hmi_ctrl->compositor = ec;

    ivi_layout_get_screens(&screen_length, &pp_screen);

    iviscrn = pp_screen[0];

    ivi_layout_get_screen_resolution(iviscrn, &screen_width, &screen_height);

    /* init base ivi_layer*/
    hmi_ctrl->base_layer.x = 0;
    hmi_ctrl->base_layer.y = 0;
    hmi_ctrl->base_layer.width = screen_width;
    hmi_ctrl->base_layer.height = screen_height;
    hmi_ctrl->base_layer.id_layer = hmi_ctrl->hmi_setting->base_layer_id;

    create_layer(iviscrn, &hmi_ctrl->base_layer);

    panel_height = hmi_ctrl->hmi_setting->panel_height;


    /* init application ivi_layer */
    hmi_ctrl->application_layer.x = 0;
    hmi_ctrl->application_layer.y = 0;
    hmi_ctrl->application_layer.width = screen_width;
    hmi_ctrl->application_layer.height = screen_height - panel_height;
    hmi_ctrl->application_layer.id_layer = hmi_ctrl->hmi_setting->application_layer_id;

    create_layer(iviscrn, &hmi_ctrl->application_layer);

    /* init workspace background ivi_layer */
    hmi_ctrl->workspace_background_layer.x = 0;
    hmi_ctrl->workspace_background_layer.y = 0;
    hmi_ctrl->workspace_background_layer.width = screen_width;
    hmi_ctrl->workspace_background_layer.height = screen_height - panel_height;

    hmi_ctrl->workspace_background_layer.id_layer =
        hmi_ctrl->hmi_setting->workspace_background_layer_id;

    create_layer(iviscrn, &hmi_ctrl->workspace_background_layer);
    ivi_layout_layer_set_opacity(hmi_ctrl->workspace_background_layer.ivilayer, 0);
    ivi_layout_layer_set_visibility(hmi_ctrl->workspace_background_layer.ivilayer, false);

    /* init workspace ivi_layer */
    hmi_ctrl->workspace_layer.x = hmi_ctrl->workspace_background_layer.x;
    hmi_ctrl->workspace_layer.y = hmi_ctrl->workspace_background_layer.y;
    hmi_ctrl->workspace_layer.width = hmi_ctrl->workspace_background_layer.width;
    hmi_ctrl->workspace_layer.height = hmi_ctrl->workspace_background_layer.height;
    hmi_ctrl->workspace_layer.id_layer = hmi_ctrl->hmi_setting->workspace_layer_id;

    create_layer(iviscrn, &hmi_ctrl->workspace_layer);
    ivi_layout_layer_set_opacity(hmi_ctrl->workspace_layer.ivilayer, 0);
    ivi_layout_layer_set_visibility(hmi_ctrl->workspace_layer.ivilayer, false);

    wl_list_init(&hmi_ctrl->workspace_fade.layer_list);
    tmp_link_layer = MEM_ALLOC(sizeof(*tmp_link_layer));
    tmp_link_layer->layout_layer = hmi_ctrl->workspace_layer.ivilayer;
    wl_list_insert(&hmi_ctrl->workspace_fade.layer_list, &tmp_link_layer->link);
    tmp_link_layer = MEM_ALLOC(sizeof(*tmp_link_layer));
    tmp_link_layer->layout_layer = hmi_ctrl->workspace_background_layer.ivilayer;
    wl_list_insert(&hmi_ctrl->workspace_fade.layer_list, &tmp_link_layer->link);

    ivi_layout_add_notification_create_surface(set_notification_create_surface, hmi_ctrl);
    ivi_layout_add_notification_remove_surface(set_notification_remove_surface, hmi_ctrl);
    ivi_layout_add_notification_configure_surface(set_notification_configure_surface, hmi_ctrl);

    wl_signal_add(&hmi_ctrl->compositor->destroy_signal, &hmi_ctrl->destroy_listener);

    free(pp_screen);
    pp_screen = NULL;

    return hmi_ctrl;
}

/**
 * Implementations of ivi-hmi-controller.xml
 */

/**
 * A ivi_surface drawing background is identified by id_surface.
 * Properties of the ivi_surface is set by using ivi_layout APIs according to
 * the scene graph of UI defined in hmi_controller_create.
 *
 * UI ivi_layer is used to add this ivi_surface.
 */
static void
ivi_hmi_controller_set_background(struct hmi_controller *hmi_ctrl,
                                  uint32_t id_surface)

{
    struct ivi_layout_surface *ivisurf = NULL;
    struct ivi_layout_layer   *ivilayer = hmi_ctrl->base_layer.ivilayer;
    const int32_t dstx = hmi_ctrl->application_layer.x;
    const int32_t dsty = hmi_ctrl->application_layer.y;
    const int32_t width  = hmi_ctrl->application_layer.width;
    const int32_t height = hmi_ctrl->application_layer.height;
    int32_t ret = 0;

    uint32_t *add_surface_id = wl_array_add(&hmi_ctrl->ui_widgets,
                                            sizeof(*add_surface_id));
    *add_surface_id = id_surface;

    ivisurf = ivi_layout_get_surface_from_id(id_surface);
    assert(ivisurf != NULL);

    ret = ivi_layout_layer_add_surface(ivilayer, ivisurf);
    assert(!ret);

    ret = ivi_layout_surface_set_destination_rectangle(ivisurf,
                                    dstx, dsty, width, height);
    assert(!ret);

    ret = ivi_layout_surface_set_visibility(ivisurf, true);
    assert(!ret);
}

/**
 * A ivi_surface drawing panel is identified by id_surface.
 * Properties of the ivi_surface is set by using ivi_layout APIs according to
 * the scene graph of UI defined in hmi_controller_create.
 *
 * UI ivi_layer is used to add this ivi_surface.
 */
static void
ivi_hmi_controller_set_panel(struct hmi_controller *hmi_ctrl,
                             uint32_t id_surface)
{
    struct ivi_layout_surface *ivisurf  = NULL;
    struct ivi_layout_layer   *ivilayer = hmi_ctrl->base_layer.ivilayer;
    const int32_t width  = hmi_ctrl->base_layer.width;
    int32_t ret = 0;
    int32_t panel_height = 0;
    const int32_t dstx = 0;
    int32_t dsty = 0;

    uint32_t *add_surface_id = wl_array_add(&hmi_ctrl->ui_widgets,
                                            sizeof(*add_surface_id));
    *add_surface_id = id_surface;

    ivisurf = ivi_layout_get_surface_from_id(id_surface);
    assert(ivisurf != NULL);

    ret = ivi_layout_layer_add_surface(ivilayer, ivisurf);
    assert(!ret);

    panel_height = hmi_ctrl->hmi_setting->panel_height;

    dsty = hmi_ctrl->base_layer.height - panel_height;

    ret = ivi_layout_surface_set_destination_rectangle(
                        ivisurf, dstx, dsty, width, panel_height);
    assert(!ret);

    ret = ivi_layout_surface_set_visibility(ivisurf, true);
    assert(!ret);
}

/**
 * A ivi_surface drawing buttons in panel is identified by id_surface. It can set
 * several buttons. Properties of the ivi_surface is set by using ivi_layout
 * APIs according to the scene graph of UI defined in hmi_controller_create.
 * Additionally, the position of it is shifted to right when new one is requested.
 *
 * UI ivi_layer is used to add these ivi_surfaces.
 */
static void
ivi_hmi_controller_set_button(struct hmi_controller *hmi_ctrl,
                              uint32_t id_surface, int32_t number)
{
    struct ivi_layout_surface *ivisurf  = NULL;
    struct ivi_layout_layer   *ivilayer = hmi_ctrl->base_layer.ivilayer;
    const int32_t width  = 48;
    const int32_t height = 48;
    int32_t ret = 0;
    int32_t panel_height = 0;
    int32_t dstx = 0;
    int32_t dsty = 0;
    uint32_t *add_surface_id = wl_array_add(&hmi_ctrl->ui_widgets,
                                            sizeof(*add_surface_id));
    *add_surface_id = id_surface;

    ivisurf = ivi_layout_get_surface_from_id(id_surface);
    assert(ivisurf != NULL);

    ret = ivi_layout_layer_add_surface(ivilayer, ivisurf);
    assert(!ret);

    panel_height = hmi_ctrl->hmi_setting->panel_height;

    dstx = (60 * number) + 15;
    dsty = (hmi_ctrl->base_layer.height - panel_height) + 5;

    ret = ivi_layout_surface_set_destination_rectangle(
                            ivisurf,dstx, dsty, width, height);
    assert(!ret);

    ret = ivi_layout_surface_set_visibility(ivisurf, true);
    assert(!ret);
}

/**
 * A ivi_surface drawing home button in panel is identified by id_surface.
 * Properties of the ivi_surface is set by using ivi_layout APIs according to
 * the scene graph of UI defined in hmi_controller_create.
 *
 * UI ivi_layer is used to add these ivi_surfaces.
 */
static void
ivi_hmi_controller_set_home_button(struct hmi_controller *hmi_ctrl,
                                   uint32_t id_surface)
{
    struct ivi_layout_surface *ivisurf  = NULL;
    struct ivi_layout_layer   *ivilayer = hmi_ctrl->base_layer.ivilayer;
    int32_t ret = 0;
    int32_t size = 48;
    int32_t panel_height = hmi_ctrl->hmi_setting->panel_height;
    const int32_t dstx = (hmi_ctrl->base_layer.width - size) / 2;
    const int32_t dsty = (hmi_ctrl->base_layer.height - panel_height) + 5;

    uint32_t *add_surface_id = wl_array_add(&hmi_ctrl->ui_widgets,
                                            sizeof(*add_surface_id));
    *add_surface_id = id_surface;

    ivisurf = ivi_layout_get_surface_from_id(id_surface);
    assert(ivisurf != NULL);

    ret = ivi_layout_layer_add_surface(ivilayer, ivisurf);
    assert(!ret);

    ret = ivi_layout_surface_set_destination_rectangle(
                    ivisurf, dstx, dsty, size, size);
    assert(!ret);

    ret = ivi_layout_surface_set_visibility(ivisurf, true);
    assert(!ret);
}

/**
 * A ivi_surface drawing background of workspace is identified by id_surface.
 * Properties of the ivi_surface is set by using ivi_layout APIs according to
 * the scene graph of UI defined in hmi_controller_create.
 *
 * A ivi_layer of workspace_background is used to add this ivi_surface.
 */
static void
ivi_hmi_controller_set_workspacebackground(struct hmi_controller *hmi_ctrl,
                                           uint32_t id_surface)
{
    struct ivi_layout_surface *ivisurf = NULL;
    struct ivi_layout_layer   *ivilayer = NULL;
    const int32_t width  = hmi_ctrl->workspace_background_layer.width;
    const int32_t height = hmi_ctrl->workspace_background_layer.height;
    int32_t ret = 0;

    uint32_t *add_surface_id = wl_array_add(&hmi_ctrl->ui_widgets,
                                            sizeof(*add_surface_id));
    *add_surface_id = id_surface;
    ivilayer = hmi_ctrl->workspace_background_layer.ivilayer;

    ivisurf = ivi_layout_get_surface_from_id(id_surface);
    assert(ivisurf != NULL);

    ret = ivi_layout_layer_add_surface(ivilayer, ivisurf);
    assert(!ret);

    ret = ivi_layout_surface_set_destination_rectangle(ivisurf,
                                    0, 0, width, height);
    assert(!ret);

    ret = ivi_layout_surface_set_visibility(ivisurf, true);
    assert(!ret);
}

/**
 * A list of ivi_surfaces drawing launchers in workspace is identified by id_surfaces.
 * Properties of the ivi_surface is set by using ivi_layout APIs according to
 * the scene graph of UI defined in hmi_controller_create.
 *
 * The workspace can have several pages to group ivi_surfaces of launcher. Each call
 * of this interface increments a number of page to add a group of ivi_surfaces
 */
static void
ivi_hmi_controller_add_launchers(struct wl_resource *resource,
                                 int32_t icon_size)
{
    struct hmi_controller *hmi_ctrl = wl_resource_get_user_data(resource);
    struct ivi_layout_layer *layer = hmi_ctrl->workspace_layer.ivilayer;
    int32_t minspace_x = 10;
    int32_t minspace_y = minspace_x;

    int32_t width  = hmi_ctrl->workspace_layer.width;
    int32_t height = hmi_ctrl->workspace_layer.height;

    int32_t x_count = (width - minspace_x) / (minspace_x + icon_size);
    int32_t space_x = (int32_t)((width - x_count * icon_size) / (1.0 + x_count));
    float fcell_size_x = icon_size + space_x;

    int32_t y_count = (height - minspace_y) / (minspace_y + icon_size);
    int32_t space_y = (int32_t)((height - y_count * icon_size) / (1.0 + y_count));
    float fcell_size_y = icon_size + space_y;

    struct weston_config *config = NULL;
    struct weston_config_section *section = NULL;
    const char *name = NULL;
    int launcher_count = 0;
    struct wl_array launchers;
    int32_t nx = 0;
    int32_t ny = 0;
    int32_t prev = -1;
    struct launcher_info *data = NULL;

    uint32_t surfaceid = 0;
    uint32_t workspaceid = 0;
    struct launcher_info *info = NULL;

    int32_t x = 0;
    int32_t y = 0;
    int32_t ret = 0;
    struct ivi_layout_surface* layout_surface = NULL;
    uint32_t *add_surface_id = NULL;

    if (0 == x_count) {
       x_count = 1;
    }

    if (0 == y_count) {
       y_count  = 1;
    }

    config = hmi_ctrl->compositor->config;
    if (!config) {
        return;
    }

    section = weston_config_get_section(config, "ivi-shell", NULL, NULL);
    if (!section) {
        return;
    }

    wl_array_init(&launchers);

    while (weston_config_next_section(config, &section, &name)) {
        surfaceid = 0;
        workspaceid = 0;
        info = NULL;
        if (0 != strcmp(name, "ivi-launcher")) {
            continue;
        }

        if (0 != weston_config_section_get_uint(section, "icon-id", &surfaceid, 0)) {
            continue;
        }

        if (0 != weston_config_section_get_uint(section, "workspace-id", &workspaceid, 0)) {
            continue;
        }

        info = wl_array_add(&launchers, sizeof(*info));

        if (info) {
            info->surface_id = surfaceid;
            info->workspace_id = workspaceid;
            info->index = launcher_count;
            ++launcher_count;
        }
    }

    qsort(launchers.data, launcher_count, sizeof(struct launcher_info), compare_launcher_info);

    wl_array_for_each(data, &launchers)
    {
        x = 0;
        y = 0;
        ret = 0;
        layout_surface = NULL;
        add_surface_id = wl_array_add(&hmi_ctrl->ui_widgets, sizeof(*add_surface_id));

        *add_surface_id = data->surface_id;

        if (0 > prev || (uint32_t)prev != data->workspace_id) {
            nx = 0;
            ny = 0;
            prev = data->workspace_id;

            if (0 <= prev) {
                hmi_ctrl->workspace_count++;
            }
        }

        if (y_count == ny) {
            ny = 0;
            hmi_ctrl->workspace_count++;
        }

        x = nx * fcell_size_x + (hmi_ctrl->workspace_count - 1) * width + space_x;
        y = ny * fcell_size_y  + space_y;

        layout_surface = ivi_layout_get_surface_from_id(data->surface_id);
        assert(layout_surface);

        ret = ivi_layout_layer_add_surface(layer, layout_surface);
        assert(!ret);

        ret = ivi_layout_surface_set_destination_rectangle(
                        layout_surface, x, y, icon_size, icon_size);
        assert(!ret);

        ret = ivi_layout_surface_set_visibility(layout_surface, true);
        assert(!ret);

        nx++;

        if (x_count == nx) {
            ny++;
            nx = 0;
        }
    }

    wl_array_release(&launchers);
    ivi_layout_commit_changes();
}

static void
ivi_hmi_controller_UI_ready(struct wl_client *client,
                            struct wl_resource *resource)
{
    struct setting {
        uint32_t background_id;
        uint32_t panel_id;
        uint32_t tiling_id;
        uint32_t sidebyside_id;
        uint32_t fullscreen_id;
        uint32_t random_id;
        uint32_t home_id;
        uint32_t workspace_background_id;
    };

    struct config_command {
        char *key;
        uint32_t *dest;
    };

    struct hmi_controller *hmi_ctrl = wl_resource_get_user_data(resource);
    struct weston_config *config = hmi_ctrl->compositor->config;
    struct weston_config_section *section = NULL;
    struct setting dest;
    int result = 0;
    int i = 0;

    const struct config_command uint_commands[] = {
        { "background-id", &dest.background_id },
        { "panel-id", &dest.panel_id },
        { "tiling-id", &dest.tiling_id },
        { "sidebyside-id", &dest.sidebyside_id },
        { "fullscreen-id", &dest.fullscreen_id },
        { "random-id", &dest.random_id },
        { "home-id", &dest.home_id },
        { "workspace-background-id", &dest.workspace_background_id },
        { NULL, NULL }
    };

    section = weston_config_get_section(config, "ivi-shell", NULL, NULL);

    for (i = 0; -1 != result; ++i)
    {
        const struct config_command *command = &uint_commands[i];

        if (!command->key)
        {
            break;
        }

        if (weston_config_section_get_uint(
                    section, command->key, command->dest, 0) != 0)
        {
            result = -1;
        }
    }

    if (-1 != result)
    {
        ivi_hmi_controller_set_background(hmi_ctrl, dest.background_id);
        ivi_hmi_controller_set_panel(hmi_ctrl, dest.panel_id);
        ivi_hmi_controller_set_button(hmi_ctrl, dest.tiling_id, 0);
        ivi_hmi_controller_set_button(hmi_ctrl, dest.sidebyside_id, 1);
        ivi_hmi_controller_set_button(hmi_ctrl, dest.fullscreen_id, 2);
        ivi_hmi_controller_set_button(hmi_ctrl, dest.random_id, 3);
        ivi_hmi_controller_set_home_button(hmi_ctrl, dest.home_id);
        ivi_hmi_controller_set_workspacebackground(hmi_ctrl, dest.workspace_background_id);
        ivi_layout_commit_changes();

        ivi_hmi_controller_add_launchers(resource, 256);
        hmi_ctrl->is_initialized = 1;
    }
}

/**
 * Implementation of request and event of ivi_hmi_controller_workspace_control
 * and controlling workspace.
 *
 * When motion of input is detected in a ivi_surface of workspace background,
 * ivi_hmi_controller_workspace_control shall be invoked and to start controlling of
 * workspace. The workspace has several pages to show several groups of applications.
 * The workspace is slid by using ivi-layout to select a a page in layer_set_pos
 * according to motion. When motion finished, e.g. touch up detected, control is
 * terminated and event:ivi_hmi_controller_workspace_control is notified.
 */
struct pointer_grab {
    struct weston_pointer_grab grab;
    struct ivi_layout_layer *layer;
    struct wl_resource *resource;
};

struct touch_grab {
    struct weston_touch_grab grab;
    struct ivi_layout_layer *layer;
    struct wl_resource *resource;
};

struct move_grab {
    wl_fixed_t dst[2];
    wl_fixed_t rgn[2][2];
    double v[2];
    struct timespec start_time;
    struct timespec pre_time;
    wl_fixed_t start_pos[2];
    wl_fixed_t pos[2];
    int32_t is_moved;
};

struct pointer_move_grab {
    struct pointer_grab base;
    struct move_grab move;
};

struct touch_move_grab {
    struct touch_grab base;
    struct move_grab move;
    int32_t is_active;
};

static void
pointer_grab_start(struct pointer_grab *grab,
                   struct ivi_layout_layer *layer,
                   const struct weston_pointer_grab_interface *interface,
                   struct weston_pointer *pointer)
{
    grab->grab.interface = interface;
    grab->layer = layer;
    weston_pointer_start_grab(pointer, &grab->grab);
}

static void
touch_grab_start(struct touch_grab *grab,
                 struct ivi_layout_layer *layer,
                 const struct weston_touch_grab_interface *interface,
                 struct weston_touch* touch)
{
    grab->grab.interface = interface;
    grab->layer = layer;
    weston_touch_start_grab(touch, &grab->grab);
}

static int32_t
clamp(int32_t val, int32_t min, int32_t max)
{
    if (val < min) {
        return min;
    }

    if (max < val) {
        return max;
    }

    return val;
}

static void
move_workspace_grab_end(struct move_grab *move, struct wl_resource* resource,
                        wl_fixed_t grab_x, struct ivi_layout_layer *layer)
{
    struct hmi_controller *hmi_ctrl = wl_resource_get_user_data(resource);
    int32_t width = hmi_ctrl->workspace_background_layer.width;

    struct timespec time = {0};
    clock_gettime(CLOCK_MONOTONIC, &time);

    double  grab_time = 1e+3 * (time.tv_sec  - move->start_time.tv_sec) +
                        1e-6 * (time.tv_nsec - move->start_time.tv_nsec);

    double  from_motion_time = 1e+3 * (time.tv_sec  - move->pre_time.tv_sec) +
                               1e-6 * (time.tv_nsec - move->pre_time.tv_nsec);

    double pointer_v = move->v[0];

    int32_t is_flick = grab_time < 400 &&
                       0.4 < fabs(pointer_v);

    int32_t pos_x = 0;
    int32_t pos_y = 0;
    int page_no = 0;
    double end_pos = 0.0;
    uint32_t duration = 0;

    if (200 < from_motion_time) {
       pointer_v = 0.0;
    }

    ivi_layout_layer_get_position(layer, &pos_x, &pos_y);


    if (is_flick) {
        int orgx = wl_fixed_to_int(move->dst[0] + grab_x);
        page_no = (-orgx + width / 2) / width;

        if (pointer_v < 0.0) {
            page_no++;
        }else {
            page_no--;
        }
    }else {
        page_no = (-pos_x + width / 2) / width;
    }

    page_no = clamp(page_no, 0, hmi_ctrl->workspace_count - 1);
    end_pos = -page_no * width;

    duration = hmi_ctrl->hmi_setting->transition_duration;
    ivi_hmi_controller_send_workspace_end_control(resource, move->is_moved);
    ivi_layout_layer_set_transition(layer,IVI_LAYOUT_TRANSITION_LAYER_MOVE,duration);
    ivi_layout_layer_set_destination_rectangle(layer,
            end_pos, pos_y,
            hmi_ctrl->workspace_background_layer.width,
            hmi_ctrl->workspace_background_layer.height);
    ivi_layout_commit_changes();
}

static void
pointer_move_workspace_grab_end(struct pointer_grab *grab)
{
    struct pointer_move_grab *pnt_move_grab = (struct pointer_move_grab *) grab;
    struct ivi_layout_layer *layer = pnt_move_grab->base.layer;

    move_workspace_grab_end(&pnt_move_grab->move, grab->resource,
                            grab->grab.pointer->grab_x, layer);

    weston_pointer_end_grab(grab->grab.pointer);
}

static void
touch_move_workspace_grab_end(struct touch_grab *grab)
{
    struct touch_move_grab *tch_move_grab = (struct touch_move_grab *) grab;
    struct ivi_layout_layer *layer = tch_move_grab->base.layer;

    move_workspace_grab_end(&tch_move_grab->move, grab->resource,
                            grab->grab.touch->grab_x, layer);

    weston_touch_end_grab(grab->grab.touch);
}

static void
pointer_noop_grab_focus(struct weston_pointer_grab *grab)
{
}

static void
move_grab_update(struct move_grab *move, wl_fixed_t pointer[2])
{
    struct timespec timestamp = {0};
    int32_t ii = 0;
    double dt = 0.0;

    clock_gettime(CLOCK_MONOTONIC, &timestamp); //FIXME
    dt = (1e+3 * (timestamp.tv_sec  - move->pre_time.tv_sec) +
          1e-6 * (timestamp.tv_nsec - move->pre_time.tv_nsec));

    if (dt < 1e-6) {
        dt = 1e-6;
    }

    move->pre_time = timestamp;

    for (ii = 0; ii < 2; ii++) {
        wl_fixed_t prepos = move->pos[ii];
        move->pos[ii] = pointer[ii] + move->dst[ii];

        if (move->pos[ii] < move->rgn[0][ii]) {
            move->pos[ii] = move->rgn[0][ii];
            move->dst[ii] = move->pos[ii] - pointer[ii];
        } else if (move->rgn[1][ii] < move->pos[ii]) {
            move->pos[ii] = move->rgn[1][ii];
            move->dst[ii] = move->pos[ii] - pointer[ii];
        }

        move->v[ii] = wl_fixed_to_double(move->pos[ii] - prepos) / dt;

        if (!move->is_moved &&
            0 < wl_fixed_to_int(move->pos[ii] - move->start_pos[ii])) {
            move->is_moved = 1;
        }
    }
}

static void
layer_set_pos(struct ivi_layout_layer *layer, wl_fixed_t pos_x, wl_fixed_t pos_y)
{
    int32_t layout_pos_x = 0;
    int32_t layout_pos_y = 0;

    layout_pos_x = wl_fixed_to_int(pos_x);
    layout_pos_y = wl_fixed_to_int(pos_y);
    ivi_layout_layer_set_position(layer, layout_pos_x, layout_pos_y);
    ivi_layout_commit_changes();
}

static void
pointer_move_grab_motion(struct weston_pointer_grab *grab, uint32_t time,
                         wl_fixed_t x, wl_fixed_t y)
{
    struct pointer_move_grab *pnt_move_grab = (struct pointer_move_grab *) grab;
    wl_fixed_t pointer_pos[2] = {x, y};
    move_grab_update(&pnt_move_grab->move, pointer_pos);
    layer_set_pos(pnt_move_grab->base.layer,
                  pnt_move_grab->move.pos[0], pnt_move_grab->move.pos[1]);
    weston_pointer_move(pnt_move_grab->base.grab.pointer, x, y);
}

static void
touch_move_grab_motion(struct weston_touch_grab *grab, uint32_t time,
                       int touch_id, wl_fixed_t x, wl_fixed_t y)
{
    struct touch_move_grab *tch_move_grab = (struct touch_move_grab *) grab;

    if (!tch_move_grab->is_active) {
        return;
    }

    wl_fixed_t pointer_pos[2] = {grab->touch->grab_x, grab->touch->grab_y};
    move_grab_update(&tch_move_grab->move, pointer_pos);
    layer_set_pos(tch_move_grab->base.layer,
                  tch_move_grab->move.pos[0], tch_move_grab->move.pos[1]);
}

static void
pointer_move_workspace_grab_button(struct weston_pointer_grab *grab,
                                   uint32_t time, uint32_t button,
                                   uint32_t state_w)
{
    if (BTN_LEFT == button &&
        WL_POINTER_BUTTON_STATE_RELEASED == state_w) {
        struct pointer_grab *pg = (struct pointer_grab *)grab;
        pointer_move_workspace_grab_end(pg);
        free(grab);
    }
}

static void
touch_nope_grab_down(struct weston_touch_grab *grab, uint32_t time,
                     int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
touch_move_workspace_grab_up(struct weston_touch_grab *grab, uint32_t time, int touch_id)
{
    struct touch_move_grab *tch_move_grab = (struct touch_move_grab *)grab;

    if (0 == touch_id) {
        tch_move_grab->is_active = 0;
    }

    if (0 == grab->touch->num_tp) {
        touch_move_workspace_grab_end(&tch_move_grab->base);
        free(grab);
    }
}

static void
pointer_move_workspace_grab_cancel(struct weston_pointer_grab *grab)
{
    struct pointer_grab *pg = (struct pointer_grab *)grab;
    pointer_move_workspace_grab_end(pg);
    free(grab);
}

static void
touch_move_workspace_grab_cancel(struct weston_touch_grab *grab)
{
    struct touch_grab *tg = (struct touch_grab *)grab;
    touch_move_workspace_grab_end(tg);
    free(grab);
}

static const struct weston_pointer_grab_interface pointer_move_grab_workspace_interface = {
    pointer_noop_grab_focus,
    pointer_move_grab_motion,
    pointer_move_workspace_grab_button,
    pointer_move_workspace_grab_cancel
};

static const struct weston_touch_grab_interface touch_move_grab_workspace_interface = {
    touch_nope_grab_down,
    touch_move_workspace_grab_up,
    touch_move_grab_motion,
    touch_move_workspace_grab_cancel
};

enum HMI_GRAB_DEVICE
{
    HMI_GRAB_DEVICE_NONE,
    HMI_GRAB_DEVICE_POINTER,
    HMI_GRAB_DEVICE_TOUCH
};

static enum HMI_GRAB_DEVICE
get_hmi_grab_device(struct weston_seat *seat, uint32_t serial)
{
    if (seat->pointer &&
        seat->pointer->focus &&
        seat->pointer->button_count &&
        seat->pointer->grab_serial == serial) {
        return HMI_GRAB_DEVICE_POINTER;
    }

    if (seat->touch &&
        seat->touch->focus &&
        seat->touch->grab_serial == serial) {
        return HMI_GRAB_DEVICE_TOUCH;
    }

    return HMI_GRAB_DEVICE_NONE;
}

static void
move_grab_init(struct move_grab* move, wl_fixed_t start_pos[2],
               wl_fixed_t grab_pos[2], wl_fixed_t rgn[2][2],
               struct wl_resource* resource)
{
    clock_gettime(CLOCK_MONOTONIC, &move->start_time); //FIXME
    move->pre_time = move->start_time;
    move->pos[0] = start_pos[0];
    move->pos[1] = start_pos[1];
    move->start_pos[0] = start_pos[0];
    move->start_pos[1] = start_pos[1];
    move->dst[0] = start_pos[0] - grab_pos[0];
    move->dst[1] = start_pos[1] - grab_pos[1];
    memcpy(move->rgn, rgn, sizeof(move->rgn));
}

static void
move_grab_init_workspace(struct move_grab* move,
                         wl_fixed_t grab_x, wl_fixed_t grab_y,
                         struct wl_resource *resource)
{
    struct hmi_controller *hmi_ctrl = wl_resource_get_user_data(resource);
    struct ivi_layout_layer *layer = hmi_ctrl->workspace_layer.ivilayer;
    int32_t workspace_count = hmi_ctrl->workspace_count;
    int32_t workspace_width = hmi_ctrl->workspace_background_layer.width;
    int32_t layer_pos_x = 0;
    int32_t layer_pos_y = 0;
    wl_fixed_t start_pos[2] = {0};
    wl_fixed_t rgn[2][2] = {{0}};
    wl_fixed_t grab_pos[2] = {grab_x, grab_y};

    ivi_layout_layer_get_position(layer, &layer_pos_x, &layer_pos_y);

    start_pos[0] = wl_fixed_from_int(layer_pos_x);
    start_pos[1] = wl_fixed_from_int(layer_pos_y);

    rgn[0][0] = wl_fixed_from_int(-workspace_width * (workspace_count - 1));

    rgn[0][1] = wl_fixed_from_int(0);
    rgn[1][0] = wl_fixed_from_int(0);
    rgn[1][1] = wl_fixed_from_int(0);

    move_grab_init(move, start_pos, grab_pos, rgn, resource);
}

static struct pointer_move_grab *
create_workspace_pointer_move(struct weston_pointer *pointer, struct wl_resource* resource)
{
    struct pointer_move_grab *pnt_move_grab = MEM_ALLOC(sizeof(*pnt_move_grab));
    pnt_move_grab->base.resource = resource;
    move_grab_init_workspace(&pnt_move_grab->move, pointer->grab_x, pointer->grab_y, resource);
    return pnt_move_grab;
}

static struct touch_move_grab *
create_workspace_touch_move(struct weston_touch *touch, struct wl_resource* resource)
{
    struct touch_move_grab *tch_move_grab = MEM_ALLOC(sizeof(*tch_move_grab));
    tch_move_grab->base.resource = resource;
    tch_move_grab->is_active = 1;
    move_grab_init_workspace(&tch_move_grab->move, touch->grab_x,touch->grab_y, resource);
    return tch_move_grab;
}

static void
ivi_hmi_controller_workspace_control(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *seat_resource,
                                     uint32_t serial)
{
    struct hmi_controller *hmi_ctrl = wl_resource_get_user_data(resource);
    struct ivi_layout_layer *layer = NULL;
    struct pointer_move_grab *pnt_move_grab = NULL;
    struct touch_move_grab *tch_move_grab = NULL;
    struct weston_seat* seat = NULL;
    enum HMI_GRAB_DEVICE device;

    if (hmi_ctrl->workspace_count < 2) {
        return;
    }

    seat = wl_resource_get_user_data(seat_resource);
    device = get_hmi_grab_device(seat, serial);

    if (HMI_GRAB_DEVICE_POINTER != device &&
        HMI_GRAB_DEVICE_TOUCH != device) {
        return;
    }

    layer = hmi_ctrl->workspace_layer.ivilayer;

    ivi_layout_transition_move_layer_cancel(layer);

    switch (device) {
    case HMI_GRAB_DEVICE_POINTER:
        pnt_move_grab = create_workspace_pointer_move(seat->pointer, resource);

        pointer_grab_start(
            &pnt_move_grab->base, layer, &pointer_move_grab_workspace_interface,
            seat->pointer);
        break;

    case HMI_GRAB_DEVICE_TOUCH:
        tch_move_grab = create_workspace_touch_move(seat->touch, resource);

        touch_grab_start(
            &tch_move_grab->base, layer, &touch_move_grab_workspace_interface,
            seat->touch);
        break;

    default:
        break;
    }
}

/**
 * Implementation of switch_mode
 */
static void
ivi_hmi_controller_switch_mode(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t  layout_mode)
{
    struct hmi_controller *hmi_ctrl = wl_resource_get_user_data(resource);
    switch_mode(hmi_ctrl, layout_mode);
}

/**
 * Implementation of on/off displaying workspace and workspace background ivi_layers.
 */
static void
ivi_hmi_controller_home(struct wl_client *client,
                        struct wl_resource *resource,
                        uint32_t home)
{
    struct hmi_controller *hmi_ctrl = wl_resource_get_user_data(resource);
    uint32_t is_fade_in;

    if ((IVI_HMI_CONTROLLER_HOME_ON  == home && !hmi_ctrl->workspace_fade.is_fade_in) ||
        (IVI_HMI_CONTROLLER_HOME_OFF == home && hmi_ctrl->workspace_fade.is_fade_in)) {

        is_fade_in = !hmi_ctrl->workspace_fade.is_fade_in;
        hmi_controller_fade_run(hmi_ctrl, is_fade_in, &hmi_ctrl->workspace_fade);
    }

    ivi_layout_commit_changes();
}

/**
 * binding ivi-hmi-controller implementation
 */
static const struct ivi_hmi_controller_interface ivi_hmi_controller_implementation = {
    ivi_hmi_controller_UI_ready,
    ivi_hmi_controller_workspace_control,
    ivi_hmi_controller_switch_mode,
    ivi_hmi_controller_home
};

static void
unbind_hmi_controller(struct wl_resource *resource)
{
}

static void
bind_hmi_controller(struct wl_client *client,
           void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource = NULL;
    struct hmi_controller *hmi_ctrl = data;

    resource = wl_resource_create(
            client, &ivi_hmi_controller_interface, 1, id);

    wl_resource_set_implementation(
            resource, &ivi_hmi_controller_implementation,
            hmi_ctrl, unbind_hmi_controller);
}

static void
launch_hmi_client_process(void *data)
{
    struct hmi_controller *hmi_ctrl =
        (struct hmi_controller *)data;

    weston_client_start(hmi_ctrl->compositor, hmi_ctrl->hmi_setting->ivi_homescreen);

    free(hmi_ctrl->hmi_setting->ivi_homescreen);
}

/*****************************************************************************
 *  exported functions
 ****************************************************************************/

WL_EXPORT int
module_init(struct weston_compositor *ec,
            int *argc, char *argv[])
{
    struct hmi_controller *hmi_ctrl = hmi_controller_create(ec);
    struct wl_event_loop *loop = NULL;

    if (wl_global_create(ec->wl_display,
                 &ivi_hmi_controller_interface, 1,
                 hmi_ctrl, bind_hmi_controller) == NULL) {
        return -1;
    }

    loop = wl_display_get_event_loop(ec->wl_display);
    wl_event_loop_add_idle(loop, launch_hmi_client_process, hmi_ctrl);

    return 0;
}
