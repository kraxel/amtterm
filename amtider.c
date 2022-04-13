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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/mman.h>

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
	    "   -c            use CD-ROM emulation\n"
	    "   -g            start redirection gracefully\n"
	    "   -r            start redirection on reboot\n"
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
    int c, fd;
    struct stat st;

    memset(&r, 0, sizeof(r));
    r.verbose = 1;
    memcpy(r.type, "IDER", 4);
    strcpy(r.user, "admin");

    r.cb_data  = &r;
    r.cb_recv  = recv_ider;
    r.cb_state = state_ider;
    r.device = 0xa0;
    r.enable_options = IDER_START_NOW;

    if (NULL != (h = getenv("AMT_PASSWORD")))
	snprintf(r.pass, sizeof(r.pass), "%s", h);

    for (;;) {
	if (-1 == (c = getopt(argc, argv, "df:ghvqu:p:LC:")))
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
	case 'c':
	    r.device = 0xb0;
	    break;
	case 'g':
	    r.enable_options = IDER_START_GRACEFUL;
	    break;
	case 'r':
	    r.enable_options = IDER_START_ONREBOOT;
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

    if (optind < argc) {
	snprintf(r.host, sizeof(r.host), "%s", argv[optind]);
	optind++;
    }
    if (optind < argc) {
	snprintf(r.port, sizeof(r.port), "%s", argv[optind]);
	optind++;
    }
    if (0 == strlen(r.host)) {
	usage(stderr);
	exit(1);
    }

    if (0 == strlen(r.filename)) {
	usage(stderr);
	exit(1);
    }

    if (stat(r.filename, &st) < 0) {
	perror("stat");
	exit(1);
    }
    r.mmap_size = st.st_size;
    fd = open(r.filename, O_RDONLY);
    if (fd < 0) {
	perror("open");
	exit(1);
    }
    r.mmap_buf = mmap(NULL, r.mmap_size, PROT_READ,
		      MAP_PRIVATE, fd, 0);
    close(fd);
    if (r.mmap_buf == MAP_FAILED) {
	perror("mmap");
	exit(1);
    }
    if (r.device == 0xa0) {
	r.lba_size = 512;
	r.lba_shift = 9;
    } else {
	r.lba_size = 2048;
	r.lba_shift = 11;
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
    munmap(r.mmap_buf, r.mmap_size);

    exit(0);
}
