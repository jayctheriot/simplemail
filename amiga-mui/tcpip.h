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
** tcpip.h
*/

#include <proto/socket.h> /* Now it actually _is_ nice!!! :) */ /* Yes :-) */
/*#include <proto/amissl.h>*/

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

int open_socket_lib(void);
void close_socket_lib(void);
int open_ssl_lib(void);
void close_ssl_lib(void);
SSL_CTX *ssl_context(void);

long tcp_herrno(void);
long tcp_errno(void);
