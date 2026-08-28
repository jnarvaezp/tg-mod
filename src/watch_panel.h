/*
    tg
    Copyright (C) 2015 Marcello Mamino

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef TG_WATCH_PANEL_H
#define TG_WATCH_PANEL_H

struct main_window;

/* Watchmaker's logbook: left panel with the watch registry and sessions. */
GtkWidget *watch_panel_build(struct main_window *w);

/* Rebuild the watch list and the session tree from watchdb. */
void watch_panel_refresh(struct main_window *w);

#endif
