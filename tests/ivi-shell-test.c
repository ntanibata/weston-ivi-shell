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
static struct weston_surface *
get_weston_surface(struct data *d, uint32_t surface_id)
{
	struct ivi_layout_surface *ivi_surface = NULL;
	ivi_surface = d->interface->get_surface_from_id(surface_id);
	return d->interface->surface_get_weston_surface(ivi_surface);
}

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
 *  tests for ivi-layer
 ****************************************************************************/
static void
test_layer_create(struct data *d)
{
	static const uint32_t id_layer = 346;
	uint32_t id1;
	uint32_t id2;
	struct ivi_layout_layer *ivilayer;
	struct ivi_layout_layer *new_ivilayer;
	struct ivi_layout_layer *destroy_ivilayer;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, id_layer == d->interface->get_id_of_layer(ivilayer));

	new_ivilayer = d->interface->get_layer_from_id(id_layer);
	ivi_test_assert(d, ivilayer == new_ivilayer);

	id1 = d->interface->get_id_of_layer(ivilayer);
	id2 = d->interface->get_id_of_layer(new_ivilayer);
	ivi_test_assert(d, id1 == id2);

	d->interface->layer_remove(ivilayer);
	destroy_ivilayer = d->interface->get_layer_from_id(id_layer);
	ivi_test_assert(d, destroy_ivilayer == NULL);
}

static void
test_layer_visibility(struct data *d)
{
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	const struct ivi_layout_layer_properties *prop;
	bool visibility;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_visibility(
		ivilayer, true) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	visibility = d->interface->layer_get_visibility(ivilayer);
	ivi_test_assert(d, visibility == true);

	prop = d->interface->get_properties_of_layer(ivilayer);
	ivi_test_assert(d, prop->visibility == true);

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_opacity(struct data *d)
{
	static const uint32_t id_layer = 346;
	wl_fixed_t opacity;
	struct ivi_layout_layer *ivilayer;
	const struct ivi_layout_layer_properties *prop;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_opacity(
		ivilayer, wl_fixed_from_double(0.5)) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	opacity = d->interface->layer_get_opacity(ivilayer);
	ivi_test_assert(d, opacity == wl_fixed_from_double(0.5));

	prop = d->interface->get_properties_of_layer(ivilayer);
	ivi_test_assert(d, prop->opacity == wl_fixed_from_double(0.5));

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_orientation(struct data *d)
{
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	const struct ivi_layout_layer_properties *prop;
	enum wl_output_transform orientation;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_orientation(
		ivilayer, WL_OUTPUT_TRANSFORM_90) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	orientation = d->interface->layer_get_orientation(ivilayer);
	ivi_test_assert(d, orientation == WL_OUTPUT_TRANSFORM_90);

	prop = d->interface->get_properties_of_layer(ivilayer);
	ivi_test_assert(d, prop->orientation == WL_OUTPUT_TRANSFORM_90);

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_dimension(struct data *d)
{
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	const struct ivi_layout_layer_properties *prop;
	int32_t dest_width;
	int32_t dest_height;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_dimension(
		ivilayer, 200, 300) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->layer_get_dimension(
		ivilayer, &dest_width, &dest_height) == IVI_SUCCEEDED);
	ivi_test_assert(d, dest_width == 200);
	ivi_test_assert(d, dest_height == 300);

	prop = d->interface->get_properties_of_layer(ivilayer);
	ivi_test_assert(d, prop->dest_width == 200);
	ivi_test_assert(d, prop->dest_height == 300);

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_position(struct data *d)
{
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	const struct ivi_layout_layer_properties *prop;
	int32_t dest_x;
	int32_t dest_y;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_position(
		ivilayer, 20, 30) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->layer_get_position(
		ivilayer, &dest_x, &dest_y) == IVI_SUCCEEDED);
	ivi_test_assert(d, dest_x == 20);
	ivi_test_assert(d, dest_y == 30);

	prop = d->interface->get_properties_of_layer(ivilayer);
	ivi_test_assert(d, prop->dest_x == 20);
	ivi_test_assert(d, prop->dest_y == 30);

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_destination(struct data *d)
{
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	const struct ivi_layout_layer_properties *prop;
	int32_t dest_width;
	int32_t dest_height;
	int32_t dest_x;
	int32_t dest_y;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_destination_rectangle(ivilayer, 20, 30, 200, 300) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->layer_get_dimension(
		ivilayer, &dest_width, &dest_height) == IVI_SUCCEEDED);
	ivi_test_assert(d, dest_width == 200);
	ivi_test_assert(d, dest_height == 300);

	ivi_test_assert(d, d->interface->layer_get_position(
		ivilayer, &dest_x, &dest_y) == IVI_SUCCEEDED);
	ivi_test_assert(d, dest_x == 20);
	ivi_test_assert(d, dest_y == 30);

	prop = d->interface->get_properties_of_layer(ivilayer);
	ivi_test_assert(d, prop->dest_width == 200);
	ivi_test_assert(d, prop->dest_height == 300);
	ivi_test_assert(d, prop->dest_x == 20);
	ivi_test_assert(d, prop->dest_y == 30);

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_source_rectangle(struct data *d)
{
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	const struct ivi_layout_layer_properties *prop;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_source_rectangle(
		ivilayer, 20, 30, 200, 300) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	prop = d->interface->get_properties_of_layer(ivilayer);
	ivi_test_assert(d, prop->source_width == 200);
	ivi_test_assert(d, prop->source_height == 300);
	ivi_test_assert(d, prop->source_x == 20);
	ivi_test_assert(d, prop->source_y == 30);

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_render_order(struct data *d)
{
#define SURFACE_NUM (3)
	static const uint32_t surfaces[SURFACE_NUM] = {1013, 1014, 1015};
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	struct ivi_layout_surface *ivisurfs[SURFACE_NUM] = {};
	struct ivi_layout_surface **array;
	int32_t length = 0;
	uint32_t i;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);

	for (i = 0; i < SURFACE_NUM; i++) {
		create_surface_sync(d, surfaces[i]);
		ivisurfs[i] = d->interface->get_surface_from_id(surfaces[i]);
	}

	ivi_test_assert(d, d->interface->layer_set_render_order(
		ivilayer, ivisurfs, SURFACE_NUM) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->get_surfaces_on_layer(
		ivilayer, &length, &array) == IVI_SUCCEEDED);
	ivi_test_assert(d, length == SURFACE_NUM);
	for (i = 0; i < SURFACE_NUM; i++) {
		ivi_test_assert(d, array[i] == ivisurfs[i]);
	}

	ivi_test_assert(d, d->interface->layer_set_render_order(
		ivilayer, NULL, 0) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->get_surfaces_on_layer(
		ivilayer, &length, &array) == IVI_SUCCEEDED);
	ivi_test_assert(d, length == 0);

	cleanup_surfaces(d, surfaces, SURFACE_NUM);

	d->interface->layer_remove(ivilayer);
#undef SURFACE_NUM
}

static void
test_layer_bad_create(struct data *d)
{
	d->interface->layer_remove(NULL);
}

static void
test_layer_bad_visibility(struct data *d)
{
	bool visibility;

	ivi_test_assert(d, d->interface->layer_set_visibility(
		NULL, true) == IVI_FAILED);

	d->interface->commit_changes();

	visibility = d->interface->layer_get_visibility(NULL);
	ivi_test_assert(d, visibility == false);
}

static void
test_layer_bad_opacity(struct data *d)
{
	static const uint32_t id_layer = 346;
	wl_fixed_t opacity;
	struct ivi_layout_layer *ivilayer;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_opacity(
		ivilayer, wl_fixed_from_double(0.3)) == IVI_SUCCEEDED);

	ivi_test_assert(d, d->interface->layer_set_opacity(
		ivilayer, wl_fixed_from_double(-1)) == IVI_FAILED);

	d->interface->commit_changes();

	opacity = d->interface->layer_get_opacity(ivilayer);
	ivi_test_assert(d, opacity == wl_fixed_from_double(0.3));

	ivi_test_assert(d, d->interface->layer_set_opacity(
		ivilayer, wl_fixed_from_double(1.1)) == IVI_FAILED);

	d->interface->commit_changes();

	opacity = d->interface->layer_get_opacity(ivilayer);
	ivi_test_assert(d, opacity == wl_fixed_from_double(0.3));

	ivi_test_assert(d, d->interface->layer_set_opacity(
		NULL, wl_fixed_from_double(0.5)) == IVI_FAILED);

	d->interface->commit_changes();

	opacity = d->interface->layer_get_opacity(NULL);
	ivi_test_assert(d, opacity == wl_fixed_from_double(0.0));

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_bad_destination(struct data *d)
{
	ivi_test_assert(d, d->interface->layer_set_destination_rectangle(
		NULL, 20, 30, 200, 300) == IVI_FAILED);
}

static void
test_layer_bad_orientation(struct data *d)
{
	enum wl_output_transform orientation;

	ivi_test_assert(d, d->interface->layer_set_orientation(
		NULL, WL_OUTPUT_TRANSFORM_90) == IVI_FAILED);

	d->interface->commit_changes();

	orientation = d->interface->layer_get_orientation(NULL);
	ivi_test_assert(d, orientation == WL_OUTPUT_TRANSFORM_NORMAL);
}

static void
test_layer_bad_dimension(struct data *d)
{
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	int32_t dest_width;
	int32_t dest_height;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_dimension(
		NULL, 200, 300) == IVI_FAILED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->layer_get_dimension(
		NULL, &dest_width, &dest_height) == IVI_FAILED);
	ivi_test_assert(d, d->interface->layer_get_dimension(
		ivilayer, NULL, &dest_height) == IVI_FAILED);
	ivi_test_assert(d, d->interface->layer_get_dimension(
		ivilayer, &dest_width, NULL) == IVI_FAILED);

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_bad_position(struct data *d)
{
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	int32_t dest_x;
	int32_t dest_y;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);
	ivi_test_assert(d, ivilayer != NULL);

	ivi_test_assert(d, d->interface->layer_set_position(
		NULL, 20, 30) == IVI_FAILED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->layer_get_position(
		NULL, &dest_x, &dest_y) == IVI_FAILED);
	ivi_test_assert(d, d->interface->layer_get_position(
		ivilayer, NULL, &dest_y) == IVI_FAILED);
	ivi_test_assert(d, d->interface->layer_get_position(
		ivilayer, &dest_x, NULL) == IVI_FAILED);

	d->interface->layer_remove(ivilayer);
}

static void
test_layer_bad_source_rectangle(struct data *d)
{
	ivi_test_assert(d, d->interface->layer_set_source_rectangle(
		NULL, 20, 30, 200, 300) == IVI_FAILED);
}

static void
test_layer_bad_render_order(struct data *d)
{
#define SURFACE_NUM (3)
	static const uint32_t surfaces[SURFACE_NUM] = {1016, 1017, 1018};
	static const uint32_t id_layer = 346;
	struct ivi_layout_layer *ivilayer;
	struct ivi_layout_surface *ivisurfs[SURFACE_NUM] = {};
	struct ivi_layout_surface **array;
	int32_t length = 0;
	uint32_t i;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);

	for (i = 0; i < SURFACE_NUM; i++) {
		create_surface_sync(d, surfaces[i]);
		ivisurfs[i] = d->interface->get_surface_from_id(surfaces[i]);
	}

	ivi_test_assert(d, d->interface->layer_set_render_order(
		NULL, ivisurfs, SURFACE_NUM) == IVI_FAILED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->get_surfaces_on_layer(
		NULL, &length, &array) == IVI_FAILED);
	ivi_test_assert(d, d->interface->get_surfaces_on_layer(
		ivilayer, NULL, &array) == IVI_FAILED);
	ivi_test_assert(d, d->interface->get_surfaces_on_layer(
		ivilayer, &length, NULL) == IVI_FAILED);

	cleanup_surfaces(d, surfaces, SURFACE_NUM);

	d->interface->layer_remove(ivilayer);
#undef SURFACE_NUM
}

static void
test_layer_bad_properties(struct data *d)
{
	ivi_test_assert(d, d->interface->get_properties_of_layer(NULL) == NULL);
}

/*****************************************************************************
 *  tests for ivi-screen
 ****************************************************************************/
static void
test_screen_id(struct data *d)
{
	struct ivi_layout_screen *iviscrn;

	iviscrn = d->interface->get_screen_from_id(0);
	ivi_test_assert(d, iviscrn != NULL);
}

static void
test_screen_resolution(struct data *d)
{
	struct ivi_layout_screen **iviscreen;
	struct ivi_layout_screen *iviscrn;
	int32_t screen_length = 0;
	int32_t width;
	int32_t height;

	ivi_test_assert(d, d->interface->get_screens(
		&screen_length, &iviscreen) == IVI_SUCCEEDED);
	iviscrn = iviscreen[0];

	ivi_test_assert(d, d->interface->get_screen_resolution(
		iviscrn, &width, &height) == IVI_SUCCEEDED);
	ivi_test_assert(d, width == 1024);
	ivi_test_assert(d, height == 640);
}

static void
test_screen_render_order(struct data *d)
{
#define LAYER_NUM (3)
	static const uint32_t id_layer[LAYER_NUM] = {146, 246, 346};
	struct ivi_layout_screen *iviscrn = d->interface->get_screen_from_id(0);
	struct ivi_layout_layer *ivilayers[LAYER_NUM] = {};
	struct ivi_layout_layer **array;
	int32_t length = 0;
	uint32_t i;

	for (i = 0; i < LAYER_NUM; i++) {
		ivilayers[i] =
			d->interface->layer_create_with_dimension(id_layer[i], 200, 300);
	}

	ivi_test_assert(d, d->interface->screen_set_render_order(
		iviscrn, ivilayers, LAYER_NUM) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->get_layers_on_screen(
		iviscrn, &length, &array) == IVI_SUCCEEDED);
	ivi_test_assert(d, length == LAYER_NUM);
	for (i = 0; i < LAYER_NUM; i++) {
		ivi_test_assert(d, array[i] == ivilayers[i]);
	}

	ivi_test_assert(d, d->interface->screen_set_render_order(
		iviscrn, NULL, 0) == IVI_SUCCEEDED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->get_layers_on_screen(
		iviscrn, &length, &array) == IVI_SUCCEEDED);
	ivi_test_assert(d, length == 0);

	for (i = 0; i < LAYER_NUM; i++) {
		d->interface->layer_remove(ivilayers[i]);
	}
#undef LAYER_NUM
}

static void
test_screen_bad_resolution(struct data *d)
{
	struct ivi_layout_screen **iviscreen;
	struct ivi_layout_screen *iviscrn;
	int32_t screen_length = 0;
	int32_t width;
	int32_t height;

	ivi_test_assert(d, d->interface->get_screens(
		&screen_length, &iviscreen) == IVI_SUCCEEDED);
	iviscrn = iviscreen[0];

	ivi_test_assert(d, d->interface->get_screen_resolution(
		NULL, &width, &height) == IVI_FAILED);
	ivi_test_assert(d, d->interface->get_screen_resolution(
		iviscrn, NULL, &height) == IVI_FAILED);
	ivi_test_assert(d, d->interface->get_screen_resolution(
		iviscrn, &width, NULL) == IVI_FAILED);
}

static void
test_screen_badrender_order(struct data *d)
{
#define LAYER_NUM (3)
	static const uint32_t id_layer[LAYER_NUM] = {146, 246, 346};
	struct ivi_layout_screen *iviscrn = d->interface->get_screen_from_id(0);
	struct ivi_layout_layer *ivilayers[LAYER_NUM] = {};
	struct ivi_layout_layer **array;
	int32_t length = 0;
	uint32_t i;

	for (i = 0; i < LAYER_NUM; i++) {
		ivilayers[i] =
			d->interface->layer_create_with_dimension(id_layer[i], 200, 300);
	}

	ivi_test_assert(d, d->interface->screen_set_render_order(
		NULL, ivilayers, LAYER_NUM) == IVI_FAILED);

	d->interface->commit_changes();

	ivi_test_assert(d, d->interface->get_layers_on_screen(
		NULL, &length, &array) == IVI_FAILED);
	ivi_test_assert(d, d->interface->get_layers_on_screen(
		iviscrn, NULL, &array) == IVI_FAILED);
	ivi_test_assert(d, d->interface->get_layers_on_screen(
		iviscrn, &length, NULL) == IVI_FAILED);

	for (i = 0; i < LAYER_NUM; i++) {
		d->interface->layer_remove(ivilayers[i]);
	}
#undef LAYER_NUM
}

/*****************************************************************************
 *  tests for notifications
 ****************************************************************************/
static void
test_layer_add_notification_callback(struct ivi_layout_layer *ivilayer,
	 const struct ivi_layout_layer_properties *prop,
	 enum ivi_layout_notification_mask mask,
	 void *userdata)
{
	struct data *d = userdata;
	ivi_test_assert(d, d->interface->get_id_of_layer(ivilayer) == 347);
	ivi_test_assert(d, prop->source_width == 200);
	ivi_test_assert(d, prop->source_height == 300);
}

static void
test_layer_add_notification(struct data *d)
{
	static const uint32_t id_layer = 347;
	struct ivi_layout_layer *ivilayer;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);

	ivi_test_assert(d, d->interface->layer_add_notification(
		ivilayer, test_layer_add_notification_callback, d) == IVI_SUCCEEDED);

	d->interface->commit_changes();
	d->interface->layer_remove_notification(ivilayer);
	d->interface->commit_changes();

	d->interface->layer_remove(ivilayer);
}
static void
test_surface_add_notification_callback(struct ivi_layout_surface *ivisurf,
	 const struct ivi_layout_surface_properties *prop,
	 enum ivi_layout_notification_mask mask,
	 void *userdata)
{
	struct data *d = userdata;
	ivi_test_assert(d, d->interface->get_id_of_surface(ivisurf) == 1020);
}

static void
test_surface_add_notification(struct data *d)
{
	static const uint32_t id_surface = 1020;
	struct ivi_layout_surface *ivisurf;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_add_notification(
		ivisurf, test_surface_add_notification_callback, d) == IVI_SUCCEEDED);

	d->interface->commit_changes();
	d->interface->surface_remove_notification(ivisurf);
	d->interface->commit_changes();
}

static void
test_surface_configure_notification_callback(struct ivi_layout_surface *ivisurf,
	 void *userdata)
{
	struct data *d = userdata;
	ivi_test_assert(d, d->interface->get_id_of_surface(ivisurf) == 1021);
}

static void
test_surface_configure_notification(struct data *d)
{
#define SURFACE_NUM (2)
	static const uint32_t surfaces[SURFACE_NUM] = {1021, 1022};
	struct weston_surface *surface;

	ivi_test_assert(d, d->interface->add_notification_configure_surface(
		test_surface_configure_notification_callback, d) == IVI_SUCCEEDED);
	d->interface->commit_changes();
	create_surface_sync(d, surfaces[0]);

	surface = get_weston_surface(d, surfaces[0]);

	ivi_test_assert(d, surface->configure);
	surface->width = 1;
	surface->height = 1;
	surface->configure(surface, 1, 1);

	d->interface->remove_notification_configure_surface(
		test_surface_configure_notification_callback, d);
	d->interface->commit_changes();
	create_surface_sync(d, surfaces[1]);

	cleanup_surfaces(d, surfaces, SURFACE_NUM);
#undef SURFACE_NUM
}

static void
test_layer_create_notification_callback(struct ivi_layout_layer *ivilayer,
	 void *userdata)
{
	struct data *d = userdata;
	const struct ivi_layout_layer_properties *prop =
		d->interface->get_properties_of_layer(ivilayer);

	ivi_test_assert(d, d->interface->get_id_of_layer(ivilayer) == 348);
	ivi_test_assert(d, prop->source_width == 200);
	ivi_test_assert(d, prop->source_height == 300);
}

static void
test_layer_create_notification(struct data *d)
{
#define LAYER_NUM (2)
	static const uint32_t layers[LAYER_NUM] = {348, 349};
	struct ivi_layout_layer *ivilayers[LAYER_NUM] = {};

	ivi_test_assert(d, d->interface->add_notification_create_layer(
		test_layer_create_notification_callback, d) == IVI_SUCCEEDED);
	ivilayers[0] = d->interface->layer_create_with_dimension(layers[0], 200, 300);

	d->interface->remove_notification_create_layer(
		test_layer_create_notification_callback, d);

	ivilayers[1] = d->interface->layer_create_with_dimension(layers[1], 400, 500);

	d->interface->layer_remove(ivilayers[0]);
	d->interface->layer_remove(ivilayers[1]);
#undef LAYER_NUM
}

static void
test_surface_create_notification_callback(struct ivi_layout_surface *ivisurf,
	void *userdata)
{
	struct data *d = userdata;

	ivi_test_assert(d, d->interface->get_id_of_surface(ivisurf) == 1023);
}

static void
test_surface_create_notification(struct data *d)
{
#define SURFACE_NUM (2)
	static const uint32_t surfaces[SURFACE_NUM] = {1023, 1024};

	ivi_test_assert(d, d->interface->add_notification_create_surface(
			test_surface_create_notification_callback, d) == IVI_SUCCEEDED);

	create_surface_sync(d, surfaces[0]);

	d->interface->remove_notification_create_surface(
		test_surface_create_notification_callback, d);

	create_surface_sync(d, surfaces[1]);
#undef SURFACE_NUM
}

static void
test_layer_remove_notification_callback(struct ivi_layout_layer *ivilayer,
	void *userdata)
{
	struct data *d = userdata;
	const struct ivi_layout_layer_properties *prop =
		d->interface->get_properties_of_layer(ivilayer);

	ivi_test_assert(d, d->interface->get_id_of_layer(ivilayer) == 350);
	ivi_test_assert(d, prop->source_width == 200);
	ivi_test_assert(d, prop->source_height == 300);
}

static void
test_layer_remove_notification(struct data *d)
{
#define LAYER_NUM (2)
	static const uint32_t layers[LAYER_NUM] = {350, 351};
	struct ivi_layout_layer *ivilayers[LAYER_NUM] = {};

	ivilayers[0] = d->interface->layer_create_with_dimension(layers[0], 200, 300);
	ivi_test_assert(d, d->interface->add_notification_remove_layer(
			test_layer_remove_notification_callback, d) == IVI_SUCCEEDED);
	d->interface->layer_remove(ivilayers[0]);


	ivilayers[1] = d->interface->layer_create_with_dimension(layers[1], 200, 300);
	d->interface->remove_notification_remove_layer(
		test_layer_remove_notification_callback, d);
	d->interface->layer_remove(ivilayers[1]);
#undef LAYER_NUM
}

static void
test_surface_remove_notification_callback(struct ivi_layout_surface *ivisurf,
	void *userdata)
{
	struct data *d = userdata;

	ivi_test_assert(d, d->interface->get_id_of_surface(ivisurf) == 1025);
}

static void
test_surface_remove_notification(struct data *d)
{
#define SURFACE_NUM (2)
	static const uint32_t surfaces[SURFACE_NUM] = {1025, 1026};

	create_surface_sync(d, surfaces[0]);
	d->interface->get_surface_from_id(surfaces[0]);
	ivi_test_assert(d, d->interface->add_notification_remove_surface(
			test_surface_remove_notification_callback, d) == IVI_SUCCEEDED);
	destroy_surface_sync(d, surfaces[0]);

	create_surface_sync(d, surfaces[1]);
	d->interface->get_surface_from_id(surfaces[1]);
	d->interface->remove_notification_remove_surface(
		test_surface_remove_notification_callback, d);
	destroy_surface_sync(d, surfaces[1]);
#undef SURFACE_NUM
}

static void
test_layer_bad_add_notification_callback(struct ivi_layout_layer *ivilayer,
	 const struct ivi_layout_layer_properties *prop,
	 enum ivi_layout_notification_mask mask,
	 void *userdata)
{
}

static void
test_layer_bad_add_notification(struct data *d)
{
	static const uint32_t id_layer = 352;
	struct ivi_layout_layer *ivilayer;

	ivilayer = d->interface->layer_create_with_dimension(id_layer, 200, 300);

	ivi_test_assert(d, d->interface->layer_add_notification(
		NULL, test_layer_bad_add_notification_callback, NULL) == IVI_FAILED);
	ivi_test_assert(d, d->interface->layer_add_notification(
		ivilayer, NULL, NULL) == IVI_FAILED);

	d->interface->layer_remove(ivilayer);
}

static void
test_surface_bad_add_notification_callback(struct ivi_layout_surface *ivisurf,
	 const struct ivi_layout_surface_properties *prop,
	 enum ivi_layout_notification_mask mask,
	 void *userdata)
{
}

static void
test_surface_bad_add_notification(struct data *d)
{
	static const uint32_t id_surface = 1019;
	struct ivi_layout_surface *ivisurf;

	create_surface_sync(d, id_surface);

	ivisurf = d->interface->get_surface_from_id(id_surface);
	ivi_test_assert(d, ivisurf != NULL);

	ivi_test_assert(d, d->interface->surface_add_notification(
		NULL, test_surface_bad_add_notification_callback, NULL) == IVI_FAILED);
	ivi_test_assert(d, d->interface->surface_add_notification(
		ivisurf, NULL, NULL) == IVI_FAILED);

	cleanup_surfaces(d, &id_surface, 1);
}

static void
test_surface_bad_configure_notification(struct data *d)
{
	ivi_test_assert(d, d->interface->add_notification_configure_surface(
		NULL, NULL) == IVI_FAILED);
}

static void
test_layer_bad_create_notification(struct data *d)
{
	ivi_test_assert(d, d->interface->add_notification_create_layer(
		NULL, NULL) == IVI_FAILED);
}

static void
test_surface_bad_create_notification(struct data *d)
{
	ivi_test_assert(d, d->interface->add_notification_create_surface(
		NULL, NULL) == IVI_FAILED);
}

static void
test_layer_bad_remove_notification(struct data *d)
{
	ivi_test_assert(d, d->interface->add_notification_remove_layer(
		NULL, NULL) == IVI_FAILED);
}

static void
test_surface_bad_remove_notification(struct data *d)
{
	ivi_test_assert(d, d->interface->add_notification_remove_surface(
		NULL, NULL) == IVI_FAILED);
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

	/*
	  ivi_layer related tests.
	*/
	test_layer_create(d);
	test_layer_visibility(d);
	test_layer_opacity(d);
	test_layer_orientation(d);
	test_layer_dimension(d);
	test_layer_position(d);
	test_layer_destination(d);
	test_layer_source_rectangle(d);
	test_layer_render_order(d);
	test_layer_bad_create(d);
	test_layer_bad_visibility(d);
	test_layer_bad_opacity(d);
	test_layer_bad_destination(d);
	test_layer_bad_orientation(d);
	test_layer_bad_dimension(d);
	test_layer_bad_position(d);
	test_layer_bad_source_rectangle(d);
	test_layer_bad_properties(d);
	test_layer_bad_render_order(d);

	/*
	  ivi_screen related tests.
	*/
	test_screen_id(d);
	test_screen_resolution(d);
	test_screen_render_order(d);
	test_screen_bad_resolution(d);
	test_screen_badrender_order(d);

	/*
	  notification related tests.
	*/
	test_layer_add_notification(d);
	test_surface_add_notification(d);
	test_surface_configure_notification(d);
	test_layer_create_notification(d);
	test_surface_create_notification(d);
	test_layer_remove_notification(d);
	test_surface_remove_notification(d);
	test_layer_bad_add_notification(d);
	test_surface_bad_add_notification(d);
	test_surface_bad_configure_notification(d);
	test_layer_bad_create_notification(d);
	test_surface_bad_create_notification(d);
	test_layer_bad_remove_notification(d);
	test_surface_bad_remove_notification(d);

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
