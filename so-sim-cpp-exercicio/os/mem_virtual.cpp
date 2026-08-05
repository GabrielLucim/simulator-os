#include "mem_virtual.h"
#include "process_manager.h"
#include "os.h"
#include "../config.h"

namespace OS
{
    uint16_t next_free_physical_frame = 2;
    Arch::Cpu *g_cpu = nullptr;

    uint16_t vaddr_to_paddr(uint16_t vaddr)
    {
        if (g_cpu->get_vmem_mode() == VmemMode::Disabled)
        {
            return vaddr;
        }

        uint16_t vpage = vaddr >> 4;
        uint16_t offset = vaddr & 0x000F;

        auto &entry = g_cpu->get_page_table()->at(vpage);
        uint16_t phy_frame = entry.get(Arch::Cpu::PteField::PhyFrameID);

        return (phy_frame << 4) | offset;
    }

    void configure_hardware_page(uint16_t vpage, uint16_t phy_frame, bool present, bool readable, bool writeable, bool executable)
    {
        if (g_cpu == nullptr || g_cpu->get_page_table() == nullptr) return;

        auto &entry = g_cpu->get_page_table()->at(vpage);
        entry.reset();
        
        entry.set(Arch::Cpu::PteField::PhyFrameID, phy_frame);
        entry.set(Arch::Cpu::PteField::Present, present ? 1 : 0);
        entry.set(Arch::Cpu::PteField::Readable, readable ? 1 : 0);
        entry.set(Arch::Cpu::PteField::Writable, writeable ? 1 : 0);
        entry.set(Arch::Cpu::PteField::Executable, executable ? 1 : 0);
    }
}