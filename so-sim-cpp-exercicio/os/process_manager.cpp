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

    std::vector<uint16_t> read_program_file(std::string_view filename)
    {
        try
        {
            return Lib::load_from_disk_to_16bit_buffer(std::string(filename));
        }
        catch (...)
        {
            return {};
        }
    }

    Process* create_process_struct(std::string_view filename, bool is_idle)
    {
        Process *proc = new Process();
        proc->id = is_idle ? 0 : 1;
        proc->pointControl = 1;
        proc->active = false;

        size_t i = 0;
        for (; i < filename.length() && i < 63; i++)
            proc->name[i] = filename[i];
        proc->name[i] = '\0';

        for (size_t p = 0; p < Config::ptes_per_table; p++)
        {
            proc->allocated_vpages[p] = false;
            proc->page_table.at(p).reset();
        }

        return proc;
    }

    void process_memory_config(Process *proc, const std::vector<uint16_t> &buffer)
    {
        size_t words_count = buffer.size();
        size_t pages_needed = (words_count + 15) >> 4;

        g_cpu->set_page_table(&proc->page_table);

        for (size_t p = 0; p < pages_needed && p < Config::ptes_per_table; p++)
        {
            uint16_t frame = next_free_physical_frame++;
            configure_hardware_page(p, frame, true, true, true, true);
            
            uint16_t phys_base_addr = frame << 4;

            for (size_t w = 0; w < Config::page_size; w++)
            {
                size_t global_word_index = (p << 4) | w;
                if (global_word_index < words_count)
                {
                    g_cpu->pmem_write(phys_base_addr + w, buffer[global_word_index]);
                }
            }
        }
    }

    Process* load_user_program(std::string_view filename)
    {
        std::string arquivo_str(filename);

        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
        terminal_print_str(g_cpu, Terminal::Kernel, "Buscando arquivo: ");
        terminal_println(g_cpu, Terminal::Kernel, arquivo_str.c_str());

        std::vector<uint16_t> buffer_programa = read_program_file(filename);

        if (buffer_programa.empty())
        {
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
            terminal_println(g_cpu, Terminal::Kernel, "ERRO: Falha ao carregar programa.");
            return nullptr;
        }

        bool is_idle = (arquivo_str == "idle.bin");
        Process *proc = create_process_struct(arquivo_str, is_idle);

        process_memory_config(proc, buffer_programa);

        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
        if (is_idle)
        {
            terminal_println(g_cpu, Terminal::Kernel, "System Idle. executando idle.bin (Paging Ativo)...");
        }
        else
        {
            terminal_println(g_cpu, Terminal::Kernel, "Programa carregado com sucesso (Modo Paging)!");
        }

        return proc;
    }

    void execute_process(Process *proc)
    {
        if (proc == nullptr) return;

        if (current_process != nullptr)
            delete current_process;

        current_process = proc;
        current_process->active = true;

        g_cpu->set_page_table(&current_process->page_table);
        g_cpu->set_vmem_mode(VmemMode::Paging);

        for (uint8_t r = 0; r < Config::nregs; r++)
            g_cpu->set_gpr(r, 0);

        g_cpu->set_pc(current_process->pointControl);
    }

    void kill_current_process()
    {
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
        if (current_process != nullptr && current_process->id != 0)
        {
            terminal_print_str(g_cpu, Terminal::Kernel, "Processo abortado via comando kill: ");
            terminal_println(g_cpu, Terminal::Kernel, current_process->name);
            
            Process *idle = load_user_program("idle.bin");
            execute_process(idle);
        }
        else
        {
            terminal_println(g_cpu, Terminal::Kernel, "Nenhum processo do usuario em execucao.");
        }
    }
}