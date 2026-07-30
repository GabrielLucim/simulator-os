#include <vector>
#include <string>
#include <string_view>

#include "process_manager.h"
#include "mem_virtual.h"
#include "os.h"
#include "os-lib.h"
#include "../lib.h"
#include "../config.h"

namespace OS
{
    Process *current_process = nullptr;

    void load_idle_program()
    {
        try
        {
            std::vector<uint16_t> buffer_idle = Lib::load_from_disk_to_16bit_buffer("idle.bin");
            if (!buffer_idle.empty())
            {
                g_cpu->set_page_table(&g_kernel_page_table);
                g_cpu->set_vmem_mode(VmemMode::Paging);

                for (size_t i = 0; i < Config::ptes_per_table; i++)
                {
                    g_cpu->get_page_table()->at(i).reset();
                }
                
                configure_hardware_page(0, 1, true, true, false, true);

                uint16_t addr = 1 * Config::page_size; 
                for (uint16_t instr : buffer_idle)
                {
                    g_cpu->pmem_write(addr++, instr);
                }
                
                g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
                terminal_println(g_cpu, Terminal::Kernel, "System Idle. executando idle.bin (Paging Ativo)...");

                for (uint8_t r = 0; r < Config::nregs; r++)
                    g_cpu->set_gpr(r, 0);
                
                g_cpu->set_pc(1); 
            }
        }
        catch (...)
        {
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
            terminal_println(g_cpu, Terminal::Kernel, "Aviso: idle.bin nao encontrado. Usando loop de fallback.");
            g_cpu->set_vmem_mode(VmemMode::Disabled);
            g_cpu->pmem_write(0, 0xE000);
            g_cpu->pmem_write(1, 0x0001);
            g_cpu->set_pc(1);
        }
    }

    void load_user_program(std::string_view filename)
    {
        std::string arquivo_str(filename);

        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
        terminal_print_str(g_cpu, Terminal::Kernel, "Buscando arquivo: ");
        terminal_println(g_cpu, Terminal::Kernel, arquivo_str.c_str());

        std::vector<uint16_t> buffer_programa;
        try
        {
            buffer_programa = Lib::load_from_disk_to_16bit_buffer(arquivo_str);
        }
        catch (...)
        {
            buffer_programa.clear();
        }

        if (buffer_programa.empty())
        {
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
            terminal_println(g_cpu, Terminal::Kernel, "ERRO: Falha ao carregar programa.");
        }
        else
        {
            g_cpu->set_page_table(&g_kernel_page_table);
            g_cpu->set_vmem_mode(VmemMode::Paging);
            next_free_physical_frame = 2;

            for (size_t i = 0; i < Config::ptes_per_table; i++)
            {
                g_cpu->get_page_table()->at(i).reset();
            }

            size_t words_count = buffer_programa.size();
            size_t pages_needed = (words_count + (Config::page_size - 1)) / Config::page_size;

            for (size_t p = 0; p < pages_needed && p < Config::ptes_per_table; p++)
            {
                configure_hardware_page(p, next_free_physical_frame, true, true, true, true);
                
                uint16_t phys_base_addr = next_free_physical_frame * Config::page_size;
                for (size_t w = 0; w < Config::page_size; w++)
                {
                    size_t global_word_index = p * Config::page_size + w;
                    if (global_word_index < words_count)
                    {
                        g_cpu->pmem_write(phys_base_addr + w, buffer_programa[global_word_index]);
                    }
                }
                next_free_physical_frame++;
            }

            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
            terminal_println(g_cpu, Terminal::Kernel, "Programa carregado com sucesso (Modo Paging)!");

            if (current_process != nullptr)
                delete current_process;

            current_process = new Process();
            current_process->id = 1;
            current_process->pointControl = 1;
            current_process->active = true;

            for (size_t i = 0; i < Config::ptes_per_table; i++)
            {
                current_process->allocated_vpages[i] = false;
            }

            size_t i = 0;
            for (; i < arquivo_str.length() && i < 63; i++)
            {
                current_process->name[i] = arquivo_str[i];
            }
            current_process->name[i] = '\0';

            for (uint8_t r = 0; r < Config::nregs; r++)
                g_cpu->set_gpr(r, 0);
            
            g_cpu->set_pc(current_process->pointControl);
        }
    }

    void kill_current_process()
    {
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
        if (current_process != nullptr)
        {
            terminal_print_str(g_cpu, Terminal::Kernel, "Processo abortado via comando kill: ");
            terminal_println(g_cpu, Terminal::Kernel, current_process->name);
            delete current_process;
            current_process = nullptr;
            load_idle_program();
        }
        else
        {
            terminal_println(g_cpu, Terminal::Kernel, "Nenhum processo do usuario em execucao.");
        }
    }
}