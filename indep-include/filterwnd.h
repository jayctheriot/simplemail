/***************************************************************************
 SimpleMail - Copyright (C) 2000 Hynek Schlawack and Sebastian Bauer

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
***************************************************************************/

/**
 * @file filterwnd.h
 */

#ifndef SM__FILTERWND_H
#define SM__FILTERWND_H

struct filter;

/**
 * Opens the filter window.
 */
void filter_open(void);

/**
 * Opens the filter window with a new filter.
 *
 * @param nf defines the filter that should be added. The
 * object copied so the argument can be freed after calling
 * the function.
 */
void filter_open_with_new_filter(struct filter *nf);

/**
 * Refreshes the folder list. It should be called, whenever the folder list
 * has been changed.
 */
void filter_update_folder_list(void);

#endif

