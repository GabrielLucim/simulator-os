#ifndef VMEM_H
#define VMEM_H

#include <cstdint>
#include "../arch/arch.h"
#include "process.h"

namespace OS
{
    extern Arch::Cpu *g_cpu;

    void init_vmem();
    int allocate_physical_frame();
    void free_physical_frame(uint16_t frame);
    int32_t vaddr_to_paddr(uint16_t vaddr);
    void configure_hardware_page(uint16_t vpage, uint16_t phy_frame, bool present, bool readable, bool writeable, bool executable);
    bool free_virtual_page(Process *proc, uint16_t vaddr);
    bool handle_demand_paging(Process *proc, uint16_t vaddr);
}

#endif