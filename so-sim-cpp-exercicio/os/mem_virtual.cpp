#include "mem_virtual.h"
#include "process_manager.h"
#include "os.h"
#include "../config.h"

namespace OS
{
    Arch::Cpu *g_cpu = nullptr;

    constexpr size_t TOTAL_PHYSICAL_FRAMES = Config::phys_mem_size_words / Config::page_size;

    static bool physical_frames_used[TOTAL_PHYSICAL_FRAMES] = {false};

    int allocate_physical_frame()
    {
        for (size_t i = 2; i < TOTAL_PHYSICAL_FRAMES; i++)
        {
            if (!physical_frames_used[i])
            {
                physical_frames_used[i] = true;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void free_physical_frame(uint16_t frame)
    {
        if (frame < TOTAL_PHYSICAL_FRAMES)
        {
            physical_frames_used[frame] = false;
        }
    }

    uint16_t vaddr_to_paddr(uint16_t vaddr)
    {
        if (g_cpu->get_vmem_mode() == VmemMode::Disabled)
        {
            return vaddr;
        }

        uint16_t vpage = vaddr >> Config::page_size_bits;
        uint16_t offset = vaddr & (Config::page_size - 1);

        if (vpage >= Config::ptes_per_table)
        {
            return 0xFFFF;
        }

        auto &entry = g_cpu->get_page_table()->at(vpage);

        if (entry.get(Arch::Cpu::PteField::Present) == 0)
        {
            return 0xFFFF;
        }

        uint16_t phy_frame = entry.get(Arch::Cpu::PteField::PhyFrameID);

        return (phy_frame << Config::page_size_bits) | offset;
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