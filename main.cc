/*
 * Copyright (C) 2010-2016 Sebastian Krahmer.
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

#include <syslog.h>
#include <unistd.h>
#include <stdint.h>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <signal.h>
#include <fcntl.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <pwd.h>
#include <grp.h>
#include <sys/time.h>
#include <sys/resource.h>

#ifdef USE_CAPS
#include <sys/prctl.h>
#include <sys/capability.h>
#endif

#include "sshttp.h"
#include "multicore.h"


using namespace std;

namespace Config
{
	uint16_t ssh_port = 22, http_port = 8080;
	string local_port = "80", laddr = "0.0.0.0";
	string root = "/var/lib/empty", user = "nobody";
	int cores = -1, master = 1;
	bool v6 = 0;
	bool tproxy = 0;
}


void die(const char *s)
{
	perror(s);
	exit(errno);
}


void close_fds()
{
	struct rlimit rl;

	if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
		die("getrlimit");
	for (unsigned int i = 3; i <= rl.rlim_max; ++i)
		close(i);
	close(0);
	open("/dev/null", O_RDWR);
	dup2(0, 1);
}


int main(int argc, char **argv)
{
	int c;
	int family = AF_INET;

	while ((c = getopt(argc, argv, "S:H:L:R:U:n:6l:iT")) != -1) {
		switch (c) {
		case 'T':
			Config::tproxy = 1;
			break;
		case 'l':
			Config::laddr = optarg;
			break;
		case 'S':
			Config::ssh_port = atoi(optarg);
			break;
		case 'H':
			Config::http_port = atoi(optarg);
			break;
		case 'L':
			Config::local_port = optarg;
			break;
		case 'R':
			Config::root = optarg;
			break;
		case 'U':
			Config::user = optarg;
			break;
		case 'n':
			Config::cores = atoi(optarg);
			break;
		case '6':
			Config::v6 = 1;
			family = AF_INET6;
			if (Config::laddr == "0.0.0.0")
				Config::laddr = "::";
			break;
		default:
			printf("sshttpd [-n CPU cores] [-S ssh port] [-H http port] [-L lport] [-l laddr] [-6] ");
#ifdef USE_CAPS
			printf("[-U user] [-R chroot]");
#endif
			printf("\n");
			exit(1);
		}
	}

	printf("sshttpd: Using HTTP_PORT=%d SSH_PORT=%d and local port=%s. Going background.",
	        Config::http_port, Config::ssh_port, Config::local_port.c_str());
#ifdef USE_CAPS
	printf(" Using caps/chroot.");
#endif
	printf("\n");

	close_fds();

	nice(-20);
	openlog("sshttpd", LOG_NOWAIT|LOG_PID|LOG_NDELAY, LOG_DAEMON);

	sshttp sh;
	if (sh.init(family, Config::laddr, Config::local_port, Config::tproxy) < 0) {
		fprintf(stderr, "%s\n", sh.why());
		exit(errno);
	}

	NS_Misc::init_multicore();
	NS_Misc::setup_multicore(Config::cores);

#ifdef USE_CAPS
	struct passwd *pw = getpwnam(Config::user.c_str());
	if (!pw)
		die("unknown user:getpwnam");

	if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) < 0)
		die("prctl");
#endif
	if (chroot(Config::root.c_str()) < 0)
		die("chroot");

#ifdef USE_CAPS
	if (setgid(pw->pw_gid) < 0)
		die("setgid");
	if (initgroups(Config::user.c_str(), pw->pw_gid) < 0)
		die("initgroups");
	if (setuid(pw->pw_uid) < 0)
		die("setuid");

	// CAP_NET_BIND_SERVICE because we bind to original
	// source/port which might be privileged
	cap_t my_caps;
	cap_value_t cv[2] = {CAP_NET_ADMIN, CAP_NET_BIND_SERVICE};

	if ((my_caps = cap_init()) == NULL)
		die("cap_init");
	if (cap_set_flag(my_caps, CAP_EFFECTIVE, 2, cv, CAP_SET) < 0)
		die("cap_set_flag");
	if (cap_set_flag(my_caps, CAP_PERMITTED, 2, cv, CAP_SET) < 0)
		die("cap_set_flag");
	if (cap_set_proc(my_caps) < 0)
		die("cap_set_proc");
	cap_free(my_caps);
#endif

	if (chdir("/") < 0)
		die("chdir");

	if (Config::master) {
		if (fork() > 0)
			exit(0);
		setsid();
	}

	dup2(0, 2);

	sh.ssh_port(Config::ssh_port);
	sh.http_port(Config::http_port);

	syslog(LOG_ERR, "sshttpd started, ready to rock");
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGINT, &sa, NULL) < 0 ||
	    sigaction(SIGPIPE, &sa, NULL) < 0 ||
	    sigaction(SIGURG, &sa, NULL) < 0 ||
	    sigaction(SIGHUP, &sa, NULL) < 0)
		syslog(LOG_ERR, "Nuts?! Failed to set signal handlers.");

	for (;;) {
		if (sh.loop() < 0)
			syslog(LOG_ERR, "%s", sh.why());
	}
	return 0;
}

