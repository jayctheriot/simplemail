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

/*
** support_indep.h
*/

#ifndef SM__SUPPORT_INDEP_H
#define SM__SUPPORT_INDEP_H

int mystrcmp(const char *str1, const char *str2);
int mystricmp(const char *str1, const char *str2);
int mystrnicmp(const char *str1, const char *str2, int n);
char *mystristr(const char *str1, const char *str2);
unsigned int mystrlen(const char *str);
char *mystrdup(const char *str);
char *mystrndup(const char *str, int len);
size_t mystrlcpy(char *dest, const char *src, size_t n);

#endif
