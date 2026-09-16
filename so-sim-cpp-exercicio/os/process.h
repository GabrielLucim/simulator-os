#ifndef __ARQSIM_HEADER_PROCESS_H__
#define __ARQSIM_HEADER_PROCESS_H__

#include <cstdint>
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

    struct Process
    {
        uint16_t id;                  // PID
        char name[64];                // Nome do binario
        uint16_t pointControl;        // PC
        ProcessState state;           // Estado do processo

        // Registradores para contexto
        uint16_t gprs[Config::nregs];

        // Tabela de paginas
        Arch::Cpu::PageTable page_table;

        uint16_t num_pages;           // Quantidade de paginas alocadas
        uint32_t sleep_ticks;         // Para syscall de dormir futuro
    };
}

#endif