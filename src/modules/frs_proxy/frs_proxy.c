/*
 * proxy.c	Handle PROXY traffic.
 *
 * Version:	$Id$
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2007  The FreeRADIUS server project
 * Copyright 2007  Alan DeKok <aland@deployingradius.com>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>


/*
 *	Send a packet to a home server.
 *
 *	FIXME: have different code for proxy auth & acct!
 */
static int proxy_socket_send(rad_listen_t *listener, REQUEST *request)
{
	listen_socket_t *sock = listener->data;

	rad_assert(request->proxy_listener == listener);
	rad_assert(listener->send == proxy_socket_send);

	request->proxy->src_ipaddr = sock->ipaddr;
	request->proxy->src_port = sock->port;

	return rad_send(request->proxy, request->packet,
			request->home_server->secret);
}

/*
 *	Recieve packets from a proxy socket.
 */
static int proxy_socket_recv(rad_listen_t *listener,
			      RAD_REQUEST_FUNP *pfun, REQUEST **prequest)
{
	REQUEST		*request;
	RADIUS_PACKET	*packet;
	char		buffer[128];

	packet = rad_recv(listener->fd, 0);
	if (!packet) {
		radlog(L_ERR, "%s", fr_strerror());
		return 0;
	}

	/*
	 *	FIXME: Client MIB updates?
	 */
	switch(packet->code) {
	case PW_AUTHENTICATION_ACK:
	case PW_ACCESS_CHALLENGE:
	case PW_AUTHENTICATION_REJECT:
#ifdef WITH_ACCOUNTING
	case PW_ACCOUNTING_RESPONSE:
#endif
		break;

	default:
		/*
		 *	FIXME: Update MIB for packet types?
		 */
		radlog(L_ERR, "Invalid packet code %d sent to a proxy port "
		       "from home server %s port %d - ID %d : IGNORED",
		       packet->code,
		       ip_ntoh(&packet->src_ipaddr, buffer, sizeof(buffer)),
		       packet->src_port, packet->id);
		rad_free(&packet);
		return 0;
	}

	request = received_proxy_response(packet);
	if (!request) {
		return 0;
	}

	rad_assert(request->process != NULL);

	*pfun = request->process;
	*prequest = request;

	return 1;
}

static int proxy_socket_encode(UNUSED rad_listen_t *listener, REQUEST *request)
{
	rad_encode(request->proxy, NULL, request->home_server->secret);
	rad_sign(request->proxy, NULL, request->home_server->secret);

	return 0;
}


static int proxy_socket_decode(UNUSED rad_listen_t *listener, REQUEST *request)
{
	if (rad_verify(request->proxy_reply, request->proxy,
		       request->home_server->secret) < 0) {
		return -1;
	}

	return rad_decode(request->proxy_reply, request->proxy,
			   request->home_server->secret);
}

frs_module_t frs_proxy = {
	FRS_MODULE_INIT, RAD_LISTEN_PROXY, "proxy",
	listen_socket_parse, NULL,
	proxy_socket_recv, proxy_socket_send,
	listen_socket_print, proxy_socket_encode, proxy_socket_decode
};
