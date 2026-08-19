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

        // Tabela de páginas por processo
        Arch::Cpu::PageTable page_table;

        // Controle de páginas virtuais alocadas via syscall
        bool allocated_vpages[Config::ptes_per_table];

        // O QUE FALTAVA:
        uint16_t num_pages;                        // Quantidade de páginas que o processo ocupa
        uint16_t allocated_frames[Config::ptes_per_table]; // Mapeamento de quais frames físicos pertencem a ele
    };
}

#endif