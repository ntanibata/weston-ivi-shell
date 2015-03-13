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

#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "ivi-shell/ivi-layout-export.h"
#include "ivi-shell-test-server-protocol.h"

#define ivi_test_assert(d, cond) \
{ \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: %s: Assertion `%s` failed.\n", __FILE__, __LINE__, __func__, #cond); \
		(d)->failed = true; \
		return; \
	} \
}

struct data
{
	struct weston_compositor *compositor;
	struct weston_process process;
	const struct ivi_controller_interface *interface;
	struct wl_resource *resource;
	bool failed;
};

static pthread_t thread;
static pthread_cond_t thread_cond;
static pthread_mutex_t thread_mutex;

int
controller_module_init(struct weston_compositor *ec,
		       int *argc, char *argv[],
		       const struct ivi_controller_interface *interface,
		       size_t interface_version);


/*****************************************************************************
 *  Common internal APIs to be used by all tests.
 ****************************************************************************/
static void
create_surface_sync(struct data *d, uint32_t surface_id)
{
	pthread_mutex_init(&thread_mutex, NULL);
	pthread_cond_init(&thread_cond, NULL);

	pthread_mutex_lock(&thread_mutex);
	ivi_shell_test_send_request_create_ivi_layout_surface(d->resource, surface_id);
	pthread_cond_wait(&thread_cond, &thread_mutex);
	pthread_mutex_unlock(&thread_mutex);

	pthread_cond_destroy(&thread_cond);
	pthread_mutex_destroy(&thread_mutex);

	d->interface->commit_changes();
}

static void
destroy_surface_sync(struct data *d, uint32_t surface_id)
{
	pthread_mutex_init(&thread_mutex, NULL);
	pthread_cond_init(&thread_cond, NULL);

	pthread_mutex_lock(&thread_mutex);
	ivi_shell_test_send_request_remove_ivi_layout_surface(d->resource, surface_id);
	pthread_cond_wait(&thread_cond, &thread_mutex);
	pthread_mutex_unlock(&thread_mutex);

	pthread_cond_destroy(&thread_cond);
	pthread_mutex_destroy(&thread_mutex);

	d->interface->commit_changes();
}

static void
cleanup_surfaces(struct data *d, const uint32_t *surfaces, uint32_t size)
{
	uint32_t i;
	for (i = 0; i < size; ++i) {
		destroy_surface_sync(d, surfaces[i]);
	}
}

static void
test_client_sigchld(struct weston_process *process, int status)
{
	struct data *d = container_of(process, struct data, process);

	if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
		exit(WEXITSTATUS(status));

	ivi_test_assert(d, status == 0);

	wl_display_terminate(d->compositor->wl_display);
	exit(d->failed ? EXIT_FAILURE : EXIT_SUCCESS);
}

/*****************************************************************************
 *  Tests for ivi-surface
 ****************************************************************************/
static void
test_surface_create(struct data *d)
{
	static const uint32_t surfaces[] = { 1001, 1002 };
	uint32_t id1;
	uint32_t id2;
	struct ivi_layout_surface *ivisurf;
	struct ivi_layout_surface *new_ivisurf;
	struct ivi_layout_surface *destroy_ivisurf;

	create_surface_sync(d, surfaces[0]);
	create_surface_sync(d, surfaces[1]);

	ivisurf = d->interface->get_surface_from_id(surfaces[0]);
	ivi_test_assert(d, NULL != ivisurf);

	new_ivisurf = d->interface->get_surface_from_id(surfaces[1]);
	ivi_test_assert(d, NULL != new_ivisurf);

	id1 = d->interface->get_id_of_surface(ivisurf);
	ivi_test_assert(d, surfaces[0] == id1);

	id2 = d->interface->get_id_of_surface(new_ivisurf);
	ivi_test_assert(d, surfaces[1] == id2);

	destroy_surface_sync(d, surfaces[0]);
	destroy_ivisurf = d->interface->get_surface_from_id(surfaces[0]);
	ivi_test_assert(d, destroy_ivisurf == NULL);

	cleanup_surfaces(d, surfaces, sizeof(surfaces) / sizeof(surfaces[0]));
}

static void
test_surface_visibility(struct data *d)
{
	static const uint32_t id_surface = 1003;
	bool visibility;
	struct ivi_layout_surface *ivisurf;
	const struct ivi_layout_surface_properties *prop;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_visibility(
		ivisurf, true) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	visibility = d->interface->surface_get_visibility(ivisurf);
	ivi_test_assert(d, visibility == true);

	prop = d->interface->get_properties_of_surface(ivisurf);
	ivi_test_assert(d, prop->visibility == true);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_opacity(struct data *d)
{
	static const uint32_t id_surface = 1004;
	wl_fixed_t opacity;
	struct ivi_layout_surface *ivisurf;
	const struct ivi_layout_surface_properties *prop;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_opacity(
		ivisurf, wl_fixed_from_double(0.5)) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	opacity = d->interface->surface_get_opacity(ivisurf);
	ivi_test_assert(d, opacity == wl_fixed_from_double(0.5));

	prop = d->interface->get_properties_of_surface(ivisurf);
	ivi_test_assert(d, prop->opacity == wl_fixed_from_double(0.5));

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_orientation(struct data *d)
{
	static const uint32_t id_surface = 1005;
	struct ivi_layout_surface *ivisurf;
	const struct ivi_layout_surface_properties *prop;
	enum wl_output_transform orientation;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_orientation(
		ivisurf, WL_OUTPUT_TRANSFORM_90) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	orientation = d->interface->surface_get_orientation(ivisurf);
	ivi_test_assert(d, orientation == WL_OUTPUT_TRANSFORM_90);

	prop = d->interface->get_properties_of_surface(ivisurf);
	ivi_test_assert(d, prop->orientation == WL_OUTPUT_TRANSFORM_90);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_dimension(struct data *d)
{
	static const uint32_t id_surface = 1006;
	struct ivi_layout_surface *ivisurf;
	const struct ivi_layout_surface_properties *prop;
	int32_t dest_width;
	int32_t dest_height;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_dimension(
		ivisurf, 200, 300) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->surface_get_dimension(
		ivisurf, &dest_width, &dest_height) == IVI_SUCCEEDED);
	ivi_test_assert(d, dest_width == 200);
	ivi_test_assert(d, dest_height == 300);

	prop = d->interface->get_properties_of_surface(ivisurf);
	ivi_test_assert(d, prop->dest_width == 200);
	ivi_test_assert(d, prop->dest_height == 300);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_position(struct data *d)
{
	static const uint32_t id_surface = 1007;
	struct ivi_layout_surface *ivisurf;
	const struct ivi_layout_surface_properties *prop;
	int32_t dest_x;
	int32_t dest_y;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_position(
		ivisurf, 20, 30) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->surface_get_position(
		ivisurf, &dest_x, &dest_y) == IVI_SUCCEEDED);
	ivi_test_assert(d, dest_x == 20);
	ivi_test_assert(d, dest_y == 30);

	prop = d->interface->get_properties_of_surface(ivisurf);
	ivi_test_assert(d, prop->dest_x == 20);
	ivi_test_assert(d, prop->dest_y == 30);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_destination(struct data *d)
{
	static const uint32_t id_surface = 1008;
	struct ivi_layout_surface *ivisurf;
	const struct ivi_layout_surface_properties *prop;
	int32_t dest_width;
	int32_t dest_height;
	int32_t dest_x;
	int32_t dest_y;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_destination_rectangle(
		ivisurf, 20, 30, 200, 300) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->surface_get_dimension(
		ivisurf, &dest_width, &dest_height) == IVI_SUCCEEDED);
	ivi_test_assert(d, dest_width == 200);
	ivi_test_assert(d, dest_height == 300);

	ivi_test_assert(d, d->interface->surface_get_position(
		ivisurf, &dest_x, &dest_y) == IVI_SUCCEEDED);
	ivi_test_assert(d, dest_x == 20);
	ivi_test_assert(d, dest_y == 30);

	prop = d->interface->get_properties_of_surface(ivisurf);
	ivi_test_assert(d, prop->dest_width == 200);
	ivi_test_assert(d, prop->dest_height == 300);
	ivi_test_assert(d, prop->dest_x == 20);
	ivi_test_assert(d, prop->dest_y == 30);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_source_rectangle(struct data *d)
{
	static const uint32_t id_surface = 1009;
	struct ivi_layout_surface *ivisurf;
	const struct ivi_layout_surface_properties *prop;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_source_rectangle(
		ivisurf, 20, 30, 200, 300) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	prop = d->interface->get_properties_of_surface(ivisurf);
	ivi_test_assert(d, prop->source_width == 200);
	ivi_test_assert(d, prop->source_height == 300);
	ivi_test_assert(d, prop->source_x == 20);
	ivi_test_assert(d, prop->source_y == 30);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_bad_visibility(struct data *d)
{
	bool visibility;

	ivi_test_assert(d, d->interface->surface_set_visibility(
		NULL, true) == IVI_FAILED);

	d->interface->commit_changes();

	visibility = d->interface->surface_get_visibility(NULL);
	ivi_test_assert(d, visibility == false);
}

static void
test_surface_bad_opacity(struct data *d)
{
	static const uint32_t id_surface = 1010;
	wl_fixed_t opacity;
	struct ivi_layout_surface *ivisurf;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_opacity(
		ivisurf, wl_fixed_from_double(0.3)) == IVI_SUCCEEDED);

	ivi_test_assert(d, d->interface->surface_set_opacity(
		ivisurf, wl_fixed_from_double(-1)) == IVI_FAILED);

	d->interface->commit_changes();

	opacity = d->interface->surface_get_opacity(ivisurf);
	ivi_test_assert(d, opacity == wl_fixed_from_double(0.3));

	ivi_test_assert(d, d->interface->surface_set_opacity(
		ivisurf, wl_fixed_from_double(1.1)) == IVI_FAILED);

	d->interface->commit_changes();

	opacity = d->interface->surface_get_opacity(ivisurf);
	ivi_test_assert(d, opacity == wl_fixed_from_double(0.3));

	ivi_test_assert(d, d->interface->surface_set_opacity(
		NULL, wl_fixed_from_double(0.5)) == IVI_FAILED);

	d->interface->commit_changes();

	opacity = d->interface->surface_get_opacity(NULL);
	ivi_test_assert(d, opacity == wl_fixed_from_double(0.0));

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_bad_destination(struct data *d)
{
	ivi_test_assert(d, d->interface->surface_set_destination_rectangle(
		NULL, 20, 30, 200, 300) == IVI_FAILED);
}

static void
test_surface_bad_orientation(struct data *d)
{
	enum wl_output_transform orientation;

	ivi_test_assert(d, d->interface->surface_set_orientation(
		NULL, WL_OUTPUT_TRANSFORM_90) == IVI_FAILED);

	orientation = d->interface->surface_get_orientation(NULL);
	ivi_test_assert(d, orientation == WL_OUTPUT_TRANSFORM_NORMAL);
}

static void
test_surface_bad_dimension(struct data *d)
{
	static const uint32_t id_surface = 1011;
	struct ivi_layout_surface *ivisurf;
	int32_t dest_width;
	int32_t dest_height;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_dimension(
		NULL, 200, 300) == IVI_FAILED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->surface_get_dimension(
		NULL, &dest_width, &dest_height) == IVI_FAILED);
	ivi_test_assert(d, d->interface->surface_get_dimension(
		ivisurf, NULL, &dest_height) == IVI_FAILED);
	ivi_test_assert(d, d->interface->surface_get_dimension(
		ivisurf, &dest_width, NULL) == IVI_FAILED);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_bad_position(struct data *d)
{
	static const uint32_t id_surface = 1012;
	struct ivi_layout_surface *ivisurf;
	int32_t dest_x;
	int32_t dest_y;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_set_position(
		NULL, 20, 30) == IVI_FAILED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->surface_get_position(
		NULL, &dest_x, &dest_y) == IVI_FAILED);
	ivi_test_assert(d, d->interface->surface_get_position(
		ivisurf, NULL, &dest_y) == IVI_FAILED);
	ivi_test_assert(d, d->interface->surface_get_position(
		ivisurf, &dest_x, NULL) == IVI_FAILED);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_bad_source_rectangle(struct data *d)
{
	ivi_test_assert(d, d->interface->surface_set_source_rectangle(
		NULL, 20, 30, 200, 300) == IVI_FAILED);
}

static void
test_surface_bad_properties(struct data *d)
{
	ivi_test_assert(d, d->interface->get_properties_of_surface(NULL) == NULL);
}

/*****************************************************************************
 *  Invoked as thread by request_start_ivi_shell_test requested by test client
 ****************************************************************************/
static void *
test(void *param)
{
	struct wl_resource *resource = param;
	struct data *d = wl_resource_get_user_data(resource);

	/*
	  ivi_surface related tests.
	*/
	test_surface_create(d);
	test_surface_visibility(d);
	test_surface_opacity(d);
	test_surface_orientation(d);
	test_surface_dimension(d);
	test_surface_position(d);
	test_surface_destination(d);
	test_surface_source_rectangle(d);
	test_surface_bad_visibility(d);
	test_surface_bad_opacity(d);
	test_surface_bad_destination(d);
	test_surface_bad_orientation(d);
	test_surface_bad_dimension(d);
	test_surface_bad_position(d);
	test_surface_bad_source_rectangle(d);
	test_surface_bad_properties(d);

	ivi_shell_test_send_exit_ivi_shell_test(resource);
	exit(d->failed ? EXIT_FAILURE : EXIT_SUCCESS);
	return NULL;
}

/*****************************************************************************
 *  implementation of ivi_shell_test_interface which is
 *  requested from test client
 ****************************************************************************/
static void
request_start_ivi_shell_test(struct wl_client *client,
			     struct wl_resource *resource)
{
	pthread_create(&thread, NULL, test, resource);
}

static void
reply_create_ivi_layout_surface(struct wl_client *client,
				struct wl_resource *resource,
				uint32_t surface_id)
{
	pthread_cond_signal(&thread_cond);
}

static void
reply_remove_ivi_layout_surface(struct wl_client *client,
				struct wl_resource *resource,
				uint32_t surface_id)
{
	pthread_cond_signal(&thread_cond);
}

static const struct ivi_shell_test_interface shell_test_implementation = {
	request_start_ivi_shell_test,
	reply_create_ivi_layout_surface,
	reply_remove_ivi_layout_surface
};

static void
bind_ivi_shell_test(struct wl_client *client,
		    void *data, uint32_t version, uint32_t id)
{
	struct data *d = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &ivi_shell_test_interface,
				      1, id);

	wl_resource_set_implementation(resource,
				       &shell_test_implementation,
				       d, NULL);

	d->resource = resource;
}

/*****************************************************************************
 *  Exec test client to communicate test application
 *  via ivi_shell_test_interface
 ****************************************************************************/
static void
idle_launch_client(void *data)
{
	struct data *d = data;
	pid_t pid;
	sigset_t allsigs;
	char *path;

	path = getenv("WESTON_TEST_CLIENT_PATH"); //to get test client
	if (path == NULL) {
		return;
	}

	pid = fork();
	if (pid == -1) {
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		sigfillset(&allsigs);
		sigprocmask(SIG_UNBLOCK, &allsigs, NULL);
		execl(path, path, NULL);
		weston_log("compositor: executing '%s' failed: %m\n", path);
		exit(EXIT_FAILURE);
	}

	d->process.pid = pid;
	d->process.cleanup = test_client_sigchld;
	weston_watch_process(&d->process);
}

/*****************************************************************************
 *  Test runner
 ****************************************************************************/
static void
run(void *data)
{
	idle_launch_client(data);
}

/*****************************************************************************
 *  exported functions
 *  this method is called from ivi-shell first to start test session
 ****************************************************************************/
WL_EXPORT int
controller_module_init(struct weston_compositor *ec,
		       int *argc, char *argv[],
		       const struct ivi_controller_interface *interface,
		       size_t interface_version)
{
	struct wl_event_loop *loop;
	struct data *d;

	d = zalloc(sizeof(*d));
	if (d == NULL) {
		return -1;
	}

	d->compositor = ec;
	d->interface = interface;

	if (wl_global_create(ec->wl_display, &ivi_shell_test_interface, 1,
			     d, bind_ivi_shell_test) == NULL)
		return -1;

	loop = wl_display_get_event_loop(ec->wl_display);
	wl_event_loop_add_idle(loop, run, d);

	return 0;
}
