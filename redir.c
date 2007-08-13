#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "redir.h"

static const char *state_names[] = {
    [ REDIR_NONE      ] = "NONE",
    [ REDIR_INIT      ] = "INIT",
    [ REDIR_AUTH      ] = "AUTH",
    [ REDIR_INIT_SOL  ] = "INIT_SOL",
    [ REDIR_CONN_SOL  ] = "CONN_SOL",
    [ REDIR_INIT_IDER ] = "INIT_IDER",
    [ REDIR_CONN_IDER ] = "CONN_IDER",
    [ REDIR_CLOSING   ] = "CLOSING",
    [ REDIR_CLOSED    ] = "CLOSED",
    [ REDIR_ERROR     ] = "ERROR",
};

/* ------------------------------------------------------------------ */

static void redir_state(struct redir *r, enum redir_state new)
{
    enum redir_state old = r->state;

    r->state = new;
    if (r->cb_state)
	r->cb_state(r->cb_data, old, new);
}

/* ------------------------------------------------------------------ */

const char *redir_strstate(enum redir_state state)
{
    const char *name = NULL;

    if (state < sizeof(state_names)/sizeof(state_names[0]))
	name = state_names[state];
    if (NULL == name)
	name = "unknown";
    return name;
}

int redir_start(struct redir *r)
{
    unsigned char request[START_REDIRECTION_SESSION_LENGTH] = {
	START_REDIRECTION_SESSION, 0, 0, 0,  0, 0, 0, 0
    };

    memcpy(request+4, r->type, 4);
    redir_state(r, REDIR_INIT);
    return write(r->sock, request, sizeof(request));
}

int redir_stop(struct redir *r)
{
    unsigned char request[END_REDIRECTION_SESSION_LENGTH] = {
	END_REDIRECTION_SESSION, 0, 0, 0
    };

    redir_state(r, REDIR_CLOSED);
    return write(r->sock, request, sizeof(request));
}

int redir_auth(struct redir *r)
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
    rc = write(r->sock, request, len);
    free(request);
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
    return write(r->sock, request, sizeof(request));
}

int redir_sol_stop(struct redir *r)
{
    unsigned char request[END_SOL_REDIRECTION_LENGTH] = {
	END_SOL_REDIRECTION, 0, 0, 0,
	0, 0, 0, 0,
    };

    redir_state(r, REDIR_CLOSING);
    return write(r->sock, request, sizeof(request));
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
    rc = write(r->sock, request, len);
    free(request);
    return rc;
}

int redir_sol_recv(struct redir *r, unsigned char *buf, int blen)
{
    unsigned char msg[64];
    int count, len;

    len = buf[8] + (buf[9] << 8);
    count = blen - 10;
    if (r->cb_recv)
	r->cb_recv(r->cb_data, buf + 10, count);
    len -= count;
    while (len) {
	count = sizeof(msg);
	if (count > len)
	    count = len;
	count = read(r->sock, msg, count);
	switch (count) {
	case -1:
	    perror("read(sock)");
	    return -1;
	case 0:
	    fprintf(stderr, "EOF from socket\n");
	    return -1;
	default:
	    if (r->cb_recv)
		r->cb_recv(r->cb_data, msg, count);
	    len -= count;
	}
    }
    return 0;
}

int redir_data(struct redir *r)
{
    unsigned char request[64];
    int rc;

    rc = read(r->sock, request, sizeof(request));
    if (rc < 4)
	goto err;
    switch (request[0]) {
    case START_REDIRECTION_SESSION_REPLY:
	if (rc != START_REDIRECTION_SESSION_REPLY_LENGTH) {
	    fprintf(stderr,"START_REDIRECTION_SESSION_REPLY: got %d, expected %d bytes\n",
		    rc, START_REDIRECTION_SESSION_REPLY_LENGTH);
	    goto err;
	}
	if (request[1] != STATUS_SUCCESS) {
	    fprintf(stderr, "redirection session start failed\n");
	    goto err;
	}
	return redir_auth(r);
    case AUTHENTICATE_SESSION_REPLY:
	if (request[1] != STATUS_SUCCESS) {
	    fprintf(stderr, "session authentication failed\n");
	    goto err;
	}
	return redir_sol_start(r);
    case START_SOL_REDIRECTION_REPLY:
	if (request[1] != STATUS_SUCCESS) {
	    fprintf(stderr, "serial-over-lan redirection failed\n");
	    goto err;
	}
	redir_state(r, REDIR_CONN_SOL);
	return 0;
    case SOL_HEARTBEAT:
    case SOL_KEEP_ALIVE_PING:
    case IDER_HEARTBEAT:
    case IDER_KEEP_ALIVE_PING:
	if (rc != HEARTBEAT_LENGTH) {
	    fprintf(stderr,"HEARTBEAT: got %d, expected %d bytes\n",
		    rc, HEARTBEAT_LENGTH);
	    goto err;
	}
	if (HEARTBEAT_LENGTH != write(r->sock, request, HEARTBEAT_LENGTH)) {
	    perror("write(sock)");
	    goto err;
	}
	return 0;
    case SOL_DATA_FROM_HOST:
	return redir_sol_recv(r, request, rc);
    case END_SOL_REDIRECTION_REPLY:
	redir_stop(r);
	break;
    default:
	fprintf(stderr, "%s: unknown request 0x%02x\n", __FUNCTION__, request[0]);
	goto err;
    }
    return 0;

err:
    redir_state(r, REDIR_ERROR);
    return -1;
}
