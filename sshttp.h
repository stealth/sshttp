/*
 * Copyright (C) 2010 Sebastian Krahmer.
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

#ifndef __sshttp_h__
#define __sshttp_h__

#include <stdio.h>
#include <poll.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string>
#include <cstring>
#include <map>
#include <time.h>
#include <stdint.h>


class sshttp {
private:
	struct pollfd *pfds;
	int first_fd, max_fd;
	uint16_t d_ssh_port, d_http_port;

	std::string err;

	std::map<int, struct status *> fd2state;

	void cleanup(int);

	void calc_max_fd();

	uint16_t find_port(int);

public:
	sshttp() : d_ssh_port(22), d_http_port(8080) {};

	~sshttp() {};

	void ssh_port(uint16_t p) { d_ssh_port = p; };

	void http_port(uint16_t p) { d_http_port = p; };

	int init(uint16_t);

	int loop();

	const char *why();
};

typedef enum {
	STATE_CONNECTING = 0,
	STATE_ACCEPTING,
	STATE_DECIDING,
	STATE_CONNECTED,
	STATE_NONE
} status_t;


struct status {
	int fd, peer_fd;
	status_t state;
	time_t last_t;
	char buf[1024];
	uint16_t blen;

	status() { memset(buf, 0, sizeof(buf)); blen = 0; };
};


#endif

