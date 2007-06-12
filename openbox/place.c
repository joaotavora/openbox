/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   place.c for the Openbox window manager
   Copyright (c) 2006        Mikael Magnusson
   Copyright (c) 2003-2007   Dana Jansens

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "client.h"
#include "group.h"
#include "screen.h"
#include "frame.h"
#include "focus.h"
#include "config.h"
#include "debug.h"

static void add_choice(guint *choice, guint mychoice)
{
    guint i;
    for (i = 0; i < screen_num_monitors; ++i) {
        if (choice[i] == mychoice)
            return;
        else if (choice[i] == screen_num_monitors) {
            choice[i] = mychoice;
            return;
        }
    }
}

static Rect *pick_pointer_head(ObClient *c)
{
    guint i;
    gint px, py;

    screen_pointer_pos(&px, &py);
     
    for (i = 0; i < screen_num_monitors; ++i) {  
        if (RECT_CONTAINS(*screen_physical_area_monitor(i), px, py)) {
            return screen_area_monitor(c->desktop, i);
        }
    }
    g_assert_not_reached();
}

/*! Pick a monitor to place a window on.
  The returned array value should be freed with g_free. The areas within the
  array should not be freed. */
static Rect **pick_head(ObClient *c)
{
    Rect **area;
    guint *choice;
    guint i;
    gint px, py;
    ObClient *p;

    area = g_new(Rect*, screen_num_monitors);
    choice = g_new(guint, screen_num_monitors);
    for (i = 0; i < screen_num_monitors; ++i)
        choice[i] = screen_num_monitors; /* make them all invalid to start */

    /* try direct parent first */
    if ((p = client_direct_parent(c))) {
        add_choice(choice, client_monitor(p));
        ob_debug("placement adding choice %d for parent\n",
                 client_monitor(p));
    }

    /* more than one window in its group (more than just this window) */
    if (client_has_group_siblings(c)) {
        GSList *it;

        /* try on the client's desktop */
        for (it = c->group->members; it; it = g_slist_next(it)) {
            ObClient *itc = it->data;            
            if (itc != c &&
                (itc->desktop == c->desktop ||
                 itc->desktop == DESKTOP_ALL || c->desktop == DESKTOP_ALL))
            {
                add_choice(choice, client_monitor(it->data));
                ob_debug("placement adding choice %d for group sibling\n",
                         client_monitor(it->data));
            }
        }

        /* try on all desktops */
        for (it = c->group->members; it; it = g_slist_next(it)) {
            ObClient *itc = it->data;            
            if (itc != c) {
                add_choice(choice, client_monitor(it->data));
                ob_debug("placement adding choice %d for group sibling on "
                         "another desktop\n", client_monitor(it->data));
            }
        }
    }

    if (focus_client) {
        add_choice(choice, client_monitor(focus_client));
        ob_debug("placement adding choice %d for focused window\n",
                 client_monitor(focus_client));
    }

    screen_pointer_pos(&px, &py);

    for (i = 0; i < screen_num_monitors; i++)
        if (RECT_CONTAINS(*screen_physical_area_monitor(i), px, py)) {
            add_choice(choice, i);
            ob_debug("placement adding choice %d for mouse pointer\n", i);
            break;
        }

    /* add any leftover choices */
    for (i = 0; i < screen_num_monitors; ++i)
        add_choice(choice, i);

    for (i = 0; i < screen_num_monitors; ++i)
        area[i] = screen_area_monitor(c->desktop, choice[i]);

    return area;
}

static gboolean place_random(ObClient *client, gint *x, gint *y)
{
    gint l, r, t, b;
    Rect **areas;
    guint i;

    areas = pick_head(client);
    i = g_random_int_range(0, screen_num_monitors);

    l = areas[i]->x;
    t = areas[i]->y;
    r = areas[i]->x + areas[i]->width - client->frame->area.width;
    b = areas[i]->y + areas[i]->height - client->frame->area.height;

    if (r > l) *x = g_random_int_range(l, r + 1);
    else       *x = areas[i]->x;
    if (b > t) *y = g_random_int_range(t, b + 1);
    else       *y = areas[i]->y;

    g_free(areas);

    return TRUE;
}

static GSList* area_add(GSList *list, Rect *a)
{
    Rect *r = g_new(Rect, 1);
    *r = *a;
    return g_slist_prepend(list, r);
}

static GSList* area_remove(GSList *list, Rect *a)
{
    GSList *sit;
    GSList *result = NULL;

    for (sit = list; sit; sit = g_slist_next(sit)) {
        Rect *r = sit->data;

        if (!RECT_INTERSECTS_RECT(*r, *a)) {
            result = g_slist_prepend(result, r);
            r = NULL; /* dont free it */
        } else {
            Rect isect, extra;

            /* Use an intersection of a and r to determine the space
               around r that we can use.

               NOTE: the spaces calculated can overlap.
            */

            RECT_SET_INTERSECTION(isect, *r, *a);

            if (RECT_LEFT(isect) > RECT_LEFT(*r)) {
                RECT_SET(extra, r->x, r->y,
                         RECT_LEFT(isect) - r->x, r->height);
                result = area_add(result, &extra);
            }

            if (RECT_TOP(isect) > RECT_TOP(*r)) {
                RECT_SET(extra, r->x, r->y,
                         r->width, RECT_TOP(isect) - r->y + 1);
                result = area_add(result, &extra);
            }

            if (RECT_RIGHT(isect) < RECT_RIGHT(*r)) {
                RECT_SET(extra, RECT_RIGHT(isect) + 1, r->y,
                         RECT_RIGHT(*r) - RECT_RIGHT(isect), r->height);
                result = area_add(result, &extra);
            }

            if (RECT_BOTTOM(isect) < RECT_BOTTOM(*r)) {
                RECT_SET(extra, r->x, RECT_BOTTOM(isect) + 1,
                         r->width, RECT_BOTTOM(*r) - RECT_BOTTOM(isect));
                result = area_add(result, &extra);
            }
        }

        g_free(r);
    }
    g_slist_free(list);
    return result;
}

enum {
    IGNORE_FULLSCREEN = 1 << 0,
    IGNORE_MAXIMIZED  = 1 << 1,
    IGNORE_MENUTOOL   = 1 << 2,
    IGNORE_SHADED     = 1 << 3,
    IGNORE_NONGROUP   = 1 << 4,
    IGNORE_BELOW      = 1 << 5,
    IGNORE_NONFOCUS   = 1 << 6,
    IGNORE_END        = 1 << 7
};

static gboolean place_nooverlap(ObClient *c, gint *x, gint *y)
{
    Rect **areas;
    gint ignore;
    gboolean ret;
    gint maxsize;
    GSList *spaces = NULL, *sit, *maxit;

    areas = pick_head(c);
    ret = FALSE;
    maxsize = 0;
    maxit = NULL;

    /* try ignoring different things to find empty space */
    for (ignore = 0; ignore < IGNORE_END && !ret; ignore = (ignore << 1) + 1) {
        guint i;

        /* try all monitors in order of preference */
        for (i = 0; i < screen_num_monitors && !ret; ++i) {
            GList *it;

            /* add the whole monitor */
            spaces = area_add(spaces, areas[i]);

            /* go thru all the windows */
            for (it = client_list; it; it = g_list_next(it)) {
                ObClient *test = it->data;

                /* should we ignore this client? */
                if (screen_showing_desktop) continue;
                if (c == test) continue;
                if (test->iconic) continue;
                if (c->desktop != DESKTOP_ALL) {
                    if (test->desktop != c->desktop &&
                        test->desktop != DESKTOP_ALL) continue;
                } else {
                    if (test->desktop != screen_desktop &&
                        test->desktop != DESKTOP_ALL) continue;
                }
                if (test->type == OB_CLIENT_TYPE_SPLASH ||
                    test->type == OB_CLIENT_TYPE_DESKTOP) continue;


                if ((ignore & IGNORE_FULLSCREEN) &&
                    test->fullscreen) continue;
                if ((ignore & IGNORE_MAXIMIZED) &&
                    test->max_horz && test->max_vert) continue;
                if ((ignore & IGNORE_MENUTOOL) &&
                    (test->type == OB_CLIENT_TYPE_MENU ||
                     test->type == OB_CLIENT_TYPE_TOOLBAR) &&
                    client_has_parent(c)) continue;
                if ((ignore & IGNORE_SHADED) &&
                    test->shaded) continue;
                if ((ignore & IGNORE_NONGROUP) &&
                    client_has_group_siblings(test) &&
                    test->group != c->group) continue;
                if ((ignore & IGNORE_BELOW) &&
                    test->layer < c->layer) continue;
                if ((ignore & IGNORE_NONFOCUS) &&
                    focus_client != test) continue;

                /* don't ignore this window, so remove it from the available
                   area */
                spaces = area_remove(spaces, &test->frame->area);
            }

            for (sit = spaces; sit; sit = g_slist_next(sit)) {
                Rect *r = sit->data;

                if (r->width >= c->frame->area.width &&
                    r->height >= c->frame->area.height &&
                    r->width > maxsize)
                {
                    maxsize = r->width;
                    maxit = sit;
                }
            }

            if (maxit) {
                Rect *r = maxit->data;

                /* center it in the area */
                *x = r->x + (r->width - c->frame->area.width) / 2;
                *y = r->y + (r->height - c->frame->area.height) / 2;
                ret = TRUE;
            }

            while (spaces) {
                g_free(spaces->data);
                spaces = g_slist_delete_link(spaces, spaces);
            }
        }
    }

    g_free(areas);
    return ret;
}

static gboolean place_under_mouse(ObClient *client, gint *x, gint *y)
{
    gint l, r, t, b;
    gint px, py;
    Rect *area;

    if (!screen_pointer_pos(&px, &py))
        return FALSE;
    area = pick_pointer_head(client);

    l = area->x;
    t = area->y;
    r = area->x + area->width - client->frame->area.width;
    b = area->y + area->height - client->frame->area.height;

    *x = px - client->area.width / 2 - client->frame->size.left;
    *x = MIN(MAX(*x, l), r);
    *y = py - client->area.height / 2 - client->frame->size.top;
    *y = MIN(MAX(*y, t), b);

    return TRUE;
}

static gboolean place_per_app_setting(ObClient *client, gint *x, gint *y,
                                      ObAppSettings *settings)
{
    Rect *screen = NULL;

    if (!settings || (settings && !settings->pos_given))
        return FALSE;

    /* Find which head the pointer is on */
    if (settings->monitor == 0)
        screen = pick_pointer_head(client);
    else if (settings->monitor > 0 &&
             (guint)settings->monitor <= screen_num_monitors)
        screen = screen_area_monitor(client->desktop,
                                     (guint)settings->monitor - 1);
    else {
        Rect **all = NULL;
        all = pick_head(client);
        screen = all[0];
        g_free(all); /* the areas themselves don't need to be freed */
    }

    if (settings->center_x)
        *x = screen->x + screen->width / 2 - client->area.width / 2;
    else if (settings->opposite_x)
        *x = screen->x + screen->width - client->frame->area.width -
            settings->position.x;
    else
        *x = screen->x + settings->position.x;

    if (settings->center_y)
        *y = screen->y + screen->height / 2 - client->area.height / 2;
    else if (settings->opposite_y)
        *y = screen->y + screen->height - client->frame->area.height -
            settings->position.y;
    else
        *y = screen->y + settings->position.y;

    return TRUE;
}

static gboolean place_transient_splash(ObClient *client, gint *x, gint *y)
{
    if (client->type == OB_CLIENT_TYPE_DIALOG) {
        GSList *it;
        gboolean first = TRUE;
        gint l, r, t, b;
        for (it = client->parents; it; it = g_slist_next(it)) {
            ObClient *m = it->data;
            if (!m->iconic) {
                if (first) {
                    l = RECT_LEFT(m->frame->area);
                    t = RECT_TOP(m->frame->area);
                    r = RECT_RIGHT(m->frame->area);
                    b = RECT_BOTTOM(m->frame->area);
                    first = FALSE;
                } else {
                    l = MIN(l, RECT_LEFT(m->frame->area));
                    t = MIN(t, RECT_TOP(m->frame->area));
                    r = MAX(r, RECT_RIGHT(m->frame->area));
                    b = MAX(b, RECT_BOTTOM(m->frame->area));
                }
            }
            if (!first) {
                *x = ((r + 1 - l) - client->frame->area.width) / 2 + l; 
                *y = ((b + 1 - t) - client->frame->area.height) / 2 + t;
                return TRUE;
            }
        }
    }

    if (client->type == OB_CLIENT_TYPE_DIALOG ||
        client->type == OB_CLIENT_TYPE_SPLASH)
    {
        Rect **areas;

        areas = pick_head(client);

        *x = (areas[0]->width - client->frame->area.width) / 2 + areas[0]->x;
        *y = (areas[0]->height - client->frame->area.height) / 2 + areas[0]->y;

        g_free(areas);
        return TRUE;
    }

    return FALSE;
}

/* Return TRUE if we want client.c to enforce on-screen-keeping */
gboolean place_client(ObClient *client, gint *x, gint *y,
                      ObAppSettings *settings)
{
    gboolean ret;

    if (client->positioned)
        return FALSE;

    /* try a number of methods */
    ret = place_transient_splash(client, x, y) ||
        place_per_app_setting(client, x, y, settings) ||
        (config_place_policy == OB_PLACE_POLICY_MOUSE &&
         place_under_mouse(client, x, y)) ||
        place_nooverlap(client, x, y) ||
        place_under_mouse(client, x, y) ||
        place_random(client, x, y);
    g_assert(ret);

    /* get where the client should be */
    frame_frame_gravity(client->frame, x, y,
                        client->area.width, client->area.height);
    return ret;
}
