#include "mem_virtual.h"
#include "process_manager.h"
#include "os.h"
#include "../config.h"

namespace OS
{
    Arch::Cpu *g_cpu = nullptr;

    // Número total de frames físicos reais na memória física (32768 / 16 = 2048 frames)
    constexpr size_t TOTAL_PHYSICAL_FRAMES = Config::phys_mem_size_words / Config::page_size;

    // Vetor para controle de ocupação dos frames da RAM (evita vazamento de memória)
    static bool physical_frames_used[TOTAL_PHYSICAL_FRAMES] = {false};

    int allocate_physical_frame()
    {
        // Pula os primeiros frames reservados do sistema (0 e 1)
        for (size_t i = 2; i < TOTAL_PHYSICAL_FRAMES; i++)
        {
            if (!physical_frames_used[i])
            {
                physical_frames_used[i] = true;
                return static_cast<int>(i);
            }
        }
        return -1; // Memória física cheia
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

        // Verificação se a página virtual está dentro dos limites da tabela
        if (vpage >= Config::ptes_per_table)
        {
            return 0xFFFF; // Endereço inválido
        }

        auto &entry = g_cpu->get_page_table()->at(vpage);

        // Verificação do Present Bit
        if (entry.get(Arch::Cpu::PteField::Present) == 0)
        {
            return 0xFFFF; // Página não mapeada/inválida
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