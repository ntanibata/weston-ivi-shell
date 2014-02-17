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
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <getopt.h>
#include <pthread.h>
#include "hmi-controller.h"
#include "../shared/cairo-util.h"
#include "ivi-application-client-protocol.h"
#include "hmi-controller-client-protocol.h"

/*****************************************************************************
 *  structure, globals
 ****************************************************************************/
struct wlContextCommon {
    struct wl_display      *wlDisplay;
    struct wl_registry     *wlRegistry;
    struct wl_compositor   *wlCompositor;
    struct wl_shm          *wlShm;
    struct wl_shell        *wlShell;
    struct wl_seat         *wlSeat;
    struct wl_pointer      *wlPointer;
    struct wl_touch        *wlTouch;
    struct ivi_application *iviApplication;
    struct hmi_controller  *hmiController;

    struct wl_list         *list_wlContextStruct;
    struct wl_surface      *enterSurface;
};

struct wlContextStruct {
    struct wlContextCommon  cmm;
    struct wl_surface       *wlSurface;
    struct wl_shell_surface *wlShellSurface;
    struct wl_buffer        *wlBuffer;
    uint32_t                formats;
    cairo_surface_t         *ctx_image;
    void                    *data;
    uint32_t                id_surface;
    struct wl_list          link;
};

pthread_t thread;

volatile int gRun = 0;

extern struct hmi_controller_setting g_HmiSetting;

/*****************************************************************************
 *  Event Handler
 ****************************************************************************/

static void
shm_format(void* data, struct wl_shm* pWlShm, uint32_t format)
{
    struct wlContextStruct* pDsp = data;
    pDsp->formats |= (1 << format);
}

static struct wl_shm_listener shm_listenter = {
    shm_format
};

static int32_t
getIdOfWlSurface(struct wlContextCommon *pCtx, struct wl_surface *wlSurface)
{
    if (NULL == pCtx ||
        NULL == wlSurface ) {
        return 0;
    }

    struct wlContextStruct* pWlCtxSt = NULL;
    wl_list_for_each(pWlCtxSt, pCtx->list_wlContextStruct, link) {
        if (pWlCtxSt->wlSurface == wlSurface) {
            return pWlCtxSt->id_surface;
        }
        continue;
    }
    return -1;
}

static void
PointerHandleEnter(void* data, struct wl_pointer* wlPointer, uint32_t serial,
                   struct wl_surface* wlSurface, wl_fixed_t sx, wl_fixed_t sy)
{
   (void)wlPointer;
   (void)serial;

    struct wlContextCommon *pCtx = data;
    pCtx->enterSurface = wlSurface;

#ifdef _DEBUG
    printf("ENTER PointerHandleEnter: x(%d), y(%d)\n", sx, sy);
#endif
}

static void
PointerHandleLeave(void* data, struct wl_pointer* wlPointer, uint32_t serial,
                   struct wl_surface* wlSurface)
{
    (void)wlPointer;
    (void)wlSurface;

    struct wlContextCommon *pCtx = data;
    pCtx->enterSurface = NULL;

#ifdef _DEBUG
    printf("ENTER PointerHandleLeave: serial(%d)\n", serial);
#endif
}

static void
PointerHandleMotion(void* data, struct wl_pointer* wlPointer, uint32_t time,
                    wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data;
    (void)wlPointer;
    (void)time;

#ifdef _DEBUG
    printf("ENTER PointerHandleMotion: x(%d), y(%d)\n", gPointerX, gPointerY);
#endif
}


extern char **environ; /*defied by libc */

static pid_t execute_process(char* path, char *argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to fork\n");
    }

    if (pid) {
        return pid;
    }

    if (-1 == execve(path, argv, environ)) {
        fprintf(stderr, "Failed to execve %s\n", path);
        exit(1);
    }

    return pid;
}

static void execute_ivi_surface_creator(
    char* path, pid_t pid, char* window_title, uint32_t id_surface)
{
    char arg1[255] = {0};
    char arg3[255] = {0};
    char *argv[] = {path, arg1, window_title, arg3,  NULL};

    sprintf(arg1, "%d", pid);

    if (NULL == window_title) {
        argv[2] = "";
    }

    sprintf(arg3, "%u", id_surface);

    execute_process(path, argv);
}

static int32_t
launcher_button(uint32_t surfaceId)
{
    pid_t pid = 0;
    struct hmi_controller_launcher *launcher = NULL;

    wl_list_for_each(launcher, &g_HmiSetting.launcher_list, link) {
        if (surfaceId != launcher->icon_surface_id) {
            continue;
        }

        char *argv[] = {NULL};
        pid = execute_process(launcher->path, argv);

        if (0 < pid &&
            g_HmiSetting.surface_creator_path &&
            launcher->setid_window_titles.size) {
                uint32_t count = 0;
                char** title = NULL;

                wl_array_for_each(title, &launcher->setid_window_titles) {
                    assert(count < 128);
                    assert(0 == (pid & 0xff000000));
                    uint32_t id_surface = pid | 0x80000000 | (count << 24);
                    count++;
                    execute_ivi_surface_creator(
                        g_HmiSetting.surface_creator_path, pid, *title, id_surface);
                }
        }

        return 1;
    }

    return 0;
}

static int32_t
isWorkspaceSurface(uint32_t id)
{
    if (id == g_HmiSetting.workspace_background.id) {
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
PointerHandleButton(void* data, struct wl_pointer* wlPointer, uint32_t serial,
                    uint32_t time, uint32_t button, uint32_t state)
{
    (void)wlPointer;
    (void)serial;
    (void)time;
    struct wlContextCommon *pCtx = data;
    struct hmi_controller* hmiCtrl = pCtx->hmiController;

    if (BTN_RIGHT == button) {
        return;
    }

    const uint32_t id_surface = getIdOfWlSurface(pCtx, pCtx->enterSurface);

    switch (state) {
    case WL_POINTER_BUTTON_STATE_RELEASED:

        if (launcher_button(id_surface)) {
            hmi_controller_toggle_home(hmiCtrl);
        } else if (id_surface == g_HmiSetting.tiling.id) {
            hmi_controller_switch_mode(
            hmiCtrl, HMI_CONTROLLER_LAYOUT_MODE_TILING);
        } else if (id_surface == g_HmiSetting.sidebyside.id) {
            hmi_controller_switch_mode(
                    hmiCtrl, HMI_CONTROLLER_LAYOUT_MODE_SIDE_BY_SIDE);
        } else if (id_surface == g_HmiSetting.fullscreen.id) {
            hmi_controller_switch_mode(
                    hmiCtrl, HMI_CONTROLLER_LAYOUT_MODE_FULL_SCREEN);
        } else if (id_surface == g_HmiSetting.random.id) {
            hmi_controller_switch_mode(
                    hmiCtrl, HMI_CONTROLLER_LAYOUT_MODE_RANDOM);
        } else if (id_surface == g_HmiSetting.home.id) {
            hmi_controller_toggle_home(hmiCtrl);
        }

        break;

    case WL_POINTER_BUTTON_STATE_PRESSED:

        if (isWorkspaceSurface(id_surface)) {
            hmi_controller_workspace_swipe(hmiCtrl, pCtx->wlSeat, serial);
        }

        break;
    }
#ifdef _DEBUG
    printf("ENTER PointerHandleButton: button(%d), state(%d)\n", button, state);
#endif
}

static void
PointerHandleAxis(void* data, struct wl_pointer* wlPointer, uint32_t time,
                  uint32_t axis, wl_fixed_t value)
{
    (void)data;
    (void)wlPointer;
    (void)time;
#ifdef _DEBUG
    printf("ENTER PointerHandleAxis: axis(%d), value(%d)\n", axis, value);
#endif
}

static struct wl_pointer_listener pointer_listener = {
    PointerHandleEnter,
    PointerHandleLeave,
    PointerHandleMotion,
    PointerHandleButton,
    PointerHandleAxis
};

static void
TouchHandleDown(void *data, struct wl_touch *wlTouch, uint32_t serial, uint32_t time,
                struct wl_surface *surface, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
    struct wlContextCommon *pCtx = data;
    pCtx->enterSurface = surface;
}

static void
TouchHandleUp(void *data, struct wl_touch *wlTouch, uint32_t serial, uint32_t time,
              int32_t id)
{
    (void)serial;
    (void)time;
    struct wlContextCommon *pCtx = data;
    struct hmi_controller* hmiCtrl = pCtx->hmiController;

    const uint32_t id_surface = getIdOfWlSurface(pCtx, pCtx->enterSurface);

    if (id == 0){
        if (launcher_button(id_surface)) {
            hmi_controller_toggle_home(hmiCtrl);
        } else if (id_surface == g_HmiSetting.tiling.id) {
            hmi_controller_switch_mode(
            hmiCtrl, HMI_CONTROLLER_LAYOUT_MODE_TILING);
        } else if (id_surface == g_HmiSetting.sidebyside.id) {
            hmi_controller_switch_mode(
                    hmiCtrl, HMI_CONTROLLER_LAYOUT_MODE_SIDE_BY_SIDE);
        } else if (id_surface == g_HmiSetting.fullscreen.id) {
            hmi_controller_switch_mode(
                    hmiCtrl, HMI_CONTROLLER_LAYOUT_MODE_FULL_SCREEN);
        } else if (id_surface == g_HmiSetting.random.id) {
            hmi_controller_switch_mode(
                    hmiCtrl, HMI_CONTROLLER_LAYOUT_MODE_RANDOM);
        } else if (id_surface == g_HmiSetting.home.id) {
            hmi_controller_toggle_home(hmiCtrl);
        }
    }
}

static void
TouchHandleMotion(void *data, struct wl_touch *wlTouch, uint32_t time,
                  int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
}

static void
TouchHandleFrame(void *data, struct wl_touch *wlTouch)
{
}

static void
TouchHandleCancel(void *data, struct wl_touch *wlTouch)
{
}

static struct wl_touch_listener touch_listener = {
    TouchHandleDown,
    TouchHandleUp,
    TouchHandleMotion,
    TouchHandleFrame,
    TouchHandleCancel,
};

static void
seat_handle_capabilities(void* data, struct wl_seat* seat, uint32_t caps)
{
    (void)seat;
    struct wlContextCommon* p_wlCtx = (struct wlContextCommon*)data;
    struct wl_seat* wlSeat = p_wlCtx->wlSeat;
    struct wl_pointer* wlPointer = p_wlCtx->wlPointer;
    struct wl_touch* wlTouch = p_wlCtx->wlTouch;

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wlPointer){
        wlPointer = wl_seat_get_pointer(wlSeat);
        wl_pointer_set_user_data(wlPointer, data);
        wl_pointer_add_listener(wlPointer, &pointer_listener, data);
    } else
    if (!(caps & WL_SEAT_CAPABILITY_POINTER) && wlPointer){
        wl_pointer_destroy(wlPointer);
        wlPointer = NULL;
    }
    p_wlCtx->wlPointer = wlPointer;

    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !wlTouch){
        wlTouch = wl_seat_get_touch(wlSeat);
        wl_touch_set_user_data(wlTouch, data);
        wl_touch_add_listener(wlTouch, &touch_listener, data);
    } else
    if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && wlTouch){
        wl_touch_destroy(wlTouch);
        wlTouch = NULL;
    }
    p_wlCtx->wlTouch = wlTouch;
}

static struct wl_seat_listener seat_Listener = {
    seat_handle_capabilities,
};

static void
hmi_controller_workspace_end_swipe(void *data,
                                   struct hmi_controller *hmi_ctrl,
                                   uint32_t is_swipe)
{
    if (is_swipe) {
        return;
    }

    struct wlContextCommon *pCtx = data;
    const uint32_t id_surface = getIdOfWlSurface(pCtx, pCtx->enterSurface);

    if (launcher_button(id_surface)) {
        hmi_controller_toggle_home(hmi_ctrl);
    }
}

static const struct hmi_controller_listener hmi_controller_listener = {
    hmi_controller_workspace_end_swipe
};

static void
registry_handle_global(void* data, struct wl_registry* registry, uint32_t name,
                       const char *interface, uint32_t version)
{
    (void)version;
    struct wlContextCommon* p_wlCtx = (struct wlContextCommon*)data;

    do {
        if (!strcmp(interface, "wl_compositor")) {
            p_wlCtx->wlCompositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
            break;
        }
        if (!strcmp(interface, "wl_shm")) {
            p_wlCtx->wlShm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
            wl_shm_add_listener(p_wlCtx->wlShm, &shm_listenter, p_wlCtx);
            break;
        }
        if (!strcmp(interface, "wl_shell")) {
            p_wlCtx->wlShell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
            break;
        }
        if (!strcmp(interface, "wl_seat")) {
            p_wlCtx->wlSeat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
            wl_seat_add_listener(p_wlCtx->wlSeat, &seat_Listener, data);
            break;
        }
        if (!strcmp(interface, "ivi_application")) {
            p_wlCtx->iviApplication = wl_registry_bind(registry, name, &ivi_application_interface, 1);
            break;
        }
        if (!strcmp(interface, "hmi_controller")) {
            p_wlCtx->hmiController = wl_registry_bind(registry, name, &hmi_controller_interface, 1);

            if (p_wlCtx->hmiController) {
                hmi_controller_add_listener(p_wlCtx->hmiController, &hmi_controller_listener, p_wlCtx);
            }
            break;
        }

    } while(0);
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    NULL
};

static void
frame_listener_func(void *data, struct wl_callback *callback, uint32_t time)
{
    if (callback) {
        wl_callback_destroy(callback);
    }
}

static const struct wl_callback_listener frame_listener = {
    frame_listener_func
};

/*****************************************************************************
 *  local functions
 ****************************************************************************/
static void
createShmBuffer(struct wlContextStruct *p_wlCtx)
{
    struct wl_shm_pool *pool;

    char filename[] = "/tmp/wayland-shm-XXXXXX";
    int fd = -1;
    int size = 0;
    int width = 0;
    int height = 0;
    int stride = 0;

    fd = mkstemp(filename);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %m\n", filename);
        return;
    }

    width  = cairo_image_surface_get_width(p_wlCtx->ctx_image);
    height = cairo_image_surface_get_height(p_wlCtx->ctx_image);
    stride = cairo_image_surface_get_stride(p_wlCtx->ctx_image);

    size = stride * height;
    if (ftruncate(fd, size) < 0) {
        fprintf(stderr, "ftruncate failed: %m\n");
        close(fd);
        return;
    }

    p_wlCtx->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (MAP_FAILED == p_wlCtx->data) {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        return;
    }

    pool = wl_shm_create_pool(p_wlCtx->cmm.wlShm, fd, size);
    p_wlCtx->wlBuffer = wl_shm_pool_create_buffer(pool, 0,
                                                  width,
                                                  height,
                                                  stride,
                                                  WL_SHM_FORMAT_ARGB8888);

    if (NULL == p_wlCtx->wlBuffer) {
        fprintf(stderr, "wl_shm_create_buffer failed: %m\n");
        close(fd);
        return;
    }
    wl_shm_pool_destroy(pool);
    close(fd);

    return;
}

static void
destroyWLContextCommon(struct wlContextCommon *p_wlCtx)
{
    if (p_wlCtx->wlShell) {
        wl_shell_destroy(p_wlCtx->wlShell);
    }
    if (p_wlCtx->wlCompositor) {
        wl_compositor_destroy(p_wlCtx->wlCompositor);
    }
}

static void
destroyWLContextStruct(struct wlContextStruct *p_wlCtx)
{
    if (p_wlCtx->wlSurface) {
        wl_surface_destroy(p_wlCtx->wlSurface);
    }

    if (p_wlCtx->ctx_image) {
        cairo_surface_destroy(p_wlCtx->ctx_image);
        p_wlCtx->ctx_image = NULL;
    }
}
static void
handle_ping(void *data, struct wl_shell_surface *shell_surface,
            uint32_t serial)
{
    (void)data;
    wl_shell_surface_pong(shell_surface, serial);
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
                 uint32_t edges, int32_t width, int32_t height)
{
    (void)data;
    (void)shell_surface;
    (void)edges;
    (void)width;
    (void)height;
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
    (void)data;
    (void)shell_surface;
}

static const struct wl_shell_surface_listener shell_surface_listener = {
    handle_ping,
    handle_configure,
    handle_popup_done
};

static int
createWLContext(struct wlContextStruct *p_wlCtx)
{
    wl_display_roundtrip(p_wlCtx->cmm.wlDisplay);

    p_wlCtx->wlSurface = wl_compositor_create_surface(p_wlCtx->cmm.wlCompositor);
    if (NULL == p_wlCtx->wlSurface) {
        printf("Error: wl_compositor_create_surface failed.\n");
        destroyWLContextCommon(&p_wlCtx->cmm);
        abort();
    }

    p_wlCtx->wlShellSurface = wl_shell_get_shell_surface(p_wlCtx->cmm.wlShell,
                                                         p_wlCtx->wlSurface);

    wl_shell_surface_add_listener(p_wlCtx->wlShellSurface,
                                  &shell_surface_listener, p_wlCtx);

    createShmBuffer(p_wlCtx);

    wl_shell_surface_set_title(p_wlCtx->wlShellSurface, "");
    wl_shell_surface_set_toplevel(p_wlCtx->wlShellSurface);
    wl_display_flush(p_wlCtx->cmm.wlDisplay);
    wl_display_roundtrip(p_wlCtx->cmm.wlDisplay);

    return 0;
}

static void
drawImage(struct wlContextStruct *p_wlCtx)
{
    struct wl_callback *callback;

    int width = 0;
    int height = 0;
    int stride = 0;
    void *data = NULL;

    width = cairo_image_surface_get_width(p_wlCtx->ctx_image);
    height = cairo_image_surface_get_height(p_wlCtx->ctx_image);
    stride = cairo_image_surface_get_stride(p_wlCtx->ctx_image);
    data = cairo_image_surface_get_data(p_wlCtx->ctx_image);

    memcpy(p_wlCtx->data, data, stride * height);

    wl_surface_attach(p_wlCtx->wlSurface, p_wlCtx->wlBuffer, 0, 0);
    wl_surface_damage(p_wlCtx->wlSurface, 0, 0, width, height);

    callback = wl_surface_frame(p_wlCtx->wlSurface);
    wl_callback_add_listener(callback, &frame_listener, NULL);

    wl_surface_commit(p_wlCtx->wlSurface);

    wl_display_flush(p_wlCtx->cmm.wlDisplay);
    wl_display_roundtrip(p_wlCtx->cmm.wlDisplay);
}

static void
create_ivisurface(struct wlContextStruct *p_wlCtx,
                  uint32_t id_surface,
                  cairo_surface_t* surface)
{
    struct ivi_surface *ivisurf = NULL;

    p_wlCtx->ctx_image = surface;

    p_wlCtx->id_surface = id_surface;
    wl_list_init(&p_wlCtx->link);
    wl_list_insert(p_wlCtx->cmm.list_wlContextStruct, &p_wlCtx->link);

    createWLContext(p_wlCtx);

    ivisurf = ivi_application_surface_create(p_wlCtx->cmm.iviApplication,
                                             id_surface, p_wlCtx->wlSurface);
    if (ivisurf == NULL) {
        fprintf(stderr, "Failed to create ivi_client_surface\n");
        return;
    }

    drawImage(p_wlCtx);

    wl_display_roundtrip(p_wlCtx->cmm.wlDisplay);
}

static void
create_ivisurfaceFromFile(struct wlContextStruct *p_wlCtx,
                          uint32_t id_surface,
                          const char* imageFile)
{
    cairo_surface_t* surface = load_cairo_surface(imageFile);

    if (NULL == surface) {
        fprintf(stderr, "Failed to load_cairo_surface %s\n", imageFile);
        return;
    }

    create_ivisurface(p_wlCtx, id_surface, surface);
}

static void
set_hex_color(cairo_t *cr, uint32_t color)
{
    cairo_set_source_rgba(cr,
        ((color >> 16) & 0xff) / 255.0,
        ((color >>  8) & 0xff) / 255.0,
        ((color >>  0) & 0xff) / 255.0,
        ((color >> 24) & 0xff) / 255.0);
}

static void
create_ivisurfaceFromColor(struct wlContextStruct *p_wlCtx,
                           uint32_t id_surface,
                           uint32_t width, uint32_t height,
                           uint32_t color)
{
    cairo_surface_t *surface = NULL;
    cairo_t *cr = NULL;

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_rectangle(cr, 0, 0, width, height);
    set_hex_color(cr, color);
    cairo_fill(cr);
    cairo_destroy(cr);

    create_ivisurface(p_wlCtx, id_surface, surface);
}

static void
create_background(struct wlContextStruct *p_wlCtx, const uint32_t id_surface,
                  const char* imageFile)
{
    create_ivisurfaceFromFile(p_wlCtx, id_surface, imageFile);
    hmi_controller_set_background(p_wlCtx->cmm.hmiController, id_surface);
}

static void
create_panel(struct wlContextStruct *p_wlCtx, const uint32_t id_surface,
             const char* imageFile)
{
    create_ivisurfaceFromFile(p_wlCtx, id_surface, imageFile);
    hmi_controller_set_panel(p_wlCtx->cmm.hmiController, id_surface);
}

static void
create_button(struct wlContextStruct *p_wlCtx, const uint32_t id_surface,
              const char* imageFile, uint32_t number)
{
    create_ivisurfaceFromFile(p_wlCtx, id_surface, imageFile);
    hmi_controller_set_button(p_wlCtx->cmm.hmiController, id_surface, number);
}

static void
create_home_button(struct wlContextStruct *p_wlCtx, const uint32_t id_surface,
                   const char* imageFile)
{
    create_ivisurfaceFromFile(p_wlCtx, id_surface, imageFile);
    hmi_controller_set_home_button(p_wlCtx->cmm.hmiController, id_surface);
}

static void
create_workspace_background(
    struct wlContextStruct *p_wlCtx, struct hmi_controller_srfInfo *srfInfo)
{
    create_ivisurfaceFromColor(p_wlCtx, srfInfo->id, 1, 1, srfInfo->color);
    hmi_controller_set_workspacebackground(p_wlCtx->cmm.hmiController, srfInfo->id);
}

static int compare_launcher(const void* data1, const void* data2)
{
    struct hmi_controller_launcher *launcher1 = *(struct hmi_controller_launcher**)data1;
    struct hmi_controller_launcher *launcher2 = *(struct hmi_controller_launcher**)data2;
    return launcher1->workspace_id - launcher2->workspace_id;
}

static void
create_launchers(struct wlContextCommon *cmm)
{
    int launcher_count = wl_list_length(&g_HmiSetting.launcher_list);

    if (0 == launcher_count) {
        return;
    }

    struct hmi_controller_launcher** launchers;
    launchers = calloc(launcher_count, sizeof(*launchers));
    assert(launchers);

    int ii = 0;
    struct hmi_controller_launcher *launcher = NULL;

    wl_list_for_each(launcher, &g_HmiSetting.launcher_list, link) {
        launchers[ii] = launcher;
        ii++;
    }

    qsort(launchers, launcher_count, sizeof(*launchers), compare_launcher);

    int start = 0;

    for (ii = 0; ii < launcher_count; ii++) {

        if (ii != launcher_count -1 &&
            launchers[ii]->workspace_id == launchers[ii + 1]->workspace_id) {
            continue;
        }

        struct wl_array    surface_ids;
        wl_array_init(&surface_ids);

        int jj = 0;
        for (jj = start; jj <= ii; jj++) {
            uint32_t *id = wl_array_add(&surface_ids, sizeof(*id));
            *id = launchers[jj]->icon_surface_id;

            struct wlContextStruct *p_wlCtx = calloc(1, sizeof(*p_wlCtx));
            assert(p_wlCtx);
            p_wlCtx->cmm = *cmm;

            create_ivisurfaceFromFile(p_wlCtx, *id, launchers[jj]->icon);
        }

        hmi_controller_add_launchers(cmm->hmiController, &surface_ids, 256);
        wl_array_release(&surface_ids);
        start = ii + 1;
    }

    free(launchers);
}

static void sigFunc(int signum)
{
    gRun = 0;
}

static void*
client_thread(void *p_ret)
{
    struct wlContextCommon wlCtxCommon;

    struct wlContextStruct wlCtx_BackGround;
    struct wlContextStruct wlCtx_Panel;
    struct wlContextStruct wlCtx_Button_1;
    struct wlContextStruct wlCtx_Button_2;
    struct wlContextStruct wlCtx_Button_3;
    struct wlContextStruct wlCtx_Button_4;
    struct wlContextStruct wlCtx_HomeButton;
    struct wlContextStruct wlCtx_WorkSpaceBackGround;
    struct wl_list         launcher_wlCtxList;

    memset(&wlCtxCommon, 0x00, sizeof(wlCtxCommon));
    memset(&wlCtx_BackGround, 0x00, sizeof(wlCtx_BackGround));
    memset(&wlCtx_Panel,      0x00, sizeof(wlCtx_Panel));
    memset(&wlCtx_Button_1,   0x00, sizeof(wlCtx_Button_1));
    memset(&wlCtx_Button_2,   0x00, sizeof(wlCtx_Button_2));
    memset(&wlCtx_Button_3,   0x00, sizeof(wlCtx_Button_3));
    memset(&wlCtx_Button_4,   0x00, sizeof(wlCtx_Button_4));
    memset(&wlCtx_HomeButton, 0x00, sizeof(wlCtx_HomeButton));
    memset(&wlCtx_WorkSpaceBackGround, 0x00, sizeof(wlCtx_WorkSpaceBackGround));
    wl_list_init(&launcher_wlCtxList);
    wlCtxCommon.list_wlContextStruct = calloc(1, sizeof(struct wl_list));
    assert(wlCtxCommon.list_wlContextStruct);
    wl_list_init(wlCtxCommon.list_wlContextStruct);

    gRun = 1;

    wlCtxCommon.wlDisplay = wl_display_connect(NULL);
    if (NULL == wlCtxCommon.wlDisplay) {
        printf("Error: wl_display_connect failed.\n");
        return NULL;
    }

    /* get wl_registry */
    wlCtxCommon.wlRegistry = wl_display_get_registry(wlCtxCommon.wlDisplay);
    wl_registry_add_listener(wlCtxCommon.wlRegistry,
                             &registry_listener, &wlCtxCommon);
    wl_display_dispatch(wlCtxCommon.wlDisplay);
    wl_display_roundtrip(wlCtxCommon.wlDisplay);

    wlCtx_BackGround.cmm = wlCtxCommon;
    wlCtx_Panel.cmm      = wlCtxCommon;
    wlCtx_Button_1.cmm   = wlCtxCommon;
    wlCtx_Button_2.cmm   = wlCtxCommon;
    wlCtx_Button_3.cmm   = wlCtxCommon;
    wlCtx_Button_4.cmm   = wlCtxCommon;
    wlCtx_HomeButton.cmm = wlCtxCommon;
    wlCtx_WorkSpaceBackGround.cmm = wlCtxCommon;

    /* create desktop widgets */
    create_background(&wlCtx_BackGround, g_HmiSetting.background.id,
                      g_HmiSetting.background.filePath);

    create_panel(&wlCtx_Panel, g_HmiSetting.panel.id,
                 g_HmiSetting.panel.filePath);

    create_button(&wlCtx_Button_1, g_HmiSetting.tiling.id,
                  g_HmiSetting.tiling.filePath, 0);

    create_button(&wlCtx_Button_2, g_HmiSetting.sidebyside.id,
                  g_HmiSetting.sidebyside.filePath, 1);

    create_button(&wlCtx_Button_3, g_HmiSetting.fullscreen.id,
                  g_HmiSetting.fullscreen.filePath, 2);

    create_button(&wlCtx_Button_4, g_HmiSetting.random.id,
                  g_HmiSetting.random.filePath, 3);

    create_home_button(&wlCtx_HomeButton, g_HmiSetting.home.id,
                       g_HmiSetting.home.filePath);

    create_workspace_background(&wlCtx_WorkSpaceBackGround,
                        &g_HmiSetting.workspace_background);

    create_launchers(&wlCtxCommon);

    /* signal handling */
    signal(SIGINT,  sigFunc);
    signal(SIGKILL, sigFunc);

    while(gRun) {
        wl_display_roundtrip(wlCtxCommon.wlDisplay);
        usleep(5000);
    }

    struct wlContextStruct* pWlCtxSt = NULL;
    wl_list_for_each(pWlCtxSt, wlCtxCommon.list_wlContextStruct, link) {
        destroyWLContextStruct(pWlCtxSt);
    }

    destroyWLContextCommon(&wlCtxCommon);
    free(wlCtxCommon.list_wlContextStruct);

    return NULL;
}

/*****************************************************************************
 *  exported functions
 ****************************************************************************/
int
hmi_client_start(void)
{
    pthread_attr_t thread_attrs;
    uint32_t ret = 0;

    pthread_attr_init(&thread_attrs);
    pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_JOINABLE);
    ret = pthread_create(&thread, &thread_attrs, client_thread, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to start internal receive \
                thread. returned %d\n", ret);
    }

    return 0;
}
