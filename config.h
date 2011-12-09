#ifndef __config_h__
#define __config_h__

#include <stdint.h>

namespace Config
{
	extern uint16_t ssh_port, http_port, local_port;
	extern std::string root, user;
	extern int cores, master;
}

#endif

