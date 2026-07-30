#ifndef __ARQSIM_HEADER_VMEM_H__
#define __ARQSIM_HEADER_VMEM_H__

#include <cstdint>
#include "../arch/arch.h"

namespace OS
{
    
    extern Arch::Cpu *g_cpu;

    extern uint16_t next_free_physical_frame;
    extern Arch::Cpu::PageTable g_kernel_page_table;

    uint16_t vaddr_to_paddr(uint16_t vaddr);
    void configure_hardware_page(uint16_t vpage, uint16_t phy_frame, bool present, bool readable, bool writeable, bool executable);
}

#endif