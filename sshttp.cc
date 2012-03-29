/*
 * Copyright (C) 2010-2011 Sebastian Krahmer.
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

#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <string>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdint.h>
#include "sshttp.h"
#include "socket.h"

using namespace std;
using namespace NS_Socket;

const char *sshttp::why()
{
	return err.c_str();
}


int sshttp::init(uint16_t local_port)
{
	int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		err = "sshttp::init::socket:";
		err = strerror(errno);
		return -1;
	}

	// bind & listen
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(local_port);
	if (bind_local(sock_fd, sin, 1) < 0) {
		err = NS_Socket::why();
		return -1;
	}

	// allocate poll array
	struct rlimit rl;
	rl.rlim_cur = (1<<16);
	rl.rlim_max = (1<<16);

	if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
		err = "sshttp::init::setrlimit:";
		err = strerror(errno);
		return -1;
	}

	int flags = fcntl(sock_fd, F_GETFL);
	fcntl(sock_fd, F_SETFL, flags|O_NONBLOCK);

	pfds = new struct pollfd[rl.rlim_cur];
	memset(pfds, 0, sizeof(struct pollfd) * rl.rlim_cur);

	for (unsigned int i = 0; i < rl.rlim_cur; ++i)
                pfds[i].fd = -1;

	// setup listening socket for polling
	max_fd = sock_fd;
	first_fd = sock_fd;
	pfds[sock_fd].fd = sock_fd;
	pfds[sock_fd].events = POLLIN|POLLOUT;
	fd2state[sock_fd] = new struct status;
	fd2state[sock_fd]->fd = sock_fd;
	fd2state[sock_fd]->state = STATE_ACCEPTING;

	return 0;
}


void sshttp::cleanup(int fd)
{
	if (fd < 0)
		return;

	pfds[fd].fd = -1;
	pfds[fd].events = pfds[fd].revents = 0;
	close(fd);

	map<int, struct status *>::iterator i = fd2state.find(fd);
	if (i != fd2state.end() && i->second) {
		i->second->state = STATE_NONE;
		i->second->fd = -1;
		i->second->peer_fd = -1;
		i->second->blen = 0;
	}
	if (max_fd == fd)
		--max_fd;

	map<int, time_t>::iterator it = shutdown_fds.find(fd);
	if (it != shutdown_fds.end())
		shutdown_fds.erase(it);
}


// After a connection has gone through this shtdown(), it still needs to
// be cleanup()'ed (where handle is actually closed)
void sshttp::shutdown(int fd)
{
	if (fd < 0)
		return;
	if (fd2state.count(fd) == 0 || !fd2state[fd])
		return;

	if (fd2state[fd]->state == STATE_CLOSING || fd2state[fd]->state == STATE_NONE)
		return;

	::shutdown(fd, SHUT_RDWR);
	shutdown_fds[fd] = now;

	fd2state[fd]->state = STATE_CLOSING;
	fd2state[fd]->blen = 0;

	pfds[fd].fd = -1;
	pfds[fd].events = pfds[fd].revents = 0;

	if (max_fd == fd)
		--max_fd;
}


void sshttp::calc_max_fd()
{
	for (int i = max_fd; i >= first_fd; --i) {
		if (pfds[i].fd != -1) {
			max_fd = i;
			return;
		}
	}
}


int sshttp::loop()
{
	int i = 0, n = 0, wn = 0, afd = -1, peer_fd = -1;
	sockaddr_in sin, dst;
	socklen_t slen = sizeof(sin);

	for (;;) {
		// Need to have a quite small timeout, since STATE_DECIDING may change without
		// data arrival, e.g. without a poll() trigger.
		if (poll(pfds, max_fd + 1, 1000) < 0)
			continue;

		now = time(NULL);

		// assert: pfds[i].fd == i
		for (i = first_fd; i <= max_fd; ++i) {
			if (pfds[i].fd == -1)
				continue;

			if (fd2state.count(i) == 0 || !fd2state[i])
				continue;

			// timeout hanging connections (with pending data) but not accepting socket
			if (now - fd2state[i]->last_t >=  TIMEOUT_ALIVE &&
			    fd2state[i]->state != STATE_ACCEPTING &&
			    fd2state[i]->blen > 0) {
				// always cleanup()/shutdown() in pairs! Otherwise re-used fd numbers
				// make problems
				cleanup(fd2state[i]->peer_fd);
				cleanup(i);
				continue;
			}

			if ((pfds[i].revents & (POLLERR|POLLHUP|POLLNVAL)) != 0) {

				// flush buffer to peer if there is pending data
				if (fd2state[i]->blen > 0 && fd2state[i]->state == STATE_CONNECTED) {
					writen(fd2state[i]->peer_fd, fd2state[i]->buf, fd2state[i]->blen);
					fd2state[i]->blen = 0;
				}

				// hangup/error for i, but let kernel flush internal send buffers
				// for peer.
				shutdown(fd2state[i]->peer_fd);
				cleanup(i);
				continue;
			}

			if (pfds[i].revents == 0 && fd2state[i]->state != STATE_DECIDING)
				continue;

			// new connection ready to accept?
			if (fd2state[i]->state == STATE_ACCEPTING) {
				pfds[i].revents = 0;
				for (;;) {
					heavy_load = 0;
#ifdef LINUX26
					afd = accept4(i, (struct sockaddr *)&sin, &slen, SOCK_NONBLOCK);
#else
					afd = accept(i, (struct sockaddr *)&sin, &slen);
#endif
					if (afd < 0) {
						if (errno == EMFILE || errno == ENFILE)
							heavy_load = 1;
						break;
					}
					nodelay(afd);
					pfds[afd].fd = afd;
					pfds[afd].events = POLLIN;
					pfds[afd].revents = 0;

#ifndef LINUX26
					if (fcntl(afd, F_SETFL, O_RDWR|O_NONBLOCK) < 0) {
						cleanup(afd);
						err = "sshttp::loop::fcntl:";
						err += strerror(errno);
						return -1;
					}
#endif

					if (fd2state.count(afd) == 0) {
						fd2state[afd] = new (nothrow) status;

						if (!fd2state[afd]) {
							err = "OOM";
							close(afd);
							return -1;
						}
					}

					// We dont know yet which protocol is coming
					fd2state[afd]->fd = afd;
					fd2state[afd]->peer_fd = -1;
					fd2state[afd]->state = STATE_DECIDING;
					fd2state[afd]->from = sin;
					fd2state[afd]->last_t = now;

					if (afd > max_fd)
						max_fd = afd;
				}
				continue;

			// First input data from a client. Now we need to decide where we go.
			} else if (fd2state[i]->state == STATE_DECIDING) {
				// allow up to two seconds for clients to send first proto stuff
				if (pfds[i].revents == 0 && now - fd2state[i]->last_t < TIMEOUT_PROTOCOL)
					continue;
				pfds[i].revents = 0;
				if (dstaddr(i, &dst) < 0) {
					err = "sshttp::loop::";
					err += NS_Socket::why();
					cleanup(i);
					return -1;
				}
				dst.sin_port = htons(find_port(i));

				// error?
				if (dst.sin_port == 0) {
					err = "sshttp::loop: Error while detecting protocol.";
					cleanup(i);
					continue;
				}

				peer_fd  = tcp_connect_nb(dst, fd2state[i]->from, 1);
				if (peer_fd < 0) {
					err = "sshttp::loop::";
					err += NS_Socket::why();
					cleanup(i);
					return -1;
				}
				fd2state[i]->peer_fd = peer_fd;
				fd2state[i]->state = STATE_CONNECTED;
				fd2state[i]->last_t = now;

				if (fd2state.count(peer_fd) == 0) {
					fd2state[peer_fd] = new (nothrow) status;
					if (!fd2state[peer_fd]) {
						err = "OOM";
						cleanup(i);
						close(peer_fd);
						return -1;
					}
				}

				fd2state[peer_fd]->fd = peer_fd;
				fd2state[peer_fd]->peer_fd = i;
				fd2state[peer_fd]->state = STATE_CONNECTING;
				fd2state[peer_fd]->last_t = now;

				pfds[peer_fd].fd = peer_fd;
				// POLLIN|POLLOUT b/c we wait for connection to finish
				pfds[peer_fd].events = POLLOUT|POLLIN;
				pfds[peer_fd].revents = 0;

				pfds[i].events = POLLOUT|POLLIN;
				if (peer_fd > max_fd)
					max_fd = peer_fd;
			} else if (fd2state[i]->state == STATE_CONNECTING) {
				if (finish_connecting(i) < 0) {
					err = "sshttp::loop::";
					err += NS_Socket::why();
					cleanup(fd2state[i]->peer_fd);
					cleanup(i);
					return -1;
				}
				fd2state[i]->state = STATE_CONNECTED;
				fd2state[i]->last_t = now;
				pfds[i].fd = i;
				pfds[i].events = POLLIN;
				pfds[i].revents = 0;
			} else if (fd2state[i]->state == STATE_CONNECTED) {
				// peer not ready yet
				if (fd2state.count(fd2state[i]->peer_fd) == 0 ||
				    !fd2state[fd2state[i]->peer_fd] ||
				    fd2state[fd2state[i]->peer_fd]->state == STATE_CONNECTING) {
					pfds[i].revents = 0;
					continue;
				}

				if (pfds[i].revents & POLLOUT) {
					// actually data to send?
					if ((n = fd2state[fd2state[i]->peer_fd]->blen) > 0) {
						wn = writen(i, fd2state[fd2state[i]->peer_fd]->buf, n);

						// error for i, but let kernel flush internal sendbuffer
						// for peer
						if (wn <= 0) {
							shutdown(fd2state[i]->peer_fd);
							cleanup(i);
							continue;
						}
						// non blocking write couldnt write it all at once
						if (wn != n) {
							memmove(fd2state[fd2state[i]->peer_fd]->buf,
							        fd2state[fd2state[i]->peer_fd]->buf + wn,
							         n - wn);
							pfds[i].events = POLLOUT|POLLIN;
						} else {
							pfds[i].events = POLLIN;
						}
						fd2state[fd2state[i]->peer_fd]->blen -= wn;
					}
				}

				if (pfds[i].revents & POLLIN) {
					// still data in buffer? dont read() new data
					if (fd2state[i]->blen > 0) {
						pfds[i].events = POLLIN;
						pfds[fd2state[i]->peer_fd].events = POLLOUT|POLLIN;
						pfds[i].revents = 0;
						continue;
					}
					n = read(i, fd2state[i]->buf, sizeof(fd2state[i]->buf));

					// No need to writen() pending data on read error here, as above blen check
					// ensured no pending data can happen here
					if (n <= 0) {
						shutdown(fd2state[i]->peer_fd);
						cleanup(i);
						continue;
					}
					fd2state[i]->blen = n;
					// peer has data to write
					pfds[fd2state[i]->peer_fd].events = POLLOUT|POLLIN;
					pfds[i].events = POLLIN;
				}

				pfds[i].revents = 0;
				fd2state[i]->last_t = now;
				fd2state[fd2state[i]->peer_fd]->last_t = now;
			}
		}
		calc_max_fd();

		// we need to handle TIMEOUT_CLOSING cases in a different loop, as poll()
		// will always renturn .revents = POLLHUP even if we ask for no events
		for (map<int, time_t>::iterator it = shutdown_fds.begin(); it != shutdown_fds.end();) {
			if (now - it->second > TIMEOUT_CLOSING || heavy_load) {
				int fd = it->first;
				shutdown_fds.erase(it++);
				cleanup(fd);
			} else
				++it;
		}
	}
	return 0;
}


// returns 0 on error
uint16_t sshttp::find_port(int fd)
{
	int r = 0;
	char buf[1024];

	r = recv(fd, buf, sizeof(buf), MSG_PEEK);

	if ((r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || r == 0)
		return 0;
	// No packet? -> SSH
	else if (r < 0)
		return d_ssh_port;

	if (string(buf).find("HTTP") != string::npos)
		return d_http_port;
	if (string(buf).find("SSH-") != string::npos)
		return d_ssh_port;

	// no string match? https! (covered by HTTP_PORT)
	return d_http_port;
}

