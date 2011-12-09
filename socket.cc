/*
 * Copyright (C) 2001-2010 Sebastian Krahmer.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Sebastian Krahmer.
 * 4. The name Sebastian Krahmer may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <string>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include "socket.h"

namespace NS_Socket {

#ifndef IP_TRANSPARENT
const int IP_TRANSPARENT = 19;
#endif

using namespace std;

string error;

const char *why()
{
	return error.c_str();
}

int nodelay(int sock)
{
	int one = 1;
	socklen_t len = sizeof(one);

	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, len) < 0) {
		error = "NS_Socket::nodelay::setsockopt: ";
		error += strerror(errno);
		return -1;
	}

	return 0;
}

int reuse(int sock)
{
	int one = 1;
	socklen_t len = sizeof(one);

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, len) < 0) {
		error = "NS_Socket::reuse::setsockopt: ";
		error += strerror(errno);
		return -1;
	}

	return 0;
}

#ifdef FREEBSD
#define LINUX22
#endif

int dstaddr(int sock, sockaddr_in *dst)
{
	if (!dst) {
		error = "NS_Socket::dstaddr: dst == NULL";
		return -1;
	}

#ifdef LINUX22
	socklen_t size = sizeof(sockaddr_in);
	if (getsockname(sock, (struct sockaddr*)dst, &size) < 0) {
		error = "NS_Socket::dstaddr::getsockname: ";
		error += strerror(errno);
		return -1;
	}
#elif defined(LINUX24) || defined(LINUX26)
#include <linux/netfilter_ipv4.h>
	socklen_t size = sizeof(sockaddr_in);
	if (getsockopt(sock, SOL_IP, SO_ORIGINAL_DST, dst, &size) < 0) {
		error = "NS_Socket::dstaddr::getsockopt: ";
		error += strerror(errno);
		return -1;
	}
#else
#error "Not supported on this OS yet."
#endif
	return 0;
}


int bind_local(int sock, const struct sockaddr_in &sin, bool do_listen)
{
	if (reuse(sock) < 0)
		return -1;

	if (bind(sock, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
		error = "NS_Socket::bind_local::bind:";
		error += strerror(errno);
		return -1;
	}

	if (do_listen) {
		if (listen(sock, SOMAXCONN) < 0) {
			error = "NS_Socket::bind_local::listen:";
			error += strerror(errno);
			return -1;
		}
	}
	return 0;
}


int tcp_connect_nb(const struct sockaddr_in &to, const struct sockaddr_in &from, bool transparent)
{
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		error = "NS_Socket::tcp_connect_nb::socket:";
		error += strerror(errno);
		return -1;
	}

	int f;
	if ((f = fcntl(sock, F_GETFL, 0)) < 0) {
		error = "NS_Socket::tcp_connect_nb::fcntl:";
		error += strerror(errno);
		return -1;
	}
	if (fcntl(sock, F_SETFL, f|O_NONBLOCK) < 0) {
		error = "NS_Socket::tcp_connect_nb::fcntl:";
		error += strerror(errno);
		return -1;
	}

	if (from.sin_port != 0) {
		int one = 1;
		if (transparent && setsockopt(sock, SOL_IP, IP_TRANSPARENT, &one, sizeof(one)) < 0) {
			error = "NS_Socket::tcp_connect_nb::setsockopt:";
			error += strerror(errno);
			return -1;
		}
		if (bind_local(sock, from, 0) < 0)
			return -1;
	}

	if (connect(sock, (struct sockaddr *)&to, sizeof(to)) < 0 &&
	    errno != EINPROGRESS) {
		error = "NS_Socket::tcp_connect_nb::fcntl:";
		error += strerror(errno);
		return -1;
	}

	return sock;
}

int finish_connecting(int fd)
{
	int e = 0;
	socklen_t len = sizeof(error);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &len) < 0 || e < 0) {
		error = "NS_Socket::finish_connecting::getsockopt:";
		error += strerror(errno);
		return -1;
	}

	return nodelay(fd);
}


int readn(int fd, void *buf, size_t len)
{
	int o = 0, n;
	char *ptr = (char*)buf;

	while (len > 0) {
		if ((n = read(fd, ptr+o, len)) <= 0)
			return n;
		len -= n;
		o += n;
	}
	return o;
}


int writen(int fd, const void *buf, size_t len)
{
	int o = 0, n;
	char *ptr = (char*)buf;

	while (len > 0) {
		if ((n = write(fd, ptr+o, len)) <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return o;
			return n;
		}
		len -= n;
		o += n;
	}
	return o;
}


} // namespace

