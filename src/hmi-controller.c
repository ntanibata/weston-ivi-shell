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

#define ID_LAYER_BACKGROUND 2000
#define ID_LAYER_PANEL      2100
#define ID_LAYER_BUTTON     2200
#define ID_APPLAYER         3000

#define PANEL_HEIGHT  70

/*****************************************************************************
 *  globals
 ****************************************************************************/

void
hmi_create_background(uint32_t id_surface)
{
    struct weston_layout_surface *ivisurf = NULL;
    struct weston_layout_layer *ivilayer = NULL;
    weston_layout_screen_ptr *ppScreen = NULL;
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t length = 0;

    weston_layout_getScreens(&length, &ppScreen);

    weston_layout_getScreenResolution(ppScreen[0], &width, &height);

    ivilayer = weston_layout_layerCreateWithDimension(ID_LAYER_BACKGROUND, width, height - PANEL_HEIGHT);

    weston_layout_screenAddLayer(ppScreen[0], ivilayer);

    weston_layout_layerSetVisibility(ivilayer, 1);

    ivisurf = weston_layout_getSurfaceFromId(id_surface);

    weston_layout_layerAddSurface(ivilayer, ivisurf);

    weston_layout_surfaceSetDestinationRectangle(ivisurf, 0, PANEL_HEIGHT, width, height - PANEL_HEIGHT);

    weston_layout_surfaceSetVisibility(ivisurf, 1);

    weston_layout_commitChanges();
}

void
hmi_create_panel(uint32_t id_surface)
{
    struct weston_layout_surface *ivisurf = NULL;
    struct weston_layout_layer *ivilayer = NULL;
    weston_layout_screen_ptr *ppScreen = NULL;
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t length = 0;

    weston_layout_getScreens(&length, &ppScreen);

    weston_layout_getScreenResolution(ppScreen[0], &width, &height);

    ivilayer = weston_layout_layerCreateWithDimension(ID_LAYER_PANEL, width, PANEL_HEIGHT);

    weston_layout_screenAddLayer(ppScreen[0], ivilayer);

    weston_layout_layerSetVisibility(ivilayer, 1);

    ivisurf = weston_layout_getSurfaceFromId(id_surface);

    weston_layout_layerAddSurface(ivilayer, ivisurf);

    weston_layout_surfaceSetDestinationRectangle(ivisurf, 0, 0, width, PANEL_HEIGHT);

    weston_layout_surfaceSetVisibility(ivisurf, 1);

    weston_layout_commitChanges();
}

void
hmi_create_button(uint32_t id_surface, uint32_t number)
{
    struct weston_layout_surface *ivisurf = NULL;
    struct weston_layout_layer *ivilayer = NULL;
    weston_layout_screen_ptr *ppScreen = NULL;
    const uint32_t width  = 48;
    const uint32_t height = 48;
    uint32_t length = 0;

    weston_layout_getScreens(&length, &ppScreen);

    ivilayer = weston_layout_layerCreateWithDimension((ID_LAYER_BUTTON + number), width, height);

    weston_layout_screenAddLayer(ppScreen[0], ivilayer);

    weston_layout_layerSetVisibility(ivilayer, 1);

    ivisurf = weston_layout_getSurfaceFromId(id_surface);

    weston_layout_layerAddSurface(ivilayer, ivisurf);

    weston_layout_surfaceSetDestinationRectangle(ivisurf, ((60 * number) + 15), 5, width, height);

    weston_layout_surfaceSetVisibility(ivisurf, 1);

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
                        struct weston_layout_layer *ivilayer,
                        uint32_t screen_width, uint32_t screen_height)
{
    const float surface_width  = (float)screen_width * 0.25;
    const float surface_height = (float)(screen_height - PANEL_HEIGHT) * 0.5;
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

        ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
        assert(!ret);

        if (num <= 8) {
            if (num < 5) {
                surface_x = (num - 1) * (surface_width);
                surface_y = 0.0f;
            }
            else {
                surface_x = (num - 5) * (surface_width);
                surface_y = (float)(screen_height - PANEL_HEIGHT) * 0.5;
            }
            weston_layout_surfaceSetDestinationRectangle(ivisurf, surface_x, surface_y,
                                                      surface_width, surface_height);
            weston_layout_surfaceSetVisibility(ivisurf, 1);
            num++;
            continue;
        }

        weston_layout_surfaceSetVisibility(ivisurf, 0);
    }
}

static void
mode_divided_into_two(weston_layout_surface_ptr *ppSurface, uint32_t surface_length,
                      struct weston_layout_layer *ivilayer,
                      uint32_t screen_width, uint32_t screen_height)
{
    float surface_width  = (float)screen_width * 0.5f;
    float surface_height = (float)(screen_height - PANEL_HEIGHT);
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

        ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
        assert(!ret);

        if (num == 1) {
            weston_layout_surfaceSetDestinationRectangle(ivisurf, 0, 0,
                                                surface_width, surface_height);
            weston_layout_surfaceSetVisibility(ivisurf, 1);
            num++;
            continue;
        }
        else if (num == 2) {
            weston_layout_surfaceSetDestinationRectangle(ivisurf, surface_width, 0,
                                                surface_width, surface_height);
            weston_layout_surfaceSetVisibility(ivisurf, 1);
            num++;
            continue;
        }

        weston_layout_surfaceSetVisibility(ivisurf, 0);
    }
}

static void
mode_maximize_someone(weston_layout_surface_ptr *ppSurface, uint32_t surface_length,
                      struct weston_layout_layer *ivilayer,
                      uint32_t screen_width, uint32_t screen_height)
{
    const float surface_width  = (float)screen_width;
    const float surface_height = (float)(screen_height - PANEL_HEIGHT);
    struct weston_layout_surface *ivisurf  = NULL;
    uint32_t i = 0;
    for (i = 0; i < surface_length; i++) {
        ivisurf = ppSurface[i];

        /* skip desktop widgets */
        if (is_surf_in_desktopWidget(ivisurf)) {
            continue;
        }

        weston_layout_layerAddSurface(ivilayer, ivisurf);

        weston_layout_surfaceSetDestinationRectangle(ivisurf, 0, 0, surface_width, surface_height);

        weston_layout_surfaceSetVisibility(ivisurf, 1);
    }
}

static void
mode_random_replace(weston_layout_surface_ptr *ppSurface, uint32_t surface_length,
                    struct weston_layout_layer *ivilayer,
                    uint32_t screen_width, uint32_t screen_height)
{
    const float surface_width  = (float)screen_width * 0.25f;
    const float surface_height = (float)(screen_height - PANEL_HEIGHT) * 0.25;
    float surface_x = 0.0f;
    float surface_y = 0.0f;
    struct weston_layout_surface *ivisurf  = NULL;

    uint32_t i = 0;
    for (i = 0; i < surface_length; i++) {
        ivisurf = ppSurface[i];

        /* skip desktop widgets */
        if (is_surf_in_desktopWidget(ivisurf)) {
            continue;
        }

        weston_layout_layerAddSurface(ivilayer, ivisurf);

        surface_x = rand() % 800;
        surface_y = rand() % 500;
        weston_layout_surfaceSetDestinationRectangle(ivisurf, surface_x, surface_y,
                                                  surface_width, surface_height);
        weston_layout_surfaceSetVisibility(ivisurf, 1);
    }
}


static void
switch_mode(uint32_t id_surface)
{
    struct weston_layout_layer *ivilayer  = NULL;
    weston_layout_surface_ptr  *ppSurface = NULL;
    weston_layout_screen_ptr   *ppScreen  = NULL;
    uint32_t surface_length = 0;
    uint32_t screen_length  = 0;
    uint32_t screen_width   = 0;
    uint32_t screen_height  = 0;

    weston_layout_getScreens(&screen_length, &ppScreen);

    weston_layout_getScreenResolution(ppScreen[0], &screen_width, &screen_height);

    ivilayer = weston_layout_layerCreateWithDimension(ID_APPLAYER, screen_width,
                                                   (screen_height - PANEL_HEIGHT));

    weston_layout_screenAddLayer(ppScreen[0], ivilayer);

    weston_layout_layerSetDestinationRectangle(ivilayer, 0, PANEL_HEIGHT,
                                            screen_width, (screen_height - PANEL_HEIGHT));
    weston_layout_layerSetVisibility(ivilayer, 1);

    weston_layout_getSurfaces(&surface_length, &ppSurface);

    do {
        if (id_surface == MODE_DIVIDED_INTO_EIGHT) {
            mode_divided_into_eight(ppSurface, surface_length, ivilayer,
                                    screen_width, screen_height);
            break;
        }
        if (id_surface == MODE_DIVIDED_INTO_TWO) {
            mode_divided_into_two(ppSurface, surface_length, ivilayer,
                                    screen_width, screen_height);
            break;
        }
        if (id_surface == MODE_MAXIMIZE_SOMEONE) {
            mode_maximize_someone(ppSurface, surface_length, ivilayer,
                                    screen_width, screen_height);
            break;
        }
        if (id_surface == MODE_RANDOM_REPLACE) {
            mode_random_replace(ppSurface, surface_length, ivilayer,
                                    screen_width, screen_height);
            break;
        }
    } while(0);

    weston_layout_commitChanges();

    free(ppSurface);
    free(ppScreen);

    return;
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
    hmi_client_start();

    return 0;
}
