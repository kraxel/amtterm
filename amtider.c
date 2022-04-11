/*
 *  amtider -- Intel AMT IDE-redirection client, console version.
 *
 *  Copyright (C) 2022 Hannes Reinecke <hare@suse.de>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>

#include "redir.h"

#define APPNAME "amtider"
#define BUFSIZE 512

/* ------------------------------------------------------------------ */

static int recv_ider(void *cb_data, unsigned char *buf, int len)
{
    struct redir *r = cb_data;

    return write(STDOUT_FILENO, buf, len);
}

static void state_ider(void *cb_data, enum redir_state old,
		       enum redir_state new)
{
    struct redir *r = cb_data;

    if (r->verbose)
	fprintf(stderr, APPNAME ": %s -> %s (%s)\n",
		redir_state_name(old), redir_state_name(new),
		redir_state_desc(new));
    switch (new) {
    case REDIR_RUN_IDER:
	if (r->verbose)
	    fprintf(stderr,
		    "IDE redirection ok\n"
		    "connected now, use ^] to escape\n");
	break;
    case REDIR_ERROR:
	fprintf(stderr, APPNAME ": ERROR: %s\n", r->err);
	break;
    default:
	break;
    }
}

static int redir_loop(struct redir *r)
{
    struct timeval tv;
    fd_set set;
    sigset_t mask;
    int max_fd = r->sock, sfd;
    int interval = HEARTBEAT_INTERVAL * 4 / 1000;

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
	perror("sigprocmask");
	exit(1);
    }
    sfd = signalfd(-1, &mask, 0);
    if (sfd < 0) {
	perror("signalfd");
	exit(1);
    }


    for(;;) {
	if (r->state == REDIR_CLOSED ||
	    r->state == REDIR_ERROR)
	    break;

	FD_ZERO(&set);
	FD_SET(r->sock, &set);
	if (sfd > 0) {
	    FD_SET(sfd, &set);
	    max_fd = sfd > r->sock? sfd : r->sock;
	}
	tv.tv_sec  = interval;
	tv.tv_usec = 0;
	switch (select(max_fd+1,&set,NULL,NULL,&tv)) {
	case -1:
	    perror("select");
	    return -1;
	case 0:
	    fprintf(stderr,"select: timeout\n");
	    return -1;
	}

	if (FD_ISSET(r->sock,&set)) {
	    if (-1 == redir_data(r))
		return -1;
	}
	if (FD_ISSET(sfd, &set)) {
	    close(sfd);
	    sfd = -1;
	    if (-1 == redir_ider_stop(r))
		return -1;
	    interval = 2;
	    fprintf(stderr, "Wait %d seconds for reply\n", interval);
	}
    }
    return 0;
}

/* ------------------------------------------------------------------ */

struct termios  saved_attributes;
int             saved_fl;

static void tty_save(void)
{
    fcntl(STDIN_FILENO,F_GETFL,&saved_fl);
    tcgetattr (STDIN_FILENO, &saved_attributes);
}

static void tty_noecho(void)
{
    struct termios tattr;

    memcpy(&tattr,&saved_attributes,sizeof(struct termios));
    tattr.c_lflag &= ~(ECHO);
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}

static void tty_restore(void)
{
    fcntl(STDIN_FILENO,F_SETFL,saved_fl);
    tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
}

/* ------------------------------------------------------------------ */

static void usage(FILE *fp)
{
    fprintf(fp,
	    "\n"
	    "This is " APPNAME ", release " VERSION ", it'll establish\n"
	    "ide-redirection (ider) connections to your Intel AMT boxes.\n"
	    "\n"
	    "usage: " APPNAME " [options] host [port]\n"
	    "options:\n"
	    "   -h            print this text\n"
	    "   -v            verbose (default)\n"
	    "   -q            quiet\n"
	    "   -f file       file to use as device data\n"
	    "   -L            use legacy authentication\n"
#if defined(USE_OPENSSL) || defined(USE_GNUTLS)
	    "   -C cacert     enable SSL and use PEM cacert file\n"
#endif
	    "   -u user       username (default: admin)\n"
	    "   -p pass       password (default: $AMT_PASSWORD)\n"
	    "\n"
#if defined(USE_OPENSSL) || defined(USE_GNUTLS)
	    "By default port 16994 (SSL: 16995) is used.\n"
#else
	    "By default port 16994 is used.\n"
#endif
	    "If no password is given " APPNAME " will ask for one.\n");
}

int main(int argc, char *argv[])
{
    struct redir r;
    char *h;
    int c;

    memset(&r, 0, sizeof(r));
    r.verbose = 1;
    memcpy(r.type, "IDER", 4);
    strcpy(r.user, "admin");

    r.cb_data  = &r;
    r.cb_recv  = recv_ider;
    r.cb_state = state_ider;
    r.enable_options = 0x18;

    if (NULL != (h = getenv("AMT_PASSWORD")))
	snprintf(r.pass, sizeof(r.pass), "%s", h);

    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hvqu:p:LC:")))
	    break;
	switch (c) {
	case 'v':
	    r.trace = 1;
	    break;
	case 'q':
	    r.verbose = 0;
	    break;
	case 'f':
	    snprintf(r.filename, sizeof(r.filename), "%s", optarg);
	    break;
	case 'u':
	    snprintf(r.user, sizeof(r.user), "%s", optarg);
	    break;
	case 'p':
	    snprintf(r.pass, sizeof(r.pass), "%s", optarg);
	    memset(optarg,'*',strlen(optarg)); /* rm passwd from ps list */
	    break;
	case 'L':
	    r.legacy = 1;
	    break;
#if defined(USE_OPENSSL) || defined(USE_GNUTLS)
	case 'C':
	    r.cacert = optarg;
	    break;
#endif

	case 'h':
	    usage(stdout);
	    exit(0);
	default:
	    usage(stderr);
	    exit(1);
	}
    }

    if (optind < argc)
	snprintf(r.host, sizeof(r.host), "%s", argv[optind]);
    if (optind+1 < argc)
	snprintf(r.port, sizeof(r.port), "%s", argv[optind+1]);
    if (0 == strlen(r.host)) {
	usage(stderr);
	exit(1);
    }

    if (0 == strlen(r.pass)) {
	tty_save();
	tty_noecho();
	fprintf(stderr, "AMT password for host %s: ", r.host);
	fgets(r.pass, sizeof(r.pass), stdin);
	fprintf(stderr, "\n");
	if (NULL != (h = strchr(r.pass, '\r')))
	    *h = 0;
	if (NULL != (h = strchr(r.pass, '\n')))
	    *h = 0;
	tty_restore();
    }

    if (-1 == redir_connect(&r)) {
	exit(1);
    }

    redir_start(&r);
    redir_loop(&r);

    exit(0);
}
