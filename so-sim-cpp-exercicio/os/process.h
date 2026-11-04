#ifndef __ARQSIM_HEADER_PROCESS_H__
#define __ARQSIM_HEADER_PROCESS_H__

#include <cstdint>
#include <vector>
#include "../config.h"
#include "../arch/arch.h"

namespace OS
{
    enum class ProcessState
    {
        Ready,
        Running,
        Blocked,
        Terminated
    };

    struct AllocatedBlock
    {
        uint16_t start_vaddr;   // Endereço virtual inicial do bloco alocado
        uint16_t num_pages;     // Quantidade de páginas que este bloco utiliza
    };

    struct Process
    {
        uint16_t id;                  // PID
        char name[64];                // Nome do binário
        uint16_t pointControl;        // PC
        ProcessState state;           // Estado do processo

        // Registradores para contexto
        uint16_t gprs[Config::nregs];

        // Tabela de páginas
        Arch::Cpu::PageTable page_table;

        uint16_t num_pages;           // Quantidade total de páginas alocadas
        uint32_t sleep_ticks;         // Contagem de ticks restantes para o sleep
        uint16_t quantum_ticks;       // Contagem do quantum atual do processo

        // Rastreamento de blocos alocados dinamicamente (para evitar vazamento de memória)
        std::vector<AllocatedBlock> allocations;
    };
}

#endif