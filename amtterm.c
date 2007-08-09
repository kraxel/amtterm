#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "RedirectionConstants.h"
#include "tcp.h"

#define APPNAME "amtterm"
#define BUFSIZE 512

struct redir {
    int sock;
    int connected;
    int verbose;
    int closed;
    unsigned char type[4];
    unsigned char user[16];
    unsigned char pass[16];
};

/* ------------------------------------------------------------------ */

static int redir_start(struct redir *r)
{
    unsigned char request[START_REDIRECTION_SESSION_LENGTH] = {
	START_REDIRECTION_SESSION, 0, 0, 0,  0, 0, 0, 0
    };

    memcpy(request+4, r->type, 4);
    return write(r->sock, request, sizeof(request));
}

static int redir_stop(struct redir *r)
{
    unsigned char request[END_REDIRECTION_SESSION_LENGTH] = {
	END_REDIRECTION_SESSION, 0, 0, 0
    };

    return write(r->sock, request, sizeof(request));
}

static int redir_auth(struct redir *r)
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
    rc = write(r->sock, request, len);
    free(request);
    return rc;
}

static int redir_sol_start(struct redir *r)
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

    return write(r->sock, request, sizeof(request));
}

static int redir_sol_stop(struct redir *r)
{
    unsigned char request[END_SOL_REDIRECTION_LENGTH] = {
	END_SOL_REDIRECTION, 0, 0, 0,
	0, 0, 0, 0,
    };

    return write(r->sock, request, sizeof(request));
}

static int redir_sol_send(struct redir *r, unsigned char *buf, int blen)
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

static int redir_sol_recv(struct redir *r, unsigned char *buf, int blen)
{
    unsigned char msg[64];
    int count, len;

    len = buf[8] + (buf[9] << 8);
    count = blen - 10;
    write(0, buf + 10, count);
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
	    write(0, msg, count);
	    len -= count;
	}
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static int redir_data(struct redir *r)
{
    unsigned char request[64];
    int rc;

    rc = read(r->sock, request, sizeof(request));
    if (rc < 4)
	return -1;
    switch (request[0]) {
    case START_REDIRECTION_SESSION_REPLY:
	if (rc != START_REDIRECTION_SESSION_REPLY_LENGTH) {
	    fprintf(stderr,"START_REDIRECTION_SESSION_REPLY: got %d, expected %d bytes\n",
		    rc, START_REDIRECTION_SESSION_REPLY_LENGTH);
	    return -1;
	}
	if (request[1] != STATUS_SUCCESS) {
	    fprintf(stderr, "redirection session start failed\n");
	    return -1;
	}
	if (r->verbose)
	    fprintf(stderr, "redirection session start ok\n");
	return redir_auth(r);
    case AUTHENTICATE_SESSION_REPLY:
	if (request[1] != STATUS_SUCCESS) {
	    fprintf(stderr, "session authentication failed\n");
	    return -1;
	}
	if (r->verbose)
	    fprintf(stderr, "session authentication ok\n");
	return redir_sol_start(r);
    case START_SOL_REDIRECTION_REPLY:
	if (request[1] != STATUS_SUCCESS) {
	    fprintf(stderr, "serial-over-lan redirection failed\n");
	    return -1;
	}
	if (r->verbose) {
	    fprintf(stderr, "serial-over-lan redirection ok\n");
	    fprintf(stderr, "connected now, use ^] to escape\n");
	}
	r->connected = 1;
	return 0;
    case SOL_HEARTBEAT:
	if (rc != HEARTBEAT_LENGTH) {
	    fprintf(stderr,"HEARTBEAT: got %d, expected %d bytes\n",
		    rc, HEARTBEAT_LENGTH);
	    return -1;
	}
	if (HEARTBEAT_LENGTH != write(r->sock, request, HEARTBEAT_LENGTH)) {
	    perror("write(sock)");
	    return -1;
	}
	return 0;
    case SOL_DATA_FROM_HOST:
	return redir_sol_recv(r, request, rc);
    case END_SOL_REDIRECTION_REPLY:
	redir_stop(r);
	r->closed = 1;
	break;
    default:
	fprintf(stderr, "%s: unknown request 0x%02x\n", __FUNCTION__, request[0]);
	return -1;
    }
    return 0;
}

static int redir_loop(struct redir *r)
{
    unsigned char buf[BUFSIZE+1];
    struct timeval tv;
    int rc;
    fd_set set;

    for(;!r->closed;) {
	FD_ZERO(&set);
	if (r->connected)
	    FD_SET(0,&set);
	FD_SET(r->sock,&set);
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
	
	if (FD_ISSET(0,&set)) {
	    /* stdin has data */
	    rc = read(0,buf,BUFSIZE);
	    switch (rc) {
	    case -1:
		perror("read(stdin)");
		return -1;
	    case 0:
		fprintf(stderr,"EOF from stdin\n");
		return -1;
	    default:
		if (buf[0] == 0x1d) {
		    if (r->verbose)
			fprintf(stderr, "\n" APPNAME ": saw ^], exiting\n");
		    redir_sol_stop(r);
		    r->connected = 0;
		}
		if (-1 == redir_sol_send(r, buf, rc))
		    return -1;
		break;
	    }
	}

	if (FD_ISSET(r->sock,&set)) {
	    if (-1 == redir_data(r))
		return -1;
	}
    }
    return 0;
}

/* ------------------------------------------------------------------ */

struct termios  saved_attributes;
int             saved_fl;

static void tty_save(void)
{
    fcntl(0,F_GETFL,&saved_fl);
    tcgetattr (0, &saved_attributes);
}

static void tty_noecho(void)
{
    struct termios tattr;
    
    memcpy(&tattr,&saved_attributes,sizeof(struct termios));
    tattr.c_lflag &= ~(ECHO);
    tcsetattr (0, TCSAFLUSH, &tattr);
}

static void tty_raw(void)
{
    struct termios tattr;
    
    fcntl(0,F_SETFL,O_NONBLOCK);
    memcpy(&tattr,&saved_attributes,sizeof(struct termios));
    tattr.c_lflag &= ~(ISIG|ICANON|ECHO);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tcsetattr (0, TCSAFLUSH, &tattr);
}

static void tty_restore(void)
{
    fcntl(0,F_SETFL,saved_fl);
    tcsetattr (0, TCSANOW, &saved_attributes);
}

/* ------------------------------------------------------------------ */

static void usage(FILE *fp)
{
    fprintf(fp,
            "\n"
            "Establish serial-over-lan connection to Intel AMT boxes.\n"
            "\n"
            "usage: " APPNAME " [options] host [port]\n"
            "options:\n"
            "   -h            print this text\n"
            "   -v            verbose (default)\n"
            "   -q            quiet\n"
            "   -u user       username (default: admin)\n"
            "   -p pass       password (default: $AMT_PASSWORD)\n"
            "\n"
            "By default port 16994 is used.\n"
	    "If no password is given " APPNAME " will ask for one.\n"
            "\n"
            "-- \n"
            "(c) 2007 Gerd Hoffmann <kraxel@redhat.com>\n"
	    "\n");
}

int main(int argc, char *argv[])
{
    struct addrinfo ai;
    struct redir r;
    char *port = "16994";
    char *host = NULL;
    char *h;
    int c;

    memset(&r, 0, sizeof(r));
    r.verbose = 1;
    memcpy(r.type, "SOL ", 4);
    strcpy(r.user, "admin");

    if (NULL != (h = getenv("AMT_PASSWORD")))
	snprintf(r.pass, sizeof(r.pass), "%s", h);

    for (;;) {
        if (-1 == (c = getopt(argc, argv, "hvqu:p:")))
            break;
        switch (c) {
	case 'v':
	    r.verbose = 1;
	    break;
	case 'q':
	    r.verbose = 0;
	    break;
	case 'u':
	    snprintf(r.user, sizeof(r.user), "%s", optarg);
	    break;
	case 'p':
	    snprintf(r.pass, sizeof(r.pass), "%s", optarg);
	    memset(optarg,'*',strlen(optarg)); /* rm passwd from ps list */
	    break;

        case 'h':
            usage(stdout);
            exit(0);
        default:
            usage(stderr);
            exit(1);
        }
    }

    if (optind < argc)
	host = argv[optind];
    if (optind+1 < argc)
	port = argv[optind+1];
    if (NULL == host) {
	usage(stderr);
	exit(1);
    }

    tty_save();
    if (0 == strlen(r.pass)) {
	tty_noecho();
	fprintf(stderr, "AMT password for host %s: ", host);
	fgets(r.pass, sizeof(r.pass), stdin);
	if (NULL != (h = strchr(r.pass, '\r')))
	    *h = 0;
	if (NULL != (h = strchr(r.pass, '\n')))
	    *h = 0;
	fprintf(stderr, "\n");
    }

    memset(&ai, 0, sizeof(ai));
    ai.ai_socktype = SOCK_STREAM;
    ai.ai_family = PF_UNSPEC;
    tcp_verbose = r.verbose;
    r.sock = tcp_connect(&ai, NULL, NULL, host, port);
    if (-1 == r.sock)
	exit(1);

    tty_raw();
    redir_start(&r);
    redir_loop(&r);
    tty_restore();
    
    exit(0);
}
