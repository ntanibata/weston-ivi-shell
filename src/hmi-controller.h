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

#ifndef _HMI_CONTROLLER_H_
#define _HMI_CONTROLLER_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>
#include "compositor.h"

#define ID_SURF_BACKGROUND 1000
#define ID_SURF_PANEL      1100

enum mode_id_surface {
    MODE_DIVIDED_INTO_EIGHT = 2000,
    MODE_DIVIDED_INTO_TWO   = 2100,
    MODE_MAXIMIZE_SOMEONE   = 2200,
    MODE_RANDOM_REPLACE     = 2300
};

void pointer_button_event(uint32_t id_surface);

int hmi_client_start(void);

void hmi_create_background(uint32_t id_surface);
void hmi_create_panel(uint32_t id_surface);
void hmi_create_button(uint32_t id_surface, uint32_t number);

#ifdef __cplusplus
} /**/
#endif /* __cplusplus */

#endif /* _HMI_CONTROLLER_H_ */
