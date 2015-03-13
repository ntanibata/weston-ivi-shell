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

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

#include "weston-test-client-helper.h"
#include "ivi-application-client-protocol.h"
#include "ivi-shell-test-client-protocol.h"

struct data
{
	bool done;
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct ivi_application *ivi_application;
	struct ivi_shell_test *ivi_shell_test;
	struct wl_list surface_list; // struct surface_info::link
};

struct surface_info
{
	struct wl_list link; // struct data::surface_list
	uint32_t id;
	struct ivi_surface *surface;
};

static struct surface_info *
create_surface_info(uint32_t id, struct ivi_surface *surface)
{
	struct surface_info *info = malloc(sizeof(*info));
	info->id = id;
	info->surface = surface;
	return info;
}

static void
exit_ivi_shell(void *data,
	       struct ivi_shell_test *ivi_shell_test)
{
	struct data *d = data;
	d->done = true;
}

/*****************************************************************************
 *  This craetes ivi-surface requeted by ivi-shell-test from server side.
 ****************************************************************************/
static void
request_create_ivi_layout_surface(void *data,
				  struct ivi_shell_test *ivi_shell_test,
				  uint32_t surface_id)
{
	struct data *d = data;
	struct wl_surface *surface;
	struct ivi_surface *ivisurf;
	struct surface_info *info;

	surface = wl_compositor_create_surface(d->compositor);
	wl_display_roundtrip(d->display);

	ivisurf = ivi_application_surface_create(d->ivi_application, surface_id, surface);
	wl_display_roundtrip(d->display);

	if (ivisurf) {
		info = create_surface_info(surface_id, ivisurf);
		wl_list_insert(&d->surface_list, &info->link);
	}

	ivi_shell_test_reply_create_ivi_layout_surface(d->ivi_shell_test, surface_id);
	wl_display_roundtrip(d->display);
}

/*****************************************************************************
 *  This removes ivi-surface requeted by ivi-shell-test from server side.
 ****************************************************************************/
static void
request_remove_ivi_layout_surface(void *data,
				  struct ivi_shell_test *ivi_shell_test,
				  uint32_t surface_id)
{
	struct data *d = data;
	struct surface_info *link;
	struct surface_info *next;

	wl_list_for_each_safe(link, next, &d->surface_list, link) {
		if (link->id == surface_id) {
			wl_list_remove(&link->link);
			ivi_surface_destroy(link->surface);
			wl_display_roundtrip(d->display);
			free(link);
		}
	}

	ivi_shell_test_reply_remove_ivi_layout_surface(d->ivi_shell_test, surface_id);
	wl_display_roundtrip(d->display);
}

struct ivi_shell_test_listener shell_test_listener = {
	exit_ivi_shell,
	request_create_ivi_layout_surface,
	request_remove_ivi_layout_surface
};

static void
handle_global(void *data, struct wl_registry *registry,
	      uint32_t id, const char *interface, uint32_t version)
{
	struct data *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry, id,
					 &wl_compositor_interface, 1);

		assert(d->compositor && " Failed to bind wl_compositor protocol.");
	}
	else if (strcmp(interface, "ivi_application") == 0) {
		d->ivi_application =
			wl_registry_bind(registry, id,
					 &ivi_application_interface, 1);

		assert(d->ivi_application && " Failed to bind ivi_application protocol.");
	}
	else if (strcmp(interface, "ivi_shell_test") == 0) {
		d->ivi_shell_test =
			wl_registry_bind(registry, id,
					 &ivi_shell_test_interface, 1);

		assert(d->ivi_shell_test && " Failed to bind ivi_shell_test protocol.");
		ivi_shell_test_add_listener(d->ivi_shell_test, &shell_test_listener, d);
	}
}

static const struct wl_registry_listener registry_listener = {
	handle_global
};

/*****************************************************************************
 *  This is test client to invoke test of ivi-shell by using "ivi_shell_test".
 ****************************************************************************/
TEST(ivi_layout_test_client)
{
	static struct data d = {};
	wl_list_init(&d.surface_list);

	d.display = wl_display_connect(NULL);

	d.registry = wl_display_get_registry(d.display);
	wl_registry_add_listener(d.registry, &registry_listener, &d);

	wl_display_dispatch(d.display);
	wl_display_roundtrip(d.display);

	/*
	  start ivi-shell testing via ivi_shell_test.request_start_ivi_shell_test
	*/
	ivi_shell_test_request_start_ivi_shell_test(d.ivi_shell_test);

	while (!d.done) {
		if (wl_display_dispatch(d.display) < 0) {
			break;
		}
	}

	exit(EXIT_SUCCESS);
}
