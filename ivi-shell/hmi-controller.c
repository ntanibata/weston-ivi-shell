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
#include <time.h>

#include "weston-layout.h"
#include "hmi-controller.h"
#include "hmi-controller-server-protocol.h"

#define PANEL_HEIGHT  70

/*****************************************************************************
 *  structure, globals
 ****************************************************************************/
struct hmi_controller_layer {
    struct weston_layout_layer  *ivilayer;
    uint32_t id_layer;
    enum hmi_controller_layout_mode layout_mode;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

struct link_layer {
    struct weston_layout_layer *layout_layer;
    struct wl_list link;
};

struct link_animation {
    struct hmi_controller_animation *animation;
    struct wl_list link;
};

struct hmi_controller_animation;
typedef void (*hmi_controller_animation_frame_func)(void *animation, uint32_t timestamp);
typedef void (*hmi_controller_animation_frame_user_func)(void *animation);
typedef void (*hmi_controller_animation_destroy_func)(struct hmi_controller_animation *animation);

struct move_animation_user_data {
    struct weston_layout_layer* layer;
};

struct hmi_controller_animation {
    void *user_data;
    uint32_t  time_start;
    uint32_t  is_done;
    hmi_controller_animation_frame_func frame_func;
    hmi_controller_animation_frame_user_func frame_user_func;
    hmi_controller_animation_destroy_func destroy_func;
};

struct hmi_controller_animation_fade {
    struct hmi_controller_animation base;
    float  start;
    float  end;
    struct weston_spring spring;
};

struct hmi_controller_animation_move {
    struct hmi_controller_animation base;
    float pos;
    float pos_start;
    float pos_end;
    float v0;
    float a;
    float time_end;
};

struct hmi_controller_fade {
    uint32_t isFadeIn;
    struct hmi_controller_animation_fade *animation;
    struct wl_list layer_list;
};

struct hmi_controller_anima_event {
    struct wl_display       *display;
    struct wl_event_source  *event_source;
};

struct hmi_controller_layer g_DesktopLayer = {0};
struct hmi_controller_layer g_ApplicationLayer = {0};
struct hmi_controller_layer g_WorkSpaceBackGroundLayer = {0};
struct hmi_controller_layer g_WorkSpaceLayer = {0};
struct hmi_controller_layer g_CursorLayer = {0};
struct hmi_controller_setting g_HmiSetting = {0};

struct wl_list g_list_animation = {0};
struct hmi_controller_fade g_WorkSpaceFade = {0};
struct hmi_controller_anima_event  anima_event = {0};

struct hmi_controller_animation_move* g_workspace_swipe_animation = NULL;

int32_t g_workspace_count = 0;

/*****************************************************************************
 *  local functions
 ****************************************************************************/
static int32_t
is_surf_in_desktopWidget(struct weston_layout_surface *ivisurf)
{
    uint32_t id = weston_layout_getIdOfSurface(ivisurf);

    if (id == g_HmiSetting.background.id ||
        id == g_HmiSetting.panel.id ||
        id == g_HmiSetting.tiling.id ||
        id == g_HmiSetting.sidebyside.id ||
        id == g_HmiSetting.fullscreen.id ||
        id == g_HmiSetting.random.id ||
        id == g_HmiSetting.home.id ||
        id == g_HmiSetting.workspace_background.id) {
        return 1;
    }

    struct hmi_controller_launcher *launcher = NULL;
    wl_list_for_each(launcher, &g_HmiSetting.launcher_list, link) {
        if (id == launcher->icon_surface_id) {
            return 1;
        }
    }

    return 0;
}

static void
mode_divided_into_eight(weston_layout_surface_ptr *ppSurface, uint32_t surface_length,
                        struct hmi_controller_layer *layer)
{
    const float surface_width  = (float)layer->width * 0.25;
    const float surface_height = (float)layer->height * 0.5;
    int32_t surface_x = 0;
    int32_t surface_y = 0;
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
                surface_x = (int32_t)((num - 1) * (surface_width));
                surface_y = 0;
            }
            else {
                surface_x = (int32_t)((num - 5) * (surface_width));
                surface_y = (int32_t)surface_height;
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
    uint32_t surface_width  = layer->width / 2;
    uint32_t surface_height = layer->height;
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
    const uint32_t  surface_width  = layer->width;
    const uint32_t  surface_height = layer->height;
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
    const uint32_t surface_width  = (uint32_t)(layer->width * 0.25f);
    const uint32_t surface_height = (uint32_t)(layer->height * 0.25f);
    uint32_t surface_x = 0;
    uint32_t surface_y = 0;
    struct weston_layout_surface *ivisurf  = NULL;
    int32_t ret = 0;

    uint32_t i = 0;
    for (i = 0; i < surface_length; i++) {
        ivisurf = ppSurface[i];

        /* skip desktop widgets */
        if (is_surf_in_desktopWidget(ivisurf)) {
            continue;
        }

        surface_x = rand() % (layer->width - surface_width);
        surface_y = rand() % (layer->height - surface_height);

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
switch_mode(enum hmi_controller_layout_mode layout_mode)
{
    struct hmi_controller_layer *layer = &g_ApplicationLayer;
    weston_layout_surface_ptr  *ppSurface = NULL;
    uint32_t surface_length = 0;
    int32_t ret = 0;

    layer->layout_mode = layout_mode;

    ret = weston_layout_getSurfaces(&surface_length, &ppSurface);
    assert(!ret);

    if (!has_applicatipn_surface(ppSurface, surface_length)) {
        free(ppSurface);
        ppSurface = NULL;
        return;
    }

    switch (layout_mode) {
    case HMI_CONTROLLER_LAYOUT_MODE_TILING:
        mode_divided_into_eight(ppSurface, surface_length, layer);
        break;
    case HMI_CONTROLLER_LAYOUT_MODE_SIDE_BY_SIDE:
        mode_divided_into_two(ppSurface, surface_length, layer);
        break;
    case HMI_CONTROLLER_LAYOUT_MODE_FULL_SCREEN:
        mode_maximize_someone(ppSurface, surface_length, layer);
        break;
    case HMI_CONTROLLER_LAYOUT_MODE_RANDOM:
        mode_random_replace(ppSurface, surface_length, layer);
        break;
    }

    weston_layout_commitChanges();

    free(ppSurface);
    ppSurface = NULL;

    return;
}

static void
hmi_controller_add_animation(struct hmi_controller_animation *animation)
{
    struct link_animation *link_anima = NULL;

    link_anima = calloc(1, sizeof(*link_anima));
    if (NULL == link_anima) {
        return;
    }

    link_anima->animation = animation;
    wl_list_insert(&g_list_animation, &link_anima->link);
    wl_event_source_timer_update(anima_event.event_source, 1);
}

static void
hmi_controller_remove_animation(struct hmi_controller_animation *animation)
{
    struct link_animation *link_animation = NULL;
    struct link_animation *next = NULL;

    wl_list_for_each_safe(link_animation, next, &g_list_animation, link) {
        if (link_animation->animation == animation) {
            wl_list_remove(&link_animation->link);
            free(link_animation);
            break;
        }
    }
}

static void
hmi_controller_animation_frame(
    struct hmi_controller_animation *animation, uint32_t timestamp)
{
    if (0 == animation->time_start) {
        animation->time_start = timestamp;
    }

    animation->frame_func(animation, timestamp);
    animation->frame_user_func(animation);
}

static void
hmi_controller_animation_spring_frame(
    struct hmi_controller_animation_fade *animation, uint32_t timestamp)
{
    if (0 == animation->spring.timestamp) {
        animation->spring.timestamp = timestamp;
    }

    weston_spring_update(&animation->spring, timestamp);
    animation->base.is_done = weston_spring_done(&animation->spring);
}

static void
hmi_controller_animation_move_frame(
    struct hmi_controller_animation_move *animation, uint32_t timestamp)
{
    float s = animation->pos_start;
    float t = timestamp - animation->base.time_start;
    float v0 = animation->v0;
    float a = animation->a;
    float time_end = animation->time_end;

    if (time_end <= t) {
        animation->pos = animation->pos_end;
        animation->base.is_done = 1;
    } else {
        animation->pos = v0 * t + 0.5 * a * t * t + s;
    }
}

static void
hmi_controller_animation_destroy(struct hmi_controller_animation *animation)
{
    if (animation->destroy_func) {
        animation->destroy_func(animation);
    }

    free(animation);
}

static struct hmi_controller_animation_fade *
hmi_controller_animation_fade_create(float start, float end, float k,
    hmi_controller_animation_frame_user_func frame_user_func, void* user_data,
    hmi_controller_animation_destroy_func destroy_func)
{
    struct hmi_controller_animation_fade* animation = calloc(1, sizeof(*animation));
    assert(animation);

    animation->base.frame_user_func = frame_user_func;
    animation->base.user_data = user_data;
    animation->base.frame_func =
        (hmi_controller_animation_frame_func)hmi_controller_animation_spring_frame;
    animation->base.destroy_func = destroy_func;

    animation->start = start;
    animation->end = end;
    weston_spring_init(&animation->spring, k, start, end);
    animation->spring.friction = 1400;
    animation->spring.previous = -(end - start) * 0.03;

    return animation;
}

static struct hmi_controller_animation_move *
hmi_controller_animation_move_create(
    float pos_start, float pos_end, float v_start, float v_end,
    hmi_controller_animation_frame_user_func frame_user_func, void* user_data,
    hmi_controller_animation_destroy_func destroy_func)
{
    struct hmi_controller_animation_move* animation = calloc(1, sizeof(*animation));
    assert(animation);

    animation->base.frame_user_func = frame_user_func;
    animation->base.user_data = user_data;
    animation->base.frame_func =
        (hmi_controller_animation_frame_func)hmi_controller_animation_move_frame;
    animation->base.destroy_func = destroy_func;

    animation->pos_start = pos_start;
    animation->pos_end = pos_end;
    animation->v0 = v_start;
    animation->pos = pos_start;

    float dx = (pos_end - pos_start);

    if (1e-3 < fabs(dx)) {
        animation->a = 0.5 * (v_end * v_end - v_start * v_start) / dx;
        if (1e-6 < fabs(animation->a)) {
            animation->time_end = (v_end - v_start) / animation->a;

        } else {
            animation->a = 0;
            animation->time_end = fabs(dx / animation->v0);
        }

    } else {
        animation->time_end = 0;
    }

    return animation;
}

static float
hmi_controller_animation_fade_alpha_get(struct hmi_controller_animation_fade* animation)
{
    if (animation->spring.current > 0.999) {
        return 1.0;
    } else if (animation->spring.current < 0.001 ) {
        return 0.0;
    } else {
        return animation->spring.current;
    }
}

static uint32_t
hmi_controller_animation_is_done(struct hmi_controller_animation *animation)
{
    return animation->is_done;
}

static void
hmi_controller_fade_update(struct hmi_controller_animation_fade *animation, float end)
{
    animation->spring.target = end;
}

static void
hmi_controller_anima_fade_user_frame(struct hmi_controller_animation_fade *animation)
{
    float alpha = hmi_controller_animation_fade_alpha_get(animation);
    alpha = wl_fixed_from_double(alpha);
    struct hmi_controller_fade *fade = animation->base.user_data;
    struct link_layer *linklayer = NULL;
    int32_t is_done = hmi_controller_animation_is_done(&animation->base);
    int32_t is_visible = !is_done || fade->isFadeIn;

    wl_list_for_each(linklayer, &fade->layer_list, link) {
        weston_layout_layerSetOpacity(linklayer->layout_layer, alpha);
        weston_layout_layerSetVisibility(linklayer->layout_layer, is_visible);
    }

    if (is_done) {
        hmi_controller_remove_animation(&animation->base);
        hmi_controller_animation_destroy(&animation->base);
        fade->animation = NULL;
    }
}

static void
hmi_controller_anima_move_user_frame(struct hmi_controller_animation_move *animation)
{
    struct move_animation_user_data* user_data = animation->base.user_data;
    struct weston_layout_layer *layer = user_data->layer;
    int32_t is_done = hmi_controller_animation_is_done(&animation->base);

    int32_t pos[2] = {0};
    weston_layout_layerGetPosition(layer, pos);

    pos[0] = (int32_t)animation->pos;
    weston_layout_layerSetPosition(layer, pos);

    if (is_done) {
        hmi_controller_remove_animation(&animation->base);
        hmi_controller_animation_destroy(&animation->base);
    }
}

static void
hmi_controller_fade_run(uint32_t isFadeIn, struct hmi_controller_fade *fade)
{
    float tint = isFadeIn ? 1.0 : 0.0;
    fade->isFadeIn = isFadeIn;

    if (fade->animation) {
        hmi_controller_fade_update(fade->animation, tint);
    } else {
        fade->animation = hmi_controller_animation_fade_create(
            1.0 - tint, tint, 300.0,
            (hmi_controller_animation_frame_user_func)hmi_controller_anima_fade_user_frame,
            fade, NULL);

        hmi_controller_add_animation(&fade->animation->base);
    }
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

    switch_mode(layer->layout_mode);
}

static void
set_notification_configure_surface(struct weston_layout_surface *ivisurf,
                                void *userdata)
{
    struct hmi_controller_layer *layer = &g_ApplicationLayer;
    (void)ivisurf;
    (void)userdata;

    switch_mode(layer->layout_mode);
}

static void parse_commma_separated_string(char* src, struct wl_array *dst)
{
    char *tmp_src = strdup(src);
    char *sep = ",";
    char *saveptr = NULL;

    char *tok = strtok_r(tmp_src, sep, &saveptr);

    while (tok) {
        char** add = wl_array_add(dst, sizeof(*add));
        assert(add);
        *add = strdup(tok);
        assert(*add);
        tok = strtok_r(NULL, sep, &saveptr);
    }

    free(tmp_src);
}

static void parse_window_title_section(char* src, struct wl_array *titles)
{
    wl_array_init(titles);

    if (NULL == src) {
        return;
    }

    if (0 == strlen(src)) {
        char** add = wl_array_add(titles, sizeof(*add));
        *add = NULL;
        return;
    }

    parse_commma_separated_string(src, titles);
}

static void
init_hmi_controllerSetting(struct hmi_controller_setting *setting)
{
    uint32_t iconid = 0;
    memset(setting, 0, sizeof(*setting));
    wl_list_init(&setting->workspace_list);
    wl_list_init(&setting->launcher_list);

    struct weston_config *config = NULL;
    config = weston_config_parse("weston.ini");

    struct weston_config_section *shellSection = NULL;
    shellSection = weston_config_get_section(config, "ivi-shell", NULL, NULL);

    weston_config_section_get_uint(
        shellSection, "desktop-layer-id", &setting->desktop_layer_id, 1000);

    weston_config_section_get_uint(
        shellSection, "workspace-background-layer-id", &setting->workspace_backgound_layer_id, 2000);

    weston_config_section_get_uint(
        shellSection, "workspace-layer-id", &setting->workspace_layer_id, 3000);

    weston_config_section_get_uint(
        shellSection, "application-layer-id", &setting->application_layer_id, 4000);

    weston_config_section_get_uint(
        shellSection, "cursor-layer-id", &setting->cursor_layer_id, 5000);

    weston_config_section_get_string(
        shellSection, "background-image", &setting->background.filePath,
        DATADIR "/weston/background.png");

    weston_config_section_get_uint(
        shellSection, "background-id", &setting->background.id, 1001);

    weston_config_section_get_string(
        shellSection, "panel-image", &setting->panel.filePath,
        DATADIR "/weston/panel.png");

    weston_config_section_get_uint(
        shellSection, "panel-id", &setting->panel.id, 1002);

    weston_config_section_get_string(
        shellSection, "tiling-image", &setting->tiling.filePath,
        DATADIR "/weston/tiling.png");

    weston_config_section_get_uint(
        shellSection, "tiling-id", &setting->tiling.id, 1003);

    weston_config_section_get_string(
        shellSection, "sidebyside-image", &setting->sidebyside.filePath,
        DATADIR "/weston/sidebyside.png");

    weston_config_section_get_uint(
        shellSection, "sidebyside-id", &setting->sidebyside.id, 1004);

    weston_config_section_get_string(
        shellSection, "fullscreen-image", &setting->fullscreen.filePath,
        DATADIR "/weston/fullscreen.png");

    weston_config_section_get_uint(
        shellSection, "fullscreen-id", &setting->fullscreen.id, 1005);

    weston_config_section_get_string(
        shellSection, "random-image", &setting->random.filePath,
        DATADIR "/weston/random.png");

    weston_config_section_get_uint(
        shellSection, "random-id", &setting->random.id, 1006);

    weston_config_section_get_string(
        shellSection, "home-image", &setting->home.filePath,
        DATADIR "/weston/home.png");

    weston_config_section_get_uint(
        shellSection, "home-id", &setting->home.id, 1007);

    weston_config_section_get_string(
        shellSection, "cursor-image", &setting->cursor.filePath,
        DATADIR "/weston/cursor.png");

    weston_config_section_get_uint(
        shellSection, "cursor-id", &setting->cursor.id, 5001);

    weston_config_section_get_uint(
        shellSection, "workspace-background-color",
        &setting->workspace_background.color, 0x99000000);

    weston_config_section_get_uint(
        shellSection, "workspace-background-id",
        &setting->workspace_background.id, 2001);

    struct weston_config_section *section = NULL;
    const char *name = NULL;

    iconid = setting->workspace_layer_id + 1;

    weston_config_section_get_string(
        shellSection, "ivi-surface-creator-path", &setting->surface_creator_path, "");

    while (weston_config_next_section(config, &section, &name)) {

        if (0 == strcmp(name, "ivi-launcher")) {

            struct hmi_controller_launcher *launcher = NULL;
            launcher = calloc(1, sizeof(*launcher));
            assert(launcher);
            wl_list_init(&launcher->link);
            wl_array_init(&launcher->setid_window_titles);
            launcher->icon_surface_id = iconid;
            iconid++;

            weston_config_section_get_string(section, "icon", &launcher->icon, NULL);
            weston_config_section_get_string(section, "path", &launcher->path, NULL);
            weston_config_section_get_uint(section, "workspace-id", &launcher->workspace_id, 0);

            char* window_titles = NULL;
            weston_config_section_get_string(section, "setid-window-titles", &window_titles, NULL);
            parse_window_title_section(window_titles, &launcher->setid_window_titles);
            free(window_titles);

            wl_list_insert(setting->launcher_list.prev, &launcher->link);
        }
    }

    weston_config_destroy(config);
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
    struct link_layer *tmp_link_layer = NULL;

    init_hmi_controllerSetting(&g_HmiSetting);

    weston_layout_getScreens(&screen_length, &ppScreen);

    iviscrn = ppScreen[0];

    weston_layout_getScreenResolution(iviscrn, &screen_width, &screen_height);
    assert(!ret);

    /* init desktop layer*/
    g_DesktopLayer.x = 0;
    g_DesktopLayer.y = 0;
    g_DesktopLayer.width  = screen_width;
    g_DesktopLayer.height = screen_height;
    g_DesktopLayer.id_layer = g_HmiSetting.desktop_layer_id;

    create_layer(iviscrn, &g_DesktopLayer);

    /* init application layer */
    g_ApplicationLayer.x = 0;
    g_ApplicationLayer.y = 0;
    g_ApplicationLayer.width  = screen_width;
    g_ApplicationLayer.height = screen_height - PANEL_HEIGHT;
    g_ApplicationLayer.id_layer = g_HmiSetting.application_layer_id;
    g_ApplicationLayer.layout_mode = HMI_CONTROLLER_LAYOUT_MODE_TILING;

    create_layer(iviscrn, &g_ApplicationLayer);

    /* init workspace backgound layer */
    g_WorkSpaceBackGroundLayer.x = g_ApplicationLayer.x;
    g_WorkSpaceBackGroundLayer.y = g_ApplicationLayer.y;
    g_WorkSpaceBackGroundLayer.width = g_ApplicationLayer.width;
    g_WorkSpaceBackGroundLayer.height =g_ApplicationLayer.height;

    g_WorkSpaceBackGroundLayer.id_layer =
            g_HmiSetting.workspace_backgound_layer_id;

    create_layer(iviscrn, &g_WorkSpaceBackGroundLayer);
    weston_layout_layerSetOpacity(g_WorkSpaceBackGroundLayer.ivilayer, 0);
    weston_layout_layerSetVisibility(g_WorkSpaceBackGroundLayer.ivilayer, 0);

    /* init workspace layer */
    g_WorkSpaceLayer.x = g_WorkSpaceBackGroundLayer.x;
    g_WorkSpaceLayer.y = g_WorkSpaceBackGroundLayer.y;
    g_WorkSpaceLayer.width = g_WorkSpaceBackGroundLayer.width;
    g_WorkSpaceLayer.height = g_WorkSpaceBackGroundLayer.height;
    g_WorkSpaceLayer.id_layer = g_HmiSetting.workspace_layer_id;
    create_layer(iviscrn, &g_WorkSpaceLayer);
    weston_layout_layerSetOpacity(g_WorkSpaceLayer.ivilayer, 0);
    weston_layout_layerSetVisibility(g_WorkSpaceLayer.ivilayer, 0);

    /* init work space fade */
    wl_list_init(&g_WorkSpaceFade.layer_list);
    tmp_link_layer = calloc(1, sizeof(*tmp_link_layer));
    assert(tmp_link_layer);
    tmp_link_layer->layout_layer = g_WorkSpaceLayer.ivilayer;
    wl_list_insert(&g_WorkSpaceFade.layer_list, &tmp_link_layer->link);
    tmp_link_layer = calloc(1, sizeof(*tmp_link_layer));
    assert(tmp_link_layer);
    tmp_link_layer->layout_layer = g_WorkSpaceBackGroundLayer.ivilayer;
    wl_list_insert(&g_WorkSpaceFade.layer_list, &tmp_link_layer->link);

    wl_list_init(&g_list_animation);

    weston_layout_setNotificationCreateSurface(set_notification_create_surface, NULL);
    weston_layout_setNotificationRemoveSurface(set_notification_remove_surface, NULL);
    weston_layout_setNotificationConfigureSurface(set_notification_configure_surface, NULL);

    free(ppScreen);
    ppScreen = NULL;
}

static int
do_anima(void* data)
{
    struct hmi_controller_anima_event *event = data;
    uint32_t fps = 20;

    if (wl_list_empty(&g_list_animation)) {
        wl_event_source_timer_update(event->event_source, 0);
        return 1;
    }

    wl_event_source_timer_update(event->event_source, 1000 / fps);

    struct timespec timestamp = {0};
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    uint32_t msec = (1e+3 * timestamp.tv_sec + 1e-6 * timestamp.tv_nsec);

    struct link_animation *link_animation = NULL;
    struct link_animation *next = NULL;

    wl_list_for_each_safe(link_animation, next, &g_list_animation, link) {
        hmi_controller_animation_frame(link_animation->animation, msec);
    }

    weston_layout_commitChanges();
    return 1;
}

/*****************************************************************************
 *  hmi controller interface
 ****************************************************************************/

static void
hmi_controller_set_background(struct wl_client *client,
                              struct wl_resource *resource,
                              uint32_t id_surface)

{
    struct weston_layout_surface *ivisurf = NULL;
    struct weston_layout_layer   *ivilayer = g_DesktopLayer.ivilayer;
    const uint32_t dstx = g_ApplicationLayer.x;
    const uint32_t dsty = g_ApplicationLayer.y;
    const uint32_t width  = g_ApplicationLayer.width;
    const uint32_t height = g_ApplicationLayer.height;
    uint32_t ret = 0;

    ivisurf = weston_layout_getSurfaceFromId(id_surface);
    assert(ivisurf != NULL);

    ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
    assert(!ret);

    ret = weston_layout_surfaceSetDestinationRectangle(ivisurf,
                                    dstx, dsty, width, height);
    assert(!ret);

    ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
    assert(!ret);

    weston_layout_commitChanges();
}

static void
hmi_controller_set_panel(struct wl_client *client,
                         struct wl_resource *resource,
                         uint32_t id_surface)
{
    struct weston_layout_surface *ivisurf  = NULL;
    struct weston_layout_layer   *ivilayer = g_DesktopLayer.ivilayer;
    const uint32_t width  = g_DesktopLayer.width;
    uint32_t ret = 0;

    ivisurf = weston_layout_getSurfaceFromId(id_surface);
    assert(ivisurf != NULL);

    ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
    assert(!ret);

    const uint32_t dstx = 0;
    const uint32_t dsty = g_DesktopLayer.height - PANEL_HEIGHT;

    ret = weston_layout_surfaceSetDestinationRectangle(
                        ivisurf, dstx, dsty, width, PANEL_HEIGHT);
    assert(!ret);

    ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
    assert(!ret);

    weston_layout_commitChanges();
}

static void
hmi_controller_set_button(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t id_surface, uint32_t number)
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

    const uint32_t dstx = (60 * number) + 15;
    const uint32_t dsty = (g_DesktopLayer.height - PANEL_HEIGHT) + 5;

    ret = weston_layout_surfaceSetDestinationRectangle(
                            ivisurf,dstx, dsty, width, height);
    assert(!ret);

    ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
    assert(!ret);

    weston_layout_commitChanges();
}

static void
hmi_controller_set_home_button(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id_surface)
{
    struct weston_layout_surface *ivisurf  = NULL;
    struct weston_layout_layer   *ivilayer = g_DesktopLayer.ivilayer;
    uint32_t ret = 0;
    uint32_t size = 48;
    const uint32_t dstx = (g_DesktopLayer.width - size) / 2;
    const uint32_t dsty = (g_DesktopLayer.height - PANEL_HEIGHT) + 5;

    ivisurf = weston_layout_getSurfaceFromId(id_surface);
    assert(ivisurf != NULL);

    ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
    assert(!ret);

    ret = weston_layout_surfaceSetDestinationRectangle(
                    ivisurf, dstx, dsty, size, size);
    assert(!ret);

    ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
    assert(!ret);

    weston_layout_commitChanges();
}

static void
hmi_controller_set_workspacebackground(struct wl_client *client,
                                       struct wl_resource *resource,
                                       uint32_t id_surface)
{
    struct weston_layout_surface *ivisurf = NULL;
    struct weston_layout_layer   *ivilayer = NULL;
    ivilayer = g_WorkSpaceBackGroundLayer.ivilayer;

    const uint32_t width  = g_WorkSpaceBackGroundLayer.width;
    const uint32_t height = g_WorkSpaceBackGroundLayer.height;
    uint32_t ret = 0;

    ivisurf = weston_layout_getSurfaceFromId(id_surface);
    assert(ivisurf != NULL);

    ret = weston_layout_layerAddSurface(ivilayer, ivisurf);
    assert(!ret);

    ret = weston_layout_surfaceSetDestinationRectangle(ivisurf,
                                    0, 0, width, height);
    assert(!ret);

    ret = weston_layout_surfaceSetVisibility(ivisurf, 1);
    assert(!ret);

    weston_layout_commitChanges();
}

static void
hmi_controller_add_launchers(struct wl_client *client,
                             struct wl_resource *resource,
                             struct wl_array *surface_ids,
                             uint32_t icon_size)
{
    uint32_t minspace_x = 10;
    uint32_t minspace_y = minspace_x;

    uint32_t width  = g_DesktopLayer.width;
    uint32_t height = g_WorkSpaceLayer.height;

    uint32_t x_count = (width - minspace_x) / (minspace_x + icon_size);
    uint32_t space_x = (uint32_t)((width - x_count * icon_size) / (1.0 + x_count));
    float fcell_size_x = icon_size + space_x;

    uint32_t y_count = (height - minspace_y) / (minspace_y + icon_size);
    uint32_t space_y = (uint32_t)((height - y_count * icon_size) / (1.0 + y_count));
    float fcell_size_y = icon_size + space_y;

    if (0 == x_count) {
       x_count = 1;
    }

    if (0 == y_count) {
       y_count  = 1;
    }

    g_workspace_count++;

    uint32_t *surface_id = NULL;
    uint32_t nx = 0;
    uint32_t ny = 0;

    wl_array_for_each(surface_id, surface_ids) {

        if (y_count == ny) {
            ny = 0;
            g_workspace_count++;
        }

        uint32_t x = (uint32_t)(nx * fcell_size_x + (g_workspace_count - 1) * width + space_x);
        uint32_t y = (uint32_t)(ny * fcell_size_y  + space_y) ;

        struct weston_layout_surface* layout_surface = NULL;
        layout_surface = weston_layout_getSurfaceFromId(*surface_id);
        assert(layout_surface);

        uint32_t ret = 0;
        ret = weston_layout_layerAddSurface(g_WorkSpaceLayer.ivilayer, layout_surface);
        assert(!ret);

        ret = weston_layout_surfaceSetDestinationRectangle(
                        layout_surface, x, y, icon_size, icon_size);
        assert(!ret);

        ret = weston_layout_surfaceSetVisibility(layout_surface, 1);
        assert(!ret);

        nx++;

        if (x_count == nx) {
            ny++;
            nx = 0;
        }
    }

    weston_layout_commitChanges();
}

struct pointer_grab {
    struct weston_pointer_grab grab;
    struct weston_layout_layer *layer;
};

struct pointer_move_grab {
    struct pointer_grab base;
    wl_fixed_t dst[2];
    wl_fixed_t clip[2][2];
    float v[2];
    struct timespec start_time;
    struct timespec pre_time;
    wl_fixed_t pre_pos[2];
    struct wl_resource* resource;
    int32_t is_moved;
};

static void
pointer_grab_start(struct pointer_grab *grab,
                   struct weston_layout_layer *layer,
                   const struct weston_pointer_grab_interface *interface,
                   struct weston_pointer *pointer)
{
    grab->grab.interface = interface;
    grab->layer = layer;
    weston_pointer_start_grab(pointer, &grab->grab);
}

static int32_t clip_val(int32_t val, int32_t min, int32_t max)
{
    if (val < min) {
        return min;
    }

    if (max < val) {
        return max;
    }

    return val;
}

static void hmi_controller_move_animation_destroy(
    struct hmi_controller_animation *animation)
{
    if (animation == (struct hmi_controller_animation *)g_workspace_swipe_animation) {
        g_workspace_swipe_animation = NULL;
    }

    free(animation->user_data);
    animation->user_data = NULL;
}

static void
pointer_move_workspace_grab_end(struct pointer_grab *grab)
{
    struct pointer_move_grab *move = (struct pointer_move_grab *) grab;
    int32_t width = (int32_t)g_WorkSpaceBackGroundLayer.width;

    struct timespec time = {0};
    clock_gettime(CLOCK_MONOTONIC, &time);

    uint32_t grab_time = 1e+3 * (time.tv_sec  - move->start_time.tv_sec) +
                         1e-6 * (time.tv_nsec - move->start_time.tv_nsec);

    uint32_t from_motion_time = 1e+3 * (time.tv_sec  - move->pre_time.tv_sec) +
                                1e-6 * (time.tv_nsec - move->pre_time.tv_nsec);

    float pointer_v = move->v[0];

    if (200 < from_motion_time) {
       pointer_v = 0.0;
    }

    int32_t is_flick = grab_time < 400 &&
                       0.4 < fabs(pointer_v);

    int32_t pos[2] = {0};
    struct weston_layout_layer *layer = move->base.layer;
    weston_layout_layerGetPosition(layer, pos);

    float v = 700 * 1e-3;
    int page_no = 0;

    if (is_flick) {
        int orgx = wl_fixed_to_int(move->dst[0] + grab->grab.pointer->grab_x);
        page_no = (-orgx + width / 2) / width;

        if (pointer_v < 0.0) {
            page_no++;
        }else {
            page_no--;
        }
    }else {
        page_no = (-pos[0] + width / 2) / width;
    }

    page_no = clip_val(page_no, 0, g_workspace_count - 1);
    float end_pos = -page_no * width;

    float v0 = 0.0;
    if (pos[0] < end_pos) {
        v0 = v;
    } else {
        v0 = -v;
    }

    struct move_animation_user_data *animation_user_data = NULL;
    animation_user_data = calloc(1, sizeof(*animation_user_data));
    assert(animation_user_data);
    animation_user_data->layer = layer;

    struct hmi_controller_animation_move* animation = NULL;
    animation = hmi_controller_animation_move_create(
        pos[0], end_pos, v0, v0,
        (hmi_controller_animation_frame_user_func)hmi_controller_anima_move_user_frame,
        animation_user_data, hmi_controller_move_animation_destroy);

    g_workspace_swipe_animation = animation;
    hmi_controller_add_animation(&animation->base);

    hmi_controller_send_workspace_end_swipe(move->resource, move->is_moved);
    weston_pointer_end_grab(grab->grab.pointer);
}

static void
pointer_noop_grab_focus(struct weston_pointer_grab *grab)
{
}

static void
pointer_move_grab_motion(struct weston_pointer_grab *grab, uint32_t time,
                         wl_fixed_t x, wl_fixed_t y)
{
    struct pointer_move_grab *move = (struct pointer_move_grab *) grab;
    struct weston_layout_layer *layer = move->base.layer;

    wl_fixed_t pointer[2];
    pointer[0] = x;
    pointer[1] = y;

    int32_t layout_pos[2] = {0};

    struct timespec timestamp = {0};
    clock_gettime(CLOCK_MONOTONIC, &timestamp);

    uint32_t dt = (1e+3 * (timestamp.tv_sec  - move->pre_time.tv_sec) +
                   1e-6 * (timestamp.tv_nsec - move->pre_time.tv_nsec));
    if (dt < 1e-6) {
        dt = 1e-6;
    }

    move->pre_time = timestamp;

    int ii = 0;
    for (ii = 0; ii < 2; ii++) {
        wl_fixed_t pos = pointer[ii] + move->dst[ii];

        if (pos < move->clip[ii][0]) {
            pos = move->clip[ii][0];
            move->dst[ii] = pos - pointer[ii];
        } else if (move->clip[ii][1] < pos) {
            pos = move->clip[ii][1];
            move->dst[ii] = pos - pointer[ii];
        }

        layout_pos[ii] = wl_fixed_to_int(pos);

        move->v[ii] = wl_fixed_to_double(pointer[ii] - move->pre_pos[ii]) / dt;
        move->pre_pos[ii] = pointer[ii];
    }

    move->is_moved = 1;

    weston_layout_layerSetPosition(layer, layout_pos);
    weston_layout_commitChanges();
    weston_pointer_move(grab->pointer, x, y);
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
pointer_move_workspace_grab_cancel(struct weston_pointer_grab *grab)
{
    struct pointer_grab *pg = (struct pointer_grab *)grab;
    pointer_move_workspace_grab_end(pg);
    free(grab);
}

static const struct weston_pointer_grab_interface pointer_move_grab_workspace_interface = {
    pointer_noop_grab_focus,
    pointer_move_grab_motion,
    pointer_move_workspace_grab_button,
    pointer_move_workspace_grab_cancel
};

static void
hmi_controller_workspace_swipe(struct wl_client *client,
                               struct wl_resource *resource,
                               struct wl_resource *seat_resource,
                               uint32_t serial)
{
    if (g_workspace_count < 2) {
        return;
    }

    if (g_workspace_swipe_animation) {
        hmi_controller_remove_animation(&g_workspace_swipe_animation->base);
        hmi_controller_animation_destroy(&g_workspace_swipe_animation->base);
    }

    struct weston_seat* seat = wl_resource_get_user_data(seat_resource);

    if (!seat->pointer ||
        !seat->pointer->focus ||
        0 == seat->pointer->button_count ||
        seat->pointer->grab_serial != serial) {
        return;
    }

    struct weston_layout_layer *layer = g_WorkSpaceLayer.ivilayer;

    int32_t layer_pos[2] = {0};
    weston_layout_layerGetPosition(layer, layer_pos);

    struct pointer_move_grab *move = calloc(1, sizeof(*move));
    assert(move);

    clock_gettime(CLOCK_MONOTONIC, &move->start_time);
    move->pre_time = move->start_time;
    move->pre_pos[0] = seat->pointer->grab_x;
    move->pre_pos[1] = seat->pointer->grab_y;

    move->dst[0] = wl_fixed_from_int(layer_pos[0]) - seat->pointer->grab_x;
    move->dst[1] = wl_fixed_from_int(layer_pos[1]) - seat->pointer->grab_y;

    move->clip[0][0] = wl_fixed_from_int(
            -g_WorkSpaceBackGroundLayer.width * (g_workspace_count - 1));

    move->clip[0][1] = wl_fixed_from_int(0);
    move->clip[1][0] = wl_fixed_from_int(layer_pos[1]);
    move->clip[1][1] = move->clip[1][0];

    move->resource = resource;

    pointer_grab_start(
        &move->base, layer, &pointer_move_grab_workspace_interface,
        seat->pointer);
}

static void
hmi_controller_switch_mode(struct wl_client *client,
                           struct wl_resource *resource,
                           uint32_t  layout_mode)
{
    switch_mode(layout_mode);
}

static void
hmi_controller_toggle_home(struct wl_client *client,
                           struct wl_resource *resource)
{
    uint32_t isFadeIn = !g_WorkSpaceFade.isFadeIn;
    hmi_controller_fade_run(isFadeIn, &g_WorkSpaceFade);
}

static const struct hmi_controller_interface hmi_controller_implementation = {
    hmi_controller_set_background,
    hmi_controller_set_panel,
    hmi_controller_set_button,
    hmi_controller_set_home_button,
    hmi_controller_set_workspacebackground,
    hmi_controller_add_launchers,
    hmi_controller_workspace_swipe,
    hmi_controller_switch_mode,
    hmi_controller_toggle_home
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

    resource = wl_resource_create(
            client, &hmi_controller_interface, 1, id);

    wl_resource_set_implementation(
            resource, &hmi_controller_implementation,
            data, unbind_hmi_controller);
}

static void
launch_hmi_client(void *data)
{
    hmi_client_start();
}

/*****************************************************************************
 *  exported functions
 ****************************************************************************/

WL_EXPORT int
module_init(struct weston_compositor *ec,
            int *argc, char *argv[])
{
    init_hmi_controller();

    anima_event.display = ec->wl_display;
    struct wl_event_loop *loop = wl_display_get_event_loop(ec->wl_display);
    anima_event.event_source = wl_event_loop_add_timer(loop, do_anima, &anima_event);
    wl_event_source_timer_update(anima_event.event_source, 0);

    if (wl_global_create(ec->wl_display,
                 &hmi_controller_interface, 1,
                 NULL, bind_hmi_controller) == NULL) {
        return -1;
    }

    wl_event_loop_add_idle(loop, launch_hmi_client, ec);

    return 0;
}
