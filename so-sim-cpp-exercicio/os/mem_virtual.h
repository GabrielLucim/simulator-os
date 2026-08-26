#ifndef __ARQSIM_HEADER_MEM_VIRTUAL_H__
#define __ARQSIM_HEADER_MEM_VIRTUAL_H__

#include <cstdint>
#include "../arch/arch.h"

namespace OS
{
    extern Arch::Cpu *g_cpu;

    int allocate_physical_frame();
    void free_physical_frame(uint16_t frame);

    uint16_t vaddr_to_paddr(uint16_t vaddr);
    void configure_hardware_page(uint16_t vpage, uint16_t phy_frame, bool present, bool readable, bool writeable, bool executable);
}

#endif