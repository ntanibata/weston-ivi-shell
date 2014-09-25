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

/*
 * ivi-shell supports a type of shell for In-Vehicle Infotainment system.
 * In-Vehicle Infotainment system traditionally manages surfaces with global
 * identification. A protocol, ivi_application, supports such a feature
 * by implementing a request, ivi_application::surface_creation defined in
 * ivi_application.xml.
 *
 *  The ivi-shell explicitly loads a module to add business logic like how to
 *  layout surfaces by using internal ivi-layout APIs.
 */
#include "config.h"

#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>
#include <dlfcn.h>
#include <limits.h>
#include <assert.h>

#include "ivi-shell.h"
#include "ivi-application-server-protocol.h"
#include "ivi-layout-export.h"
#include "ivi-layout-private.h"

#include "../shared/os-compatibility.h"

/* Representation of ivi_surface protocol object. */
struct ivi_shell_surface
{
	struct wl_resource* resource;
	struct ivi_shell *shell;
	struct ivi_layout_surface *layout_surface;

	struct weston_surface *surface;
	struct wl_listener surface_destroy_listener;

	uint32_t id_surface;

	int32_t width;
	int32_t height;

	struct wl_list link;

	struct wl_listener configured_listener;

	const struct weston_shell_client *client;
};

struct ivi_shell_setting
{
	char *ivi_module;
};

/*
 * Implementation of ivi_surface
 */

static void
surface_configure_notify(struct wl_listener *listener, void *data)
{
	struct ivi_layout_surface *layout_surf =
		(struct ivi_layout_surface *)data;

	struct ivi_shell_surface *shell_surf =
		container_of(listener,
			     struct ivi_shell_surface,
			     configured_listener);

	int32_t dest_width = 0;
	int32_t dest_height = 0;

	ivi_layout_surface_get_dimension(layout_surf,
					 &dest_width, &dest_height);

	if (shell_surf->resource) 
		ivi_surface_send_configure(shell_surf->resource,
					   dest_width, dest_height);

	if (shell_surf->client) 
		shell_surf->client->send_configure(shell_surf->surface,
						dest_width, dest_height);
}

static void
ivi_shell_surface_configure(struct weston_surface *, int32_t, int32_t);

static struct ivi_shell_surface *
get_ivi_shell_surface(struct weston_surface *surface)
{
	if (surface->configure == ivi_shell_surface_configure)
		return surface->configure_private;

	return NULL;
}

static void
ivi_shell_surface_configure(struct weston_surface *surface,
			    int32_t sx, int32_t sy)
{
	struct ivi_shell_surface *ivisurf = get_ivi_shell_surface(surface);
	struct weston_view *view;
	float from_x;
	float from_y;
	float to_x;
	float to_y;

	if (surface->width == 0 || surface->height == 0 || ivisurf == NULL)
		return;

	view = ivi_layout_get_weston_view(ivisurf->layout_surface);

	if (view == NULL)
		return;

	if (ivisurf->width != surface->width ||
	    ivisurf->height != surface->height) {
		ivisurf->width  = surface->width;
		ivisurf->height = surface->height;

		weston_view_to_global_float(view, 0, 0, &from_x, &from_y);
		weston_view_to_global_float(view, sx, sy, &to_x, &to_y);

		weston_view_set_position(view,
					 view->geometry.x + to_x - from_x,
					 view->geometry.y + to_y - from_y);
		weston_view_update_transform(view);

		ivi_layout_surface_configure(ivisurf->layout_surface,
					     surface->width, surface->height);	
	}
}

/*
 * The ivi_surface wl_resource destructor.
 *
 * Gets called via ivi_surface.destroy request or automatic wl_client clean-up.
 */
static void
shell_destroy_shell_surface(struct wl_resource *resource)
{
	struct ivi_shell_surface *ivisurf = wl_resource_get_user_data(resource);
	if (ivisurf != NULL) {
		ivisurf->resource = NULL;
	}
}

/* Gets called through the weston_surface destroy signal. */
static void
shell_handle_surface_destroy(struct wl_listener *listener, void *data)
{
	struct ivi_shell_surface *ivisurf =
			container_of(listener, struct ivi_shell_surface,
				     surface_destroy_listener);

	assert(ivisurf != NULL);

	if (ivisurf->surface!=NULL) {
		ivisurf->surface->configure = NULL;
		ivisurf->surface->configure_private = NULL;
		ivisurf->surface = NULL;
	}

	wl_list_remove(&ivisurf->surface_destroy_listener.link);
	wl_list_remove(&ivisurf->link);

	if (ivisurf->resource != NULL) {
		wl_resource_set_user_data(ivisurf->resource, NULL);
		ivisurf->resource = NULL;
	}
	free(ivisurf);

}

/* Gets called, when a client sends ivi_surface.destroy request. */
static void
surface_destroy(struct wl_client *client, struct wl_resource *resource)
{
	/*
	 * Fires the wl_resource destroy signal, and then calls
	 * ivi_surface wl_resource destructor: shell_destroy_shell_surface()
	 */
	wl_resource_destroy(resource);
}

static const struct ivi_surface_interface surface_implementation = {
	surface_destroy,
};

static struct shell_surface*
create_shell_surface(void *shell,
		     struct weston_surface *weston_surface,
		     const struct weston_shell_client *client)
{
	struct ivi_shell_surface *ivisurf;
	struct ivi_layout_surface *layout_surface;
	static uint32_t id_surface = 0xffffffff; // FIXME

	ivisurf = zalloc(sizeof *ivisurf);
	if (ivisurf == NULL) {
	weston_log("no memory\n");
		return NULL;
	}

	layout_surface = ivi_layout_surface_create(weston_surface, id_surface);

	wl_list_init(&ivisurf->link);
	wl_list_insert(&((struct ivi_shell*)shell)->ivi_surface_list, &ivisurf->link);

	ivisurf->shell = shell;
	ivisurf->id_surface = id_surface;

	ivisurf->resource = NULL;
	ivisurf->width = 0;
	ivisurf->height = 0;
	ivisurf->layout_surface = layout_surface;
	ivisurf->configured_listener.notify = surface_configure_notify;
	ivi_layout_surface_add_configured_listener(layout_surface, 
						   &ivisurf->configured_listener);
	ivisurf->client = client;

	ivisurf->surface = weston_surface;

	weston_surface->configure = ivi_shell_surface_configure;
	weston_surface->configure_private = ivisurf;

	/* FIXME !!! res is null !!!
	wl_resource_set_implementation(res, &surface_implementation,
				       ivisurf, NULL);
	*/

	id_surface--;

	return NULL;
}

static struct weston_view*
get_primary_view(void *shell,
		 struct shell_surface *shsurf)
{
	return NULL;
}

static void
set_toplevel(struct shell_surface *shsurf)
{
}

static void
set_transient(struct shell_surface *shsurf,
	      struct weston_surface *parent,
	      int x, int y, uint32_t flags)
{
}

static void
set_fullscreen(struct shell_surface *shsurf,
	       uint32_t method,
	       uint32_t framerate,
	       struct weston_output *output)
{
}

static void
set_xwayland(struct shell_surface *shsurf,
	     int x, int y, uint32_t flags)
{
}

static int
move(struct shell_surface *shsurf, struct weston_seat *ws)
{
    return 0; // success
}

static int
resize(struct shell_surface *shsurf, struct weston_seat *ws, uint32_t edges)
{
    return 0; // success
}

static void
set_title(struct shell_surface *shsurf, const char *title)
{
}

static void
set_window_geometry(struct shell_surface *shsurf,
		    int32_t x, int32_t y, int32_t width, int32_t height)
{
	/*non support*/
}

/**
 * Request handler for ivi_application.surface_create.
 *
 * Creates an ivi_surface protocol object associated with the given wl_surface.
 * ivi_surface protocol object is represented by struct ivi_shell_surface.
 *
 * \param client The client.
 * \param resource The ivi_application protocol object.
 * \param id_surface The IVI surface ID.
 * \param surface_resource The wl_surface protocol object.
 * \param id The protocol object id for the new ivi_surface protocol object.
 *
 * The wl_surface is given the ivi_surface role and associated with a unique
 * IVI ID which is used to identify the surface in a controller
 * (window manager).
 */
static void
application_surface_create(struct wl_client *client,
			   struct wl_resource *resource,
			   uint32_t id_surface,
			   struct wl_resource *surface_resource,
			   uint32_t id)
{
	struct ivi_shell *shell = wl_resource_get_user_data(resource);
	struct ivi_shell_surface *ivisurf;
	struct ivi_layout_surface *layout_surface;
	struct weston_surface *weston_surface =
		wl_resource_get_user_data(surface_resource);
	struct wl_resource *res;

	if (weston_surface_set_role(weston_surface, "ivi_surface",
				    resource, IVI_APPLICATION_ERROR_ROLE) < 0)
		return;

	layout_surface = ivi_layout_surface_create(weston_surface, id_surface);

	/* check if id_ivi is already used for wl_surface*/
	if (layout_surface == NULL){
		wl_resource_post_error(resource,
				       IVI_APPLICATION_ERROR_IVI_ID,
				       "surface_id is already assigned "
				       "by another app");
		return;
	}

	ivisurf = zalloc(sizeof *ivisurf);
	if (ivisurf == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_list_init(&ivisurf->link);
	wl_list_insert(&shell->ivi_surface_list, &ivisurf->link);

	ivisurf->shell = shell;
	ivisurf->id_surface = id_surface;

	ivisurf->width = 0;
	ivisurf->height = 0;
	ivisurf->layout_surface = layout_surface;
	ivisurf->configured_listener.notify = surface_configure_notify;
	ivi_layout_surface_add_configured_listener(layout_surface,
				     &ivisurf->configured_listener);
	ivisurf->client= NULL;

	/*
	 * The following code relies on wl_surface destruction triggering
	 * immediateweston_surface destruction
	 */
	ivisurf->surface_destroy_listener.notify = shell_handle_surface_destroy;
	wl_signal_add(&weston_surface->destroy_signal,
		      &ivisurf->surface_destroy_listener);

	ivisurf->surface = weston_surface;

	weston_surface->configure = ivi_shell_surface_configure;
	weston_surface->configure_private = ivisurf;

	res = wl_resource_create(client, &ivi_surface_interface, 1, id);
	if (res == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	ivisurf->resource = res;

	wl_resource_set_implementation(res, &surface_implementation,
				       ivisurf, shell_destroy_shell_surface);
}

static const struct ivi_application_interface application_implementation = {
	application_surface_create
};

/*
 * Handle wl_registry.bind of ivi_application global singleton.
 */
static void
bind_ivi_application(struct wl_client *client,
		     void *data, uint32_t version, uint32_t id)
{
	struct ivi_shell *shell = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &ivi_application_interface,
				      1, id);

	wl_resource_set_implementation(resource,
				       &application_implementation,
				       shell, NULL);
}

struct weston_view *
get_default_view(struct weston_surface *surface)
{
	struct ivi_shell_surface *shsurf;
	struct weston_view *view;

	if (!surface || wl_list_empty(&surface->views))
		return NULL;

	shsurf = get_ivi_shell_surface(surface);
	if (shsurf && shsurf->layout_surface) {
		view = ivi_layout_get_weston_view(shsurf->layout_surface);
		if (view)
			return view;
	}

	wl_list_for_each(view, &surface->views, surface_link) {
		if (weston_view_is_mapped(view))
			return view;
	}

	return container_of(surface->views.next,
			    struct weston_view, surface_link);
}

/*
 * Called through the compositor's destroy signal.
 */
static void
shell_destroy(struct wl_listener *listener, void *data)
{
	struct ivi_shell *shell =
		container_of(listener, struct ivi_shell, destroy_listener);
	struct ivi_shell_surface *ivisurf, *next;

	input_panel_destroy(shell);

	wl_list_for_each_safe(ivisurf, next, &shell->ivi_surface_list, link) {
		wl_list_remove(&ivisurf->link);
		free(ivisurf);
	}

	free(shell);
}

static void
init_ivi_shell(struct weston_compositor *compositor, struct ivi_shell *shell)
{
	shell->compositor = compositor;

	wl_list_init(&shell->ivi_surface_list);

	weston_layer_init(&shell->input_panel_layer, NULL);

	compositor->shell_interface.shell = shell;
	compositor->shell_interface.create_shell_surface = create_shell_surface;
	compositor->shell_interface.get_primary_view = get_primary_view;
	compositor->shell_interface.set_toplevel = set_toplevel;
	compositor->shell_interface.set_transient = set_transient;
	compositor->shell_interface.set_fullscreen = set_fullscreen;
	compositor->shell_interface.set_xwayland = set_xwayland;
	compositor->shell_interface.move = move;
	compositor->shell_interface.resize = resize;
	compositor->shell_interface.set_title = set_title;
	compositor->shell_interface.set_window_geometry = set_window_geometry;
}

static int
ivi_shell_setting_create(struct ivi_shell_setting *dest,
			 struct weston_compositor *compositor)
{
	int result = 0;
	struct weston_config *config = compositor->config;
	struct weston_config_section *section;

	if (NULL == dest)
		return -1;

	section = weston_config_get_section(config, "ivi-shell", NULL, NULL);

	if (weston_config_section_get_string(
		section, "ivi-module", &dest->ivi_module, NULL) != 0)
	{
		result = -1;
	}

	return result;
}

/*
 * Initialization of ivi-shell.
 */
struct ivi_controller_interface ivi_controller_interface;
static int
ivi_load_modules(struct weston_compositor *compositor, const char *modules,
		 int *argc, char *argv[])
{
	const char *p, *end;
	char buffer[256];
	int (*controller_module_init)(struct weston_compositor *compositor,
				      int *argc, char *argv[],
				      struct ivi_controller_interface *interface,
				      const size_t interface_version);

	if (modules == NULL)
		return 0;

	p = modules;
	while (*p) {
		end = strchrnul(p, ',');
		snprintf(buffer, sizeof buffer, "%.*s", (int)(end - p), p);

		controller_module_init = weston_load_module(buffer, "controller_module_init");
		if (controller_module_init)
			controller_module_init(compositor, argc, argv,
					       &ivi_controller_interface,
					       sizeof(struct ivi_controller_interface));

		p = end;
		while (*p == ',')
			p++;
	}

	return 0;
}

WL_EXPORT int
module_init(struct weston_compositor *compositor,
	    int *argc, char *argv[])
{
	struct ivi_shell *shell;
	struct ivi_shell_setting setting = { };

	shell = zalloc(sizeof *shell);
	if (shell == NULL)
		return -1;

	init_ivi_shell(compositor, shell);

	shell->destroy_listener.notify = shell_destroy;
	wl_signal_add(&compositor->destroy_signal, &shell->destroy_listener);

	if (input_panel_setup(shell) < 0)
		return -1;

	if (wl_global_create(compositor->wl_display,
			     &ivi_application_interface, 1,
			     shell, bind_ivi_application) == NULL)
		return -1;

	if (ivi_shell_setting_create(&setting, compositor) != 0)
		return -1;

	ivi_layout_init_with_compositor(compositor);
	

	/* Call module_init of ivi-modules which are defined in weston.ini */
	if (ivi_load_modules(compositor, setting.ivi_module, argc, argv) < 0) {
		free(setting.ivi_module);
		return -1;
	}

	free(setting.ivi_module);
	return 0;
}
