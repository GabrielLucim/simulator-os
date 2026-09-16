#ifndef __ARQSIM_HEADER_PROCESS_H__
#define __ARQSIM_HEADER_PROCESS_H__

#include <cstdint>
#include "../config.h"
#include "../arch/arch.h"

namespace OS
{
    struct Process
    {
        uint16_t id;
        char name[64];
        uint16_t pointControl;
        bool active;

        uint16_t gprs[Config::nregs];

        Arch::Cpu::PageTable page_table;

        uint16_t num_pages;  
    };
}

#endif