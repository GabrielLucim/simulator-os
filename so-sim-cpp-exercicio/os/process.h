#ifndef __ARQSIM_HEADER_PROCESS_H__
#define __ARQSIM_HEADER_PROCESS_H__

#include <cstdint>
#include "../config.h"

namespace OS
{
	struct Process
	{
		uint16_t id;
		uint16_t registrars[Config::nregs];
		uint16_t pointControl;
		bool active;
		char name[64];
		bool allocated_vpages[Config::ptes_per_table];
	};
}

#endif