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
#include <assert.h>

#include "compositor.h"
#include "weston-layout.h"
#include "hmi-controller.h"

#define ID_DESKTOP_LAYER     2000
#define ID_APPLICATION_LAYER 3000

#define PANEL_HEIGHT  70

/*****************************************************************************
 *  structure, globals
 ****************************************************************************/
struct hmi_controller_layer {
    struct weston_layout_layer  *ivilayer;
    uint32_t id_layer;

    uint32_t mode;

    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct hmi_controller_layer g_DesktopLayer = {0};
struct hmi_controller_layer g_ApplicationLayer = {0};

/*****************************************************************************
 *  globals
 ****************************************************************************/

void
hmi_controller_set_background(uint32_t id_surface)
{
    struct weston_layout_surface *ivisurf = NULL;
    struct weston_layout_layer   *ivilayer = g_DesktopLayer.ivilayer;
    const uint32_t width  = g_DesktopLayer.width;
    const uint32_t height = g_DesktopLayer.height - PANEL_HEIGHT;
    uint32_t ret = 0;

    ivisurf = weston_layout_getSurfaceFromId(id_surface);
    assert(ivisurf != NULL);

    ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
    assert(!ret);

    ret = weston_layout_surfaceSetDestinationRectangle(ivisurf,
                                    0, PANEL_HEIGHT, width, height);
    assert(!ret);

    ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
    assert(!ret);

    weston_layout_commitChanges();
}

void
hmi_controller_set_panel(uint32_t id_surface)
{
    struct weston_layout_surface *ivisurf  = NULL;
    struct weston_layout_layer   *ivilayer = g_DesktopLayer.ivilayer;
    const uint32_t width  = g_DesktopLayer.width;
    uint32_t ret = 0;

    ivisurf = weston_layout_getSurfaceFromId(id_surface);
    assert(ivisurf != NULL);

    ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
    assert(!ret);

    ret = weston_layout_surfaceSetDestinationRectangle(ivisurf,
                                    0, 0, width, PANEL_HEIGHT);
    assert(!ret);

    ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
    assert(!ret);

    weston_layout_commitChanges();
}

void
hmi_controller_set_button(uint32_t id_surface, uint32_t number)
{
    struct weston_layout_surface *ivisurf  = NULL;
    struct weston_layout_layer   *ivilayer = g_DesktopLayer.ivilayer;
    const uint32_t width  = 48;
    const uint32_t height = 48;
    uint32_t ret = 0;

    ivisurf = weston_layout_getSurfaceFromId(id_surface);
    assert(ivisurf != NULL);

    ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
    assert(!ret);

    ret = weston_layout_surfaceSetDestinationRectangle(ivisurf,
                                    ((60 * number) + 15), 5, width, height);
    assert(!ret);

    ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
    assert(!ret);

    weston_layout_commitChanges();
}

/*****************************************************************************
 *  local functions
 ****************************************************************************/
static int32_t
is_surf_in_desktopWidget(struct weston_layout_surface *ivisurf)
{
    uint32_t id = weston_layout_getIdOfSurface(ivisurf);
    if (id == ID_SURF_BACKGROUND      ||
        id == ID_SURF_PANEL           ||
        id == MODE_DIVIDED_INTO_EIGHT ||
        id == MODE_DIVIDED_INTO_TWO   ||
        id == MODE_MAXIMIZE_SOMEONE   ||
        id == MODE_RANDOM_REPLACE     ) {
        return 1;
    }
    return 0;
}


static void
mode_divided_into_eight(weston_layout_surface_ptr *ppSurface, uint32_t surface_length,
                        struct hmi_controller_layer *layer)
{
    const float surface_width  = (float)layer->width * 0.25;
    const float surface_height = (float)layer->height * 0.5;
    float surface_x = 0.0f;
    float surface_y = 0.0f;
    struct weston_layout_surface *ivisurf  = NULL;
    int32_t ret = 0;

    uint32_t i = 0;
    uint32_t num = 1;
    for (i = 0; i < surface_length; i++) {
        ivisurf = ppSurface[i];

        /* skip desktop widgets */
        if (is_surf_in_desktopWidget(ivisurf)) {
            continue;
        }

        if (num <= 8) {
            if (num < 5) {
                surface_x = (num - 1) * (surface_width);
                surface_y = 0.0f;
            }
            else {
                surface_x = (num - 5) * (surface_width);
                surface_y = surface_height;
            }
            ret = weston_layout_surfaceSetDestinationRectangle(ivisurf, surface_x, surface_y,
                                                            surface_width, surface_height);
            assert(!ret);

            ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
            assert(!ret);

            num++;
            continue;
        }

        ret = weston_layout_surfaceSetVisibility(ivisurf, 0);
        assert(!ret);
    }
}

static void
mode_divided_into_two(weston_layout_surface_ptr *ppSurface, uint32_t surface_length,
                      struct hmi_controller_layer *layer)
{
    float surface_width  = (float)layer->width * 0.5f;
    float surface_height = (float)layer->height;
    struct weston_layout_surface *ivisurf  = NULL;
    int32_t ret = 0;

    uint32_t i = 0;
    uint32_t num = 1;
    for (i = 0; i < surface_length; i++) {
        ivisurf = ppSurface[i];

        /* skip desktop widgets */
        if (is_surf_in_desktopWidget(ivisurf)) {
            continue;
        }

        if (num == 1) {
            ret = weston_layout_surfaceSetDestinationRectangle(ivisurf, 0, 0,
                                                surface_width, surface_height);
            assert(!ret);

            ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
            assert(!ret);

            num++;
            continue;
        }
        else if (num == 2) {
            ret = weston_layout_surfaceSetDestinationRectangle(ivisurf, surface_width, 0,
                                                surface_width, surface_height);
            assert(!ret);

            ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
            assert(!ret);

            num++;
            continue;
        }

        weston_layout_surfaceSetVisibility(ivisurf, 0);
        assert(!ret);
    }
}

static void
mode_maximize_someone(weston_layout_surface_ptr *ppSurface, uint32_t surface_length,
                      struct hmi_controller_layer *layer)
{
    const float surface_width  = (float)layer->width;
    const float surface_height = (float)layer->height;
    struct weston_layout_surface *ivisurf  = NULL;
    int32_t ret = 0;

    uint32_t i = 0;
    for (i = 0; i < surface_length; i++) {
        ivisurf = ppSurface[i];

        /* skip desktop widgets */
        if (is_surf_in_desktopWidget(ivisurf)) {
            continue;
        }

        ret = weston_layout_surfaceSetDestinationRectangle(ivisurf, 0, 0,
                                            surface_width, surface_height);
        assert(!ret);

        ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
        assert(!ret);
    }
}

static void
mode_random_replace(weston_layout_surface_ptr *ppSurface, uint32_t surface_length,
                    struct hmi_controller_layer *layer)
{
    const float surface_width  = (float)layer->width * 0.25f;
    const float surface_height = (float)layer->height * 0.25;
    float surface_x = 0.0f;
    float surface_y = 0.0f;
    struct weston_layout_surface *ivisurf  = NULL;
    int32_t ret = 0;

    uint32_t i = 0;
    for (i = 0; i < surface_length; i++) {
        ivisurf = ppSurface[i];

        /* skip desktop widgets */
        if (is_surf_in_desktopWidget(ivisurf)) {
            continue;
        }

        surface_x = rand() % 800;
        surface_y = rand() % 500;

        ret = weston_layout_surfaceSetDestinationRectangle(ivisurf, surface_x, surface_y,
                                                  surface_width, surface_height);
        assert(!ret);

        ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
        assert(!ret);
    }
}

static int32_t
has_applicatipn_surface(weston_layout_surface_ptr *ppSurface,
                        uint32_t surface_length)
{
    struct weston_layout_surface *ivisurf  = NULL;
    uint32_t i = 0;

    for (i = 0; i < surface_length; i++) {
        ivisurf = ppSurface[i];

        /* skip desktop widgets */
        if (is_surf_in_desktopWidget(ivisurf)) {
            continue;
        }

        return 1;
    }

    return 0;
}

static void
switch_mode(uint32_t id_surface)
{
    struct hmi_controller_layer *layer = &g_ApplicationLayer;
    weston_layout_surface_ptr  *ppSurface = NULL;
    uint32_t surface_length = 0;
    int32_t ret = 0;

    layer->mode = id_surface;

    ret = weston_layout_getSurfaces(&surface_length, &ppSurface);
    assert(!ret);

    if (!has_applicatipn_surface(ppSurface, surface_length)) {
        free(ppSurface);
        ppSurface = NULL;
        return;
    }

    do {
        if (id_surface == MODE_DIVIDED_INTO_EIGHT) {
            mode_divided_into_eight(ppSurface, surface_length, layer);
            break;
        }
        if (id_surface == MODE_DIVIDED_INTO_TWO) {
            mode_divided_into_two(ppSurface, surface_length, layer);
            break;
        }
        if (id_surface == MODE_MAXIMIZE_SOMEONE) {
            mode_maximize_someone(ppSurface, surface_length, layer);
            break;
        }
        if (id_surface == MODE_RANDOM_REPLACE) {
            mode_random_replace(ppSurface, surface_length, layer);
            break;
        }
    } while(0);

    weston_layout_commitChanges();

    free(ppSurface);
    ppSurface = NULL;

    return;
}

static void
create_layer(struct weston_layout_screen  *iviscrn,
             struct hmi_controller_layer *layer)
{
    int32_t ret = 0;

    layer->ivilayer = weston_layout_layerCreateWithDimension(layer->id_layer,
                                                layer->width, layer->height);
    assert(layer->ivilayer != NULL);

    ret = weston_layout_screenAddLayer(iviscrn, layer->ivilayer);
    assert(!ret);

    ret = weston_layout_layerSetDestinationRectangle(layer->ivilayer, layer->x, layer->y,
                                                  layer->width, layer->height);
    assert(!ret);

    ret = weston_layout_layerSetVisibility(layer->ivilayer, 1);
    assert(!ret);
}

static void
set_notification_create_surface(struct weston_layout_surface *ivisurf,
                                void *userdata)
{
    struct hmi_controller_layer *layer = &g_ApplicationLayer;
    int32_t ret = 0;
    (void)userdata;

    /* skip desktop widgets */
    if (is_surf_in_desktopWidget(ivisurf)) {
        return;
    }

    ret = weston_layout_layerAddSurface(layer->ivilayer, ivisurf);
    assert(!ret);
}

static void
set_notification_remove_surface(struct weston_layout_surface *ivisurf,
                                void *userdata)
{
    struct hmi_controller_layer *layer = &g_ApplicationLayer;
    (void)userdata;

    switch_mode(layer->mode);
}

static void
set_notification_configure_surface(struct weston_layout_surface *ivisurf,
                                void *userdata)
{
    struct hmi_controller_layer *layer = &g_ApplicationLayer;
    (void)ivisurf;
    (void)userdata;

    switch_mode(layer->mode);
}

static void
init_hmi_controller(void)
{
    weston_layout_screen_ptr    *ppScreen = NULL;
    struct weston_layout_screen *iviscrn  = NULL;
    uint32_t screen_length  = 0;
    uint32_t screen_width   = 0;
    uint32_t screen_height  = 0;
    int32_t ret = 0;

    weston_layout_getScreens(&screen_length, &ppScreen);

    iviscrn = ppScreen[0];

    weston_layout_getScreenResolution(iviscrn, &screen_width, &screen_height);
    assert(!ret);

    /* init desktop layer*/
    g_DesktopLayer.x = 0;
    g_DesktopLayer.y = 0;
    g_DesktopLayer.width  = screen_width;
    g_DesktopLayer.height = screen_height;
    g_DesktopLayer.id_layer = ID_DESKTOP_LAYER;

    create_layer(iviscrn, &g_DesktopLayer);

    /* init application layer */
    g_ApplicationLayer.x = 0;
    g_ApplicationLayer.y = PANEL_HEIGHT;
    g_ApplicationLayer.width  = screen_width;
    g_ApplicationLayer.height = screen_height - PANEL_HEIGHT;
    g_ApplicationLayer.id_layer = ID_APPLICATION_LAYER;
    g_ApplicationLayer.mode = MODE_DIVIDED_INTO_EIGHT;

    create_layer(iviscrn, &g_ApplicationLayer);

    weston_layout_setNotificationCreateSurface(set_notification_create_surface, NULL);
    weston_layout_setNotificationRemoveSurface(set_notification_remove_surface, NULL);
    weston_layout_setNotificationConfigureSurface(set_notification_configure_surface, NULL);

    free(ppScreen);
    ppScreen = NULL;
}

/*****************************************************************************
 *  exported functions
 ****************************************************************************/
WL_EXPORT void
pointer_button_event(uint32_t id_surface)
{
    switch_mode(id_surface);
}

WL_EXPORT int
module_init(struct weston_compositor *ec,
            int *argc, char *argv[])
{
    printf("DEBUG >>>> module_init in hmi-controller\n");
    init_hmi_controller();

    hmi_client_start();

    return 0;
}
