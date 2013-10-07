#ifndef __config_h__
#define __config_h__

#include <stdint.h>

namespace Config
{
	extern uint16_t ssh_port, http_port;
	extern std::string laddr, local_port;
	extern std::string root, user;
	extern int cores, master;
	extern bool v6;
}

#endif

