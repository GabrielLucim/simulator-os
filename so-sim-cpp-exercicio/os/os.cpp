#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstdlib>

#include "../config.h"
#include "../lib.h"
#include "../arch/arch.h"
#include "os.h"
#include "os-lib.h"
#include "process.h"
#include "mem_virtual.h"
#include "process_manager.h"

namespace OS
{
    char cmd_buffer[256];
    uint16_t cmd_length = 0;
    Arch::Cpu *g_cpu = nullptr;

    void boot(Arch::Cpu *cpu)
    {
        g_cpu = cpu;
        cmd_length = 0;

        g_cpu->set_page_table(&g_kernel_page_table);

        g_cpu->write_io(IO_Port::TimerInterruptCycles, 1000);

        terminal_println(g_cpu, Terminal::Command, "Type commands here");
        terminal_println(g_cpu, Terminal::App, "Apps output here");
        terminal_println(g_cpu, Terminal::Kernel, "Kernel output here");

        load_idle_program();

        terminal_print_str(g_cpu, Terminal::Command, "> ");
    }

    void interrupt(const InterruptCode interrupt)
    {
        if (interrupt == InterruptCode::Keyboard)
        {
            const uint16_t input = g_cpu->read_io(IO_Port::TerminalReadTypedChar);

            if (terminal_is_return(input))
            {
                g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Command));
                terminal_println(g_cpu, Terminal::Command, "");

                std::string_view command(cmd_buffer, cmd_length);
                if (!command.empty())
                {
                    if (command == "quit" || command == "exit")
                    {
                        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
                        terminal_println(g_cpu, Terminal::Kernel, "Desligando o simulador...");
                        g_cpu->turn_off();
                    }
                    else if (command == "kill")
                    {
                        kill_current_process();
                    }
                    else if (command.starts_with("load "))
                    {
                        load_user_program(command.substr(5));
                    }
                    else
                    {
                        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
                        terminal_print_str(g_cpu, Terminal::Kernel, "Comando invalido: ");
                        terminal_println(g_cpu, Terminal::Kernel, std::string(command).c_str());
                    }
                }

                for (int i = 0; i < 256; i++)
                    cmd_buffer[i] = '\0';
                cmd_length = 0;
                g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Command));
                terminal_print_str(g_cpu, Terminal::Command, "> ");
            }
            else if (terminal_is_backspace(input))
            {
                if (cmd_length > 0)
                {
                    cmd_length--;
                    cmd_buffer[cmd_length] = '\0';
                    g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Command));
                    g_cpu->write_io(IO_Port::TerminalUpload, '\r');
                    terminal_print_str(g_cpu, Terminal::Command, "> ");
                    for (int i = 0; i < cmd_length; i++)
                    {
                        g_cpu->write_io(IO_Port::TerminalUpload, static_cast<uint16_t>(cmd_buffer[i]));
                    }
                }
            }
            else if (cmd_length < 255)
            {
                cmd_buffer[cmd_length++] = static_cast<char>(input);
                g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Command));
                g_cpu->write_io(IO_Port::TerminalUpload, input);
            }
        }
        else if (interrupt == InterruptCode::Timer)
        {
            // Tratador do relógio do sistema
        }
        else if (interrupt == InterruptCode::CpuException)
        {
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
            
            auto exception_info = g_cpu->get_ref_cpu_exception(); 
            uint16_t vaddr_fault = exception_info.vaddr;
            uint16_t vpage = vaddr_fault / Config::page_size;

            if (exception_info.type == Arch::Cpu::CpuException::Type::VmemPageFault && 
                current_process != nullptr && current_process->allocated_vpages[vpage])
            {
                terminal_print_str(g_cpu, Terminal::Kernel, "[Demand Paging] Page Fault atendido! Alocando frame fisico para vpage: ");
                terminal_println(g_cpu, Terminal::Kernel, std::to_string(vpage).c_str());

                configure_hardware_page(vpage, next_free_physical_frame, true, true, true, false);
                next_free_physical_frame++;
            }
            else
            {
                terminal_print_str(g_cpu, Terminal::Kernel, "!!! FALHA CRITICA DE SEGURANCA: ACESSO ILEGAL A MEMORIA (");
                terminal_print_str(g_cpu, Terminal::Kernel, Arch::enum_class_to_str(exception_info.type));
                terminal_println(g_cpu, Terminal::Kernel, ") !!!");
                
                if (current_process != nullptr)
                {
                    terminal_print_str(g_cpu, Terminal::Kernel, "Processo abortado: ");
                    terminal_println(g_cpu, Terminal::Kernel, current_process->name);
                    delete current_process;
                    current_process = nullptr;
                }
                load_idle_program();
            }
        }
    }

    void syscall()
    {
        const uint16_t syscall_num = g_cpu->get_gpr(0);

        switch (syscall_num)
        {
        case 0:
            if (current_process != nullptr)
            {
                g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
                terminal_print_str(g_cpu, Terminal::Kernel, "Processo finalizado: ");
                terminal_println(g_cpu, Terminal::Kernel, current_process->name);
                delete current_process;
                current_process = nullptr;
            }
            load_idle_program();
            break;

        case 1:
        {
            uint16_t vaddr = g_cpu->get_gpr(1);
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::App));
            
            while (true)
            {
                uint16_t paddr = vaddr_to_paddr(vaddr);
                uint16_t ch = g_cpu->pmem_read(paddr);
                if (ch == 0) break;

                const char str[2] = {static_cast<char>(ch), '\0'};
                terminal_print_str(g_cpu, Terminal::App, str);
                vaddr++;
            }
        }
        break;

        case 2:
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::App));
            terminal_print_str(g_cpu, Terminal::App, "\n");
            break;

        case 3:
        {
            uint16_t value = g_cpu->get_gpr(1);
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::App));
            terminal_print_str(g_cpu, Terminal::App, std::to_string(value).c_str());
        }
        break;

        case 4:
        {
            uint16_t words_requested = g_cpu->get_gpr(1);
            if (current_process == nullptr || words_requested == 0)
            {
                g_cpu->set_gpr(1, 0);
                break;
            }

            size_t pages_needed = (words_requested + (Config::page_size - 1)) / Config::page_size;
            int start_page = -1;
            size_t consecutive = 0;

            for (size_t i = 0; i < Config::ptes_per_table; i++)
            {
                if (!current_process->allocated_vpages[i])
                {
                    consecutive++;
                    if (consecutive == pages_needed)
                    {
                        start_page = i - pages_needed + 1;
                        break;
                    }
                }
                else
                {
                    consecutive = 0;
                }
            }

            if (start_page != -1)
            {
                for (size_t p = 0; p < pages_needed; p++)
                {
                    current_process->allocated_vpages[start_page + p] = true;
                    configure_hardware_page(start_page + p, 0, false, true, true, false); 
                }

                g_cpu->set_gpr(1, 1);
                g_cpu->set_gpr(2, start_page * Config::page_size);
            }
            else
            {
                g_cpu->set_gpr(1, 0);
            }
        }
        break;

        case 5:
        {
            uint16_t vaddr_to_free = g_cpu->get_gpr(1);
            uint16_t vpage = vaddr_to_free / Config::page_size;

            if (current_process != nullptr && current_process->allocated_vpages[vpage])
            {
                current_process->allocated_vpages[vpage] = false;
                if (g_cpu->get_page_table() != nullptr)
                {
                    g_cpu->get_page_table()->at(vpage).reset();
                }
                g_cpu->set_gpr(1, 1);
            }
            else
            {
                g_cpu->set_gpr(1, 0);
            }
        }
        break;

        case 6:
        {
            uint16_t sleep_sec = g_cpu->get_gpr(1);
            uint16_t start_sec = g_cpu->read_io(IO_Port::TimerGetTimeSeconds);
            
            while ((g_cpu->read_io(IO_Port::TimerGetTimeSeconds) - start_sec) < sleep_sec)
            {
                // Espera ativamente pelo tempo limite
            }
        }
        break;

        case 7:
        {
            uint16_t current_sec = g_cpu->read_io(IO_Port::TimerGetTimeSeconds);
            g_cpu->set_gpr(1, current_sec);
        }
        break;

        default:
            break;
        }
    }
}