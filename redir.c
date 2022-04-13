/*
 *  Intel AMT tcp redirection protocol helper functions.
 *
 *  Copyright (C) 2007 Gerd Hoffmann <kraxel@redhat.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

#include "tcp.h"
#include "ssl.h"
#include "redir.h"
#include "auth.h"

static const char *state_name[] = {
    [ REDIR_NONE      ] = "NONE",
    [ REDIR_CONNECT   ] = "CONNECT",
    [ REDIR_INIT      ] = "INIT",
    [ REDIR_AUTH      ] = "AUTH",
    [ REDIR_INIT_SOL  ] = "INIT_SOL",
    [ REDIR_RUN_SOL   ] = "RUN_SOL",
    [ REDIR_INIT_IDER ] = "INIT_IDER",
    [ REDIR_CFG_IDER  ] = "CFG_IDER",
    [ REDIR_RUN_IDER  ] = "RUN_IDER",
    [ REDIR_CLOSING   ] = "CLOSING",
    [ REDIR_CLOSED    ] = "CLOSED",
    [ REDIR_ERROR     ] = "ERROR",
};

static const char *state_desc[] = {
    [ REDIR_NONE      ] = "disconnected",
    [ REDIR_CONNECT   ] = "connection to host",
    [ REDIR_INIT      ] = "redirection initialization",
    [ REDIR_AUTH      ] = "session authentication",
    [ REDIR_INIT_SOL  ] = "serial-over-lan initialization",
    [ REDIR_RUN_SOL   ] = "serial-over-lan active",
    [ REDIR_INIT_IDER ] = "IDE redirect initialization",
    [ REDIR_CFG_IDER  ] = "IDE redirect configuration",
    [ REDIR_RUN_IDER  ] = "IDE redirect active",
    [ REDIR_CLOSING   ] = "redirection shutdown",
    [ REDIR_CLOSED    ] = "connection closed",
    [ REDIR_ERROR     ] = "failure",
};

/* ------------------------------------------------------------------ */

static void hexdump(const char *prefix, const unsigned char *data, size_t size)
{
    char ascii[17];
    int i;

    for (i = 0; i < size; i++) {
	if (0 == (i%16)) {
	    fprintf(stderr,"%s%s%04x:",
		    prefix ? prefix : "",
		    prefix ? ": "   : "",
		    i);
	    memset(ascii,0,sizeof(ascii));
	}
	if (0 == (i%4))
	    fprintf(stderr," ");
	fprintf(stderr," %02x",data[i]);
	ascii[i%16] = isprint(data[i]) ? data[i] : '.';
	if (15 == (i%16))
	    fprintf(stderr,"  %s\n",ascii);
    }
    if (0 != (i%16)) {
	while (0 != (i%16)) {
	    if (0 == (i%4))
		fprintf(stderr," ");
	    fprintf(stderr,"   ");
	    i++;
	};
	fprintf(stderr," %s\n",ascii);
    }
}

ssize_t redir_write(struct redir *r, const char *buf, size_t count)
{
    int rc;

    if (r->trace)
	hexdump("out", buf, count);
    rc = sslwrite(r->ctx, buf, count);
    if (-1 == rc)
	snprintf(r->err, sizeof(r->err), "write(socket): %s", strerror(errno));
    return rc;
}

static void redir_state(struct redir *r, enum redir_state new)
{
    enum redir_state old = r->state;

    r->state = new;
    if (r->cb_state)
	r->cb_state(r->cb_data, old, new);
}

/* ------------------------------------------------------------------ */

const char *redir_state_name(enum redir_state state)
{
    const char *name = NULL;

    if (state < sizeof(state_name)/sizeof(state_name[0]))
	name = state_name[state];
    if (NULL == name)
	name = "unknown";
    return name;
}

const char *redir_state_desc(enum redir_state state)
{
    const char *desc = NULL;

    if (state < sizeof(state_desc)/sizeof(state_desc[0]))
	desc = state_desc[state];
    if (NULL == desc)
	desc = "unknown";
    return desc;
}

int redir_connect(struct redir *r)
{
    static unsigned char *defport = "16994";
    static unsigned char *sslport = "16995";
    struct addrinfo ai;

    if (r->cacert)
	defport = sslport;
    memset(&ai, 0, sizeof(ai));
    ai.ai_socktype = SOCK_STREAM;
    ai.ai_family = PF_UNSPEC;
    tcp_verbose = r->verbose;
    redir_state(r, REDIR_CONNECT);
    r->sock = tcp_connect(&ai, NULL, NULL, r->host,
			  strlen(r->port) ? r->port : defport);
    r->ctx = sslinit(r->sock, r->cacert);
    if(r->ctx == NULL) {
	close(r->sock);
	r->sock = -1;
    }
    if (-1 == r->sock) {
	redir_state(r, REDIR_ERROR);
	/* FIXME: better error message */
	snprintf(r->err, sizeof(r->err), "connect failed");
	return -1;
    }
    return 0;
}

int redir_start(struct redir *r)
{
    unsigned char request[START_REDIRECTION_SESSION_LENGTH] = {
	START_REDIRECTION_SESSION, 0, 0, 0,  0, 0, 0, 0
    };

    memcpy(request+4, r->type, 4);
    redir_state(r, REDIR_INIT);
    return redir_write(r, request, sizeof(request));
}

int redir_stop(struct redir *r)
{
    unsigned char request[END_REDIRECTION_SESSION_LENGTH] = {
	END_REDIRECTION_SESSION, 0, 0, 0
    };

    redir_state(r, REDIR_CLOSED);
    redir_write(r, request, sizeof(request));
    sslexit(r->ctx);
    r->ctx = NULL;
    close(r->sock);
    return 0;
}

static int redir_auth_old(struct redir *r)
{
    int ulen = strlen(r->user);
    int plen = strlen(r->pass);
    int len = 11+ulen+plen;
    int rc;
    unsigned char *request = malloc(len);

    memset(request, 0, len);
    request[0] = AUTHENTICATE_SESSION;
    request[4] = 0x01;
    request[5] = ulen+plen+2;
    request[9] = ulen;
    memcpy(request + 10, r->user, ulen);
    request[10 + ulen] = plen;
    memcpy(request + 11 + ulen, r->pass, plen);
    redir_state(r, REDIR_AUTH);
    rc = redir_write(r, request, len);
    free(request);
    return rc;
}

static int io(void *parm, unsigned char *data, int len, int mode)
{
    int rc;
    struct redir *r;
    struct timeval tv;
    fd_set set;

    switch(mode)
    {
    case READ:
	r = (struct redir *)parm;
	while (len) {
	    FD_ZERO(&set);
	    FD_SET(r->sock,&set);
	    if (!sslready(r->ctx)) {
		tv.tv_sec  = HEARTBEAT_INTERVAL * 4 / 1000;
		tv.tv_usec = 0;
		switch (select(r->sock+1,&set,NULL,NULL,&tv)) {
		case -1:
		    perror("select");
		    return -1;
		case 0:
		    fprintf(stderr,"select: timeout\n");
		    return -1;
		}
	    }
	    rc = sslread(r->ctx, data, len);
	    switch (rc) {
	    case -1:
		fprintf(stderr, "read(socket): %s", strerror(errno));
		return -1;
	    case 0:
		fprintf(stderr, "EOF from socket");
		return -1;
	    default:
		if (r->trace)
		    hexdump("in ", data, rc);
		data += rc;
		len -= rc;
	    }
	}
	return 0;

    case WRITE:
	r = (struct redir *)parm;
	if (redir_write(r, data, len) != len)
	    return -1;
	return 0;

    case RANDOM:
	gettimeofday(&tv, NULL);
	if (sizeof(tv) <= len)
	    memcpy(data, &tv, sizeof(tv));
	else
	    memcpy(data, &tv, len);
	return 0;
    }

    return -1;
}

int redir_auth(struct redir *r)
{
    int rc;

    if (r->legacy)
	return redir_auth_old(r);

    redir_state(r, REDIR_AUTH);
    rc = authenticate(0, r->user, r->pass, io, r);
    return rc;
}

int redir_sol_start(struct redir *r)
{
    unsigned char request[START_SOL_REDIRECTION_LENGTH] = {
	START_SOL_REDIRECTION, 0, 0, 0,
	0, 0, 0, 0,
	MAX_TRANSMIT_BUFFER & 0xff,
	MAX_TRANSMIT_BUFFER >> 8,
	TRANSMIT_BUFFER_TIMEOUT & 0xff,
	TRANSMIT_BUFFER_TIMEOUT >> 8,
	TRANSMIT_OVERFLOW_TIMEOUT & 0xff,	TRANSMIT_OVERFLOW_TIMEOUT >> 8,
	HOST_SESSION_RX_TIMEOUT & 0xff,
	HOST_SESSION_RX_TIMEOUT >> 8,
	HOST_FIFO_RX_FLUSH_TIMEOUT & 0xff,
	HOST_FIFO_RX_FLUSH_TIMEOUT >> 8,
	HEARTBEAT_INTERVAL & 0xff,
	HEARTBEAT_INTERVAL >> 8,
	0, 0, 0, 0
    };

    redir_state(r, REDIR_INIT_SOL);
    return redir_write(r, request, sizeof(request));
}

int redir_sol_stop(struct redir *r)
{
    unsigned char request[END_SOL_REDIRECTION_LENGTH] = {
	END_SOL_REDIRECTION, 0, 0, 0,
	0, 0, 0, 0,
    };

    redir_state(r, REDIR_CLOSING);
    return redir_write(r, request, sizeof(request));
}

int redir_sol_send(struct redir *r, unsigned char *buf, int blen)
{
    int len = 10+blen;
    int rc;
    unsigned char *request = malloc(len);

    memset(request, 0, len);
    request[0] = SOL_DATA_TO_HOST;
    request[8] = blen & 0xff;
    request[9] = blen >> 8;
    memcpy(request + 10, buf, blen);
    rc = redir_write(r, request, len);
    free(request);
    return rc;
}

int redir_sol_recv(struct redir *r)
{
    unsigned char msg[64];
    int count, len, bshift;
    int flags;

    len = r->buf[8] + (r->buf[9] << 8);
    count = r->blen - 10;
    if (count > len)
	count = len;
    bshift = count + 10;
    if (r->cb_recv)
	r->cb_recv(r->cb_data, r->buf + 10, count);
    len -= count;

    while (len) {
	if (r->trace)
	    fprintf(stderr, "in+: need %d more data bytes\n", len);
	count = sizeof(msg);
	if (count > len)
	    count = len;
	/* temporarily switch to blocking.  the actual data may not be
	   ready yet, but should be here Real Soon Now. */
	flags = fcntl(r->sock,F_GETFL);
	fcntl(r->sock,F_SETFL, flags & (~O_NONBLOCK));
	count = sslread(r->ctx, msg, count);
	fcntl(r->sock,F_SETFL, flags);

	switch (count) {
	case -1:
	    snprintf(r->err, sizeof(r->err), "read(socket): %s", strerror(errno));
	    return -1;
	case 0:
	    snprintf(r->err, sizeof(r->err), "EOF from socket");
	    return -1;
	default:
	    if (r->trace)
		hexdump("in+", msg, count);
	    if (r->cb_recv)
		r->cb_recv(r->cb_data, msg, count);
	    len -= count;
	}
    }

    return bshift;
}

int redir_ider_start(struct redir *r)
{
    unsigned char request[START_IDER_REDIRECTION_LENGTH] = {
	START_IDER_REDIRECTION,
	TRANSMIT_BUFFER_TIMEOUT & 0xff,
	TRANSMIT_BUFFER_TIMEOUT >> 8,
	HOST_SESSION_RX_TIMEOUT & 0xff,
	HOST_SESSION_RX_TIMEOUT >> 8,
	HEARTBEAT_INTERVAL & 0xff,
	HEARTBEAT_INTERVAL >> 8,
	1, 0, 0, 0
    };

    redir_state(r, REDIR_INIT_IDER);
    return redir_write(r, request, sizeof(request));
}

int redir_ider_config(struct redir *r)
{
    unsigned char request[IDER_DISABLE_ENABLE_FEATURES_LENGTH] = {
	IDER_DISABLE_ENABLE_FEATURES, 0, 0, 0,
	r->seqno & 0xff, (r->seqno >> 8) && 0xff,
	(r->seqno >> 16) & 0xff, (r->seqno >> 24) & 0xff,
	IDER_FEATURE_SET_REGISTER_STATE,
	IDER_FEATURE_ENABLE | r->enable_options, 0, 0, 0
    };
    redir_state(r, REDIR_CFG_IDER);
    r->seqno++;
    return redir_write(r, request, sizeof(request));
}

int redir_ider_reset(struct redir *r, unsigned int seqno)
{
    unsigned char request[IDER_RESET_OCCURED_RESPONSE_LENGTH] = {
	IDER_RESET_OCCURED_RESPONSE, 0, 0, 0,
	seqno & 0xff, (seqno >> 8) & 0xff,
	(seqno >> 16) & 0xff, (seqno >> 24) & 0xff,
    };

    return redir_write(r, request, sizeof(request));
}

int redir_ider_stop(struct redir *r)
{
    unsigned char request[END_IDER_REDIRECTION_LENGTH] = {
	END_IDER_REDIRECTION, 0, 0, 0,
	0, 0, 0, 0,
    };

    redir_state(r, REDIR_CLOSING);
    return redir_write(r, request, sizeof(request));
}

static int redir_ider_command(struct redir *r, unsigned int seqno)
{
    struct ider_command_written_message *msg =
	(struct ider_command_written_message *)r->buf;
    unsigned char device = msg->drive_select & 0x10 ? 0xb0 : 0xa0;
    bool use_dma = msg->feature & 1;
    int i;

    if (msg->command != 0xa0) {
	snprintf(r->err, sizeof(r->err), "Unhandled IDE command %02x",
		 msg->command);
	return -1;
    }
    fprintf(stderr, "command %02x (%sdma): ", msg->command,
	    use_dma ? "" : "non-");
    for (i = 0; i < sizeof(msg->packet_data); i++)
	fprintf(stderr, "%02x ", msg->packet_data[i]);
    fprintf(stderr, "\n");

    return ider_handle_command(r, seqno, device, use_dma, msg->packet_data);
}

int redir_ider_recv(struct redir *r)
{
    unsigned char msg[64];
    int count, len, bshift;
    int flags;

    len = r->buf[8] + (r->buf[9] << 8);
    count = r->blen - 10;
    if (count > len)
	count = len;
    bshift = count + 10;
    if (r->cb_recv)
	r->cb_recv(r->cb_data, r->buf + 10, count);
    len -= count;

    while (len) {
	if (r->trace)
	    fprintf(stderr, "in+: need %d more data bytes\n", len);
	count = sizeof(msg);
	if (count > len)
	    count = len;
	/* temporarily switch to blocking.  the actual data may not be
	   ready yet, but should be here Real Soon Now. */
	flags = fcntl(r->sock,F_GETFL);
	fcntl(r->sock,F_SETFL, flags & (~O_NONBLOCK));
	count = sslread(r->ctx, msg, count);
	fcntl(r->sock,F_SETFL, flags);

	switch (count) {
	case -1:
	    snprintf(r->err, sizeof(r->err), "read(socket): %s", strerror(errno));
	    return -1;
	case 0:
	    snprintf(r->err, sizeof(r->err), "EOF from socket");
	    return -1;
	default:
	    if (r->trace)
		hexdump("in+", msg, count);
	    if (r->cb_recv)
		r->cb_recv(r->cb_data, msg, count);
	    len -= count;
	}
    }

    return bshift;
}

static inline unsigned int redir_hdr_seqno(struct redir *r)
{
    return (unsigned int)r->buf[4] |
	(unsigned int)r->buf[5] << 8 |
	(unsigned int)r->buf[6] << 16 |
	(unsigned int)r->buf[7] << 24;
}

static int in_loopback_mode = 0;
static int powered_off = 0;

int redir_data(struct redir *r)
{
    int rc, bshift;
    unsigned int seqno;

repeat:
    if (r->trace) {
	fprintf(stderr, "in --\n");
	if (r->blen)
	    fprintf(stderr, "in : already have %d\n", r->blen);
    }
    rc = sslread(r->ctx, r->buf + r->blen, sizeof(r->buf) - r->blen);
    switch (rc) {
    case -1:
	snprintf(r->err, sizeof(r->err), "read(socket): %s", strerror(errno));
	goto err;
    case 0:
	snprintf(r->err, sizeof(r->err), "EOF from socket");
	goto err;
    default:
	if (r->trace)
	    hexdump("in ", r->buf + r->blen, rc);
	r->blen += rc;
    }

    for (;;) {
	if (r->blen < 4)
	    goto again;
	bshift = 0;

	switch (r->buf[0]) {
	case START_REDIRECTION_SESSION_REPLY:
	    bshift = START_REDIRECTION_SESSION_REPLY_LENGTH;
	    if (r->blen < bshift)
		goto again;
	    if (r->buf[1] != STATUS_SUCCESS) {
		snprintf(r->err, sizeof(r->err), "redirection session start failed");
		goto err;
	    }
	    if (-1 == redir_auth(r))
		goto err;
	    break;
	case AUTHENTICATE_SESSION_REPLY:
	    bshift = r->blen; /* FIXME */
	    if (r->blen < bshift)
		goto again;
	    if (r->buf[1] != STATUS_SUCCESS) {
		snprintf(r->err, sizeof(r->err), "session authentication failed");
		goto err;
	    }
	    if (!memcmp(r->type, "SOL ", 4)) {
		if (-1 == redir_sol_start(r))
		    goto err;
	    } else if (!memcmp(r->type, "IDER", 4)) {
		if (-1 == redir_ider_start(r))
		    goto err;
	    } else
		goto err;
	    break;
	case START_SOL_REDIRECTION_REPLY:
	    bshift = r->blen; /* FIXME */
	    if (r->blen < bshift)
		goto again;
	    if (r->buf[1] != STATUS_SUCCESS) {
		snprintf(r->err, sizeof(r->err), "serial-over-lan redirection failed");
		goto err;
	    }
	    redir_state(r, REDIR_RUN_SOL);
	    break;
	case SOL_HEARTBEAT:
	case SOL_KEEP_ALIVE_PING:
	case IDER_HEARTBEAT:
	case IDER_KEEP_ALIVE_PING:
	    bshift = HEARTBEAT_LENGTH;
	    if (r->blen < bshift)
		goto again;
	    if (HEARTBEAT_LENGTH != redir_write(r, r->buf, HEARTBEAT_LENGTH))
		goto err;
	    break;
	case SOL_DATA_FROM_HOST:
	    if (r->blen < 10) /* header length */
		goto again;
	    bshift = redir_sol_recv(r);
	    if (bshift < 0)
		goto err;
	    break;
	case END_SOL_REDIRECTION_REPLY:
	    bshift = r->blen; /* FIXME */
	    if (r->blen < bshift)
		goto again;
	    redir_stop(r);
	    break;
	case SOL_CONTROLS_FROM_HOST:
	{
	    bshift = r->blen; /* FIXME */
	    if (r->blen < bshift)
		goto again;

	    /* Host sends this message to the Management Console when
             * the host has changed its COM port control lines. This
             * message is likely to be one of the first messages that
             * the Host sends to the Console after it starts SOL
             * redirection.
             */
	    struct controls_from_host_message *msg = (struct controls_from_host_message *) r->buf;
	    //printf("Type %x, control %d, status %d\n", msg->type, msg->control, msg->status);
	    if (msg->status & LOOPBACK_ACTIVE) {
		if (r->verbose)
		    fprintf (stderr, "Warning, SOL device is running in loopback mode."
			     "  Text input may not be accepted\n");
		in_loopback_mode = 1;
	    } else if (in_loopback_mode) {
		if (r->verbose)
		    fprintf (stderr, "SOL device is no longer running in loopback mode\n");
		in_loopback_mode = 0;
	    }

	    if (0 == (msg->status & SYSTEM_POWER_STATE))  {
		if (r->verbose)
		    fprintf (stderr, "The system is powered off.\n");
		powered_off = 1;
	    } else if (powered_off) {
		if (r->verbose)
		    fprintf (stderr, "The system is powered on.\n");
		powered_off = 0;
	    }

	    if (r->verbose) {
		if (msg->status & (TX_OVERFLOW|RX_FLUSH_TIMEOUT|TESTMODE_ACTIVE))
		    fprintf (stderr, "Other unhandled status condition\n");

		if (msg->control & RTS_CONTROL)
		    fprintf (stderr, "RTS is asserted on the COM Port\n");

		if (msg->control & DTR_CONTROL)
		    fprintf (stderr, "DTR is asserted on the COM Port\n");

		if (msg->control & BREAK_CONTROL)
		    fprintf (stderr, "BREAK is asserted on the COM Port\n");
	    }
	    break;
	}
	case START_IDER_REDIRECTION_REPLY:
	    bshift = r->blen; /* FIXME */
	    if (r->blen < bshift)
		goto again;
	    if (r->buf[1] != STATUS_SUCCESS) {
		snprintf(r->err, sizeof(r->err), "IDE redirection failed");
		goto err;
	    }
	    r->tx_bufsize = (unsigned int)r->buf[16] |
		(unsigned int)r->buf[17] << 8;
	    r->rx_bufsize = (unsigned int)r->buf[18] |
		(unsigned int)r->buf[19] << 8;
	    fprintf(stderr,"IDE redirection enabled, features %d iana %02x%02x%02x%02x\n",
		    r->buf[21], r->buf[25], r->buf[26], r->buf[27], r->buf[28]);
	    if (-1 == redir_ider_config(r))
		goto err;
	    break;
	case IDER_DISABLE_ENABLE_FEATURES_REPLY:
	    bshift = r->blen;
	    if (r->blen < IDER_DISABLE_ENABLE_FEATURES_REPLY_LENGTH)
		goto again;
	    if (r->seqno != redir_hdr_seqno(r))
		goto err;
	    redir_state(r, REDIR_RUN_IDER);
	    break;
	case IDER_RESET_OCCURED:
	    bshift = r->blen;
	    seqno = redir_hdr_seqno(r);
	    fprintf(stderr, "seqno %u: reset, mask %u\n", seqno, r->buf[8]);
	    if (-1 == redir_ider_reset(r, seqno))
		goto err;
	    break;
	case IDER_COMMAND_WRITTEN:
	    bshift = r->blen;
	    if (r->blen < sizeof(struct ider_command_written_message))
		goto again;
	    seqno = redir_hdr_seqno(r);
	    if (-1 == redir_ider_command(r, seqno))
		goto err;
	    break;
	case IDER_DATA_FROM_HOST:
	    if (r->blen < 10) /* header length */
		goto again;
	    bshift = redir_ider_recv(r);
	    if (bshift < 0)
		goto err;
	    break;
	case END_IDER_REDIRECTION_REPLY:
	    bshift = r->blen; /* FIXME */
	    if (r->blen < bshift)
		goto again;
	    redir_state(r, REDIR_CLOSED);
	    sslexit(r->ctx);
	    r->ctx = NULL;
	    close(r->sock);
	    r->sock = -1;
	    break;
	default:
	    snprintf(r->err, sizeof(r->err), "%s: unknown r->buf 0x%02x",
		     __FUNCTION__, r->buf[0]);
	    goto err;
	}

	if (bshift == r->blen) {
	    r->blen = 0;
	    break;
	}

	/* have more data, shift by bshift */
	if (r->trace)
	    fprintf(stderr, "in : shift by %d\n", bshift);
	memmove(r->buf, r->buf + bshift, r->blen - bshift);
	r->blen -= bshift;
    }
    if (r->ctx && sslready(r->ctx))
	goto repeat;
    return 0;

again:
    /* need more data, jump back into poll/select loop */
    if (r->trace)
	fprintf(stderr, "in : need more data\n");
    if (sslready(r->ctx))
	goto repeat;
    return 0;

err:
    if (r->trace)
	fprintf(stderr, "in : ERROR (%s)\n", r->err);
    redir_state(r, REDIR_ERROR);
    sslexit(r->ctx);
    r->ctx = NULL;
    close(r->sock);
    return -1;
}
