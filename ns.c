/* ns - example of very simple getaddrinfo()
 *
 * Copyright (C) 2021  Joachim Wiberg <troglobit@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <poll.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/nameser.h>

#include <net/if.h>
#include <netinet/in.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#define info(fmt, args...) printf(fmt "\n", ##args); fflush(stdout)

#define SOCKET_TIMEOUT 10000
#define SERVICE_PORT   80


static int soerror(int sd)
{
	int code = 0;
	socklen_t len = sizeof(code);

	if (getsockopt(sd, SOL_SOCKET, SO_ERROR, &code, &len))
		return 1;

	return errno = code;
}

/*
 * In the wonderful world of network programming the manual states that
 * EINPROGRESS is only a possible error on non-blocking sockets.  Real world
 * experience, however, suggests otherwise.  Simply poll() for completion and
 * then continue. --Joachim
 */
static int check_error(int sd, int msec)
{
	struct pollfd pfd = { sd, POLLOUT, 0 };

	if (EINPROGRESS == errno) {
		info("Waiting (%d sec) for three-way handshake to complete ...", msec / 1000);
		if (poll (&pfd, 1, msec) > 0 && !soerror(sd)) {
			info("Connected.");
			return 0;
		}
	}

	return 1;
}

/* timeout in msec */
static void set_timeouts(int sd, int timeout)
{
	struct timeval sv;

	memset(&sv, 0, sizeof(sv));
	sv.tv_sec  =  timeout / 1000;
	sv.tv_usec = (timeout % 1000) * 1000;
	if (-1 == setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &sv, sizeof(sv)))
		warn("Failed setting receive timeout socket option");
	if (-1 == setsockopt(sd, SOL_SOCKET, SO_SNDTIMEO, &sv, sizeof(sv)))
		warn("Failed setting send timeout socket option");
}

static int usage(int rc)
{
	printf("Usage: ns [-ch?] [FQDN] [PORT]\n"
	       "\n"
	       "Options:\n"
		"  -c     Attempt to connect\n"
		"  -h,-?  This help text\n"
		"\n");
	return rc;
}

int main(int argc, char *argv[])
{
	struct addrinfo hints, *servinfo, *ai;
	char host[NI_MAXHOST];
	char buf[10];
	char *port = buf;
	struct sockaddr *sa;
	int try_connect = 0;
	socklen_t len;
	int tries = 0;
	int rc = 0;
	char *arg;
	int c, sd;

	while ((c = getopt(argc, argv, "ch?")) != EOF) {
		switch (c) {
		case 'c':
			try_connect = 1;
			break;

		case 'h': case '?':
			return usage(0);

		default:
			return usage(1);
		}
	}

	if (optind >= argc)
		return usage(1);

	arg = argv[optind++];
	if (optind < argc)
		port = argv[optind++];
	else
		snprintf(buf, sizeof(buf), "%d", SERVICE_PORT);

	/* Clear DNS cache before calling getaddrinfo(). */
	res_init();

	/* Obtain address(es) matching host/port */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;		/* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM;	/* Stream socket */
	hints.ai_flags = AI_NUMERICSERV;	/* No service name lookup */

	rc = getaddrinfo(arg, port, &hints, &servinfo);
	if (rc != 0 || !servinfo) {
		warnx("Failed resolving hostname %s: %s", arg, gai_strerror(rc));
		return 1;
	}
	ai = servinfo;

	while (1) {
		sd = socket(ai->ai_family, SOCK_STREAM, 0);
		if (sd == -1)
			err(1, "Error creating client socket");

		/* Now we try connecting to the server, on connect fail, try next DNS record */
		sa  = ai->ai_addr;
		len = ai->ai_addrlen;

		/* Reverse lookup just to double-check */
		if (getnameinfo(sa, len, host, sizeof(host), NULL, 0, NI_NUMERICHOST))
			goto next;

		info("Found %s on address %s:%s", arg, host, port);
		if (!try_connect)
			break;

		set_timeouts(sd, SOCKET_TIMEOUT);
		if (connect(sd, sa, len) && check_error(sd, SOCKET_TIMEOUT)) {
		next:
			tries++;

			ai = ai->ai_next;
			if (ai) {
				if (errno == EINPROGRESS)
					warnx("Failed connecting to %s, retrying ...", host);
				else
					warn("Failed connecting to %s", host);
				close(sd);
				continue;
			}

			warn("Failed connecting to %s", arg);
			rc = 1;
		}

		break;
	}

	close(sd);
	freeaddrinfo(servinfo);

	return rc;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
