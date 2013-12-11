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
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <getopt.h>
#include <pthread.h>

#include "hmi-bmpaccessor.h"
#include "hmi-controller.h"
#include "ivi-application-client-protocol.h"

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
    struct ivi_application *iviApplication;

    struct wl_list         *list_wlContextStruct;
    struct wl_surface      *enterSurface;
};

struct wlContextStruct {
    struct wlContextCommon  cmm;
    struct wl_surface       *wlSurface;
    struct wl_shell_surface *wlShellSurface;
    struct wl_buffer        *wlBuffer;
    uint32_t                formats;
    ctx_bmpaccessor         *ctx_bmp;
    void                    *data;
    uint32_t                id_surface;
    struct wl_list          link;
};

pthread_t thread;

volatile int gRun = 0;

int32_t gPointerX = 0;
int32_t gPointerY = 0;

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
    (void)data;
    (void)wlPointer;
    (void)wlSurface;
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
    gPointerX = (int)wl_fixed_to_double(sx);
    gPointerY = (int)wl_fixed_to_double(sy);
#ifdef _DEBUG
    printf("ENTER PointerHandleMotion: x(%d), y(%d)\n", gPointerX, gPointerY);
#endif
}

static void
PointerHandleButton(void* data, struct wl_pointer* wlPointer, uint32_t serial,
                    uint32_t time, uint32_t button, uint32_t state)
{
    (void)wlPointer;
    (void)serial;
    (void)time;
    if (state == 1) {
        /* when press any button */
        struct wlContextCommon *pCtx = data;
        uint32_t id_surface = getIdOfWlSurface(pCtx, pCtx->enterSurface);
        pointer_button_event(id_surface);
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
seat_handle_capabilities(void* data, struct wl_seat* seat, uint32_t caps)
{
    (void)seat;
    struct wlContextCommon* p_wlCtx = (struct wlContextCommon*)data;
    struct wl_seat* wlSeat = p_wlCtx->wlSeat;
    struct wl_pointer* wlPointer = p_wlCtx->wlPointer;

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
}

static struct wl_seat_listener seat_Listener = {
    seat_handle_capabilities,
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

    fd = mkstemp(filename);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %m\n", filename);
        return;
    }
    size = p_wlCtx->ctx_bmp->stride * p_wlCtx->ctx_bmp->height;
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
                                                  p_wlCtx->ctx_bmp->width,
                                                  p_wlCtx->ctx_bmp->height,
                                                  p_wlCtx->ctx_bmp->stride,
                                                  WL_SHM_FORMAT_XRGB8888);

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

    memcpy(p_wlCtx->data, p_wlCtx->ctx_bmp->data,
           p_wlCtx->ctx_bmp->stride * p_wlCtx->ctx_bmp->height);

    wl_surface_attach(p_wlCtx->wlSurface, p_wlCtx->wlBuffer, 0, 0);
    wl_surface_damage(p_wlCtx->wlSurface, 0, 0,
                      p_wlCtx->ctx_bmp->width,
                      p_wlCtx->ctx_bmp->height);

    callback = wl_surface_frame(p_wlCtx->wlSurface);
    wl_callback_add_listener(callback, &frame_listener, NULL);

    wl_surface_commit(p_wlCtx->wlSurface);

    wl_display_flush(p_wlCtx->cmm.wlDisplay);
    wl_display_roundtrip(p_wlCtx->cmm.wlDisplay);
}

static void
create_ivisurface(struct wlContextStruct *p_wlCtx,
                  uint32_t id_surface,
                  const char* bitmapFile)
{
    struct ivi_surface *ivisurf = NULL;

    p_wlCtx->ctx_bmp = bmpaccessor_open(bitmapFile);
    if (NULL == p_wlCtx->ctx_bmp) {
        fprintf(stderr, "Failed to create bmp accessor\n");
        return;
    }

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
create_background(struct wlContextStruct *p_wlCtx, const uint32_t id_surface,
                  const char* bitmapFile)
{
    create_ivisurface(p_wlCtx, id_surface, bitmapFile);
    hmi_create_background(id_surface);
}

static void
create_panel(struct wlContextStruct *p_wlCtx, const uint32_t id_surface,
             const char* bitmapFile)
{
    create_ivisurface(p_wlCtx, id_surface, bitmapFile);
    hmi_create_panel(id_surface);
}

static void
create_button(struct wlContextStruct *p_wlCtx, const uint32_t id_surface,
              const char* bitmapFile, uint32_t number)
{
    create_ivisurface(p_wlCtx, id_surface, bitmapFile);
    hmi_create_button(id_surface, number);
}

static void sigFunc(int signum)
{
    gRun = 0;
}

static void*
client_thread(void *p_ret)
{
#if 1
    /* wait until ivi-shell is created */
    sleep(1);
#endif
    const char* bitmap_BACKGROUND = "/opt/weston_ivi_shell/background.bmp";
    const char* bitmap_PANEL      = "/opt/weston_ivi_shell/panel.bmp";
    const char* bitmap_BUTTON_1   = "/opt/weston_ivi_shell/eight.bmp";
    const char* bitmap_BUTTON_2   = "/opt/weston_ivi_shell/twin.bmp";
    const char* bitmap_BUTTON_3   = "/opt/weston_ivi_shell/once.bmp";
    const char* bitmap_BUTTON_4   = "/opt/weston_ivi_shell/random.bmp";

    struct wlContextCommon wlCtxCommon;

    struct wlContextStruct wlCtx_BackGround;
    struct wlContextStruct wlCtx_Panel;
    struct wlContextStruct wlCtx_Button_1;
    struct wlContextStruct wlCtx_Button_2;
    struct wlContextStruct wlCtx_Button_3;
    struct wlContextStruct wlCtx_Button_4;

    memset(&wlCtxCommon, 0x00, sizeof(wlCtxCommon));
    memset(&wlCtx_BackGround, 0x00, sizeof(wlCtx_BackGround));
    memset(&wlCtx_Panel,      0x00, sizeof(wlCtx_Panel));
    memset(&wlCtx_Button_1,   0x00, sizeof(wlCtx_Button_1));
    memset(&wlCtx_Button_2,   0x00, sizeof(wlCtx_Button_2));
    memset(&wlCtx_Button_3,   0x00, sizeof(wlCtx_Button_3));
    memset(&wlCtx_Button_4,   0x00, sizeof(wlCtx_Button_4));

    wlCtxCommon.list_wlContextStruct = calloc(1, sizeof(struct wl_list));
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

    /* create desktop widgets */
    create_background(&wlCtx_BackGround, ID_SURF_BACKGROUND, bitmap_BACKGROUND);
    create_panel(&wlCtx_Panel, ID_SURF_PANEL, bitmap_PANEL);
    create_button(&wlCtx_Button_1, MODE_DIVIDED_INTO_EIGHT, bitmap_BUTTON_1, 0);
    create_button(&wlCtx_Button_2, MODE_DIVIDED_INTO_TWO,   bitmap_BUTTON_2, 1);
    create_button(&wlCtx_Button_3, MODE_MAXIMIZE_SOMEONE,   bitmap_BUTTON_3, 2);
    create_button(&wlCtx_Button_4, MODE_RANDOM_REPLACE,     bitmap_BUTTON_4, 3);

    /* signal handling */
    signal(SIGINT,  sigFunc);
    signal(SIGKILL, sigFunc);

    while(gRun) {
        wl_display_roundtrip(wlCtxCommon.wlDisplay);
        usleep(5000);
    }

    free(wlCtxCommon.list_wlContextStruct);
    destroyWLContextCommon(&wlCtxCommon);
    destroyWLContextStruct(&wlCtx_BackGround);
    destroyWLContextStruct(&wlCtx_Panel);
    destroyWLContextStruct(&wlCtx_Button_1);
    destroyWLContextStruct(&wlCtx_Button_2);
    destroyWLContextStruct(&wlCtx_Button_3);
    destroyWLContextStruct(&wlCtx_Button_4);
    bmpaccessor_close(wlCtx_BackGround.ctx_bmp);

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
