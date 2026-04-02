#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

extern int tcp_verbose;

int tcp_connect(struct addrinfo *ai,
		const char *addr, const char *port,
		const char *host, const char *serv);

int tcp_listen(struct addrinfo *ai, const char *addr, const char *port);
