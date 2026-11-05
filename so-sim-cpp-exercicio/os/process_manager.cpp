#include <list>
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
    Process *idle_process = nullptr;
    std::list<Process*> process_table;
    static uint16_t next_pid = 0;
    static std::list<Process*>::iterator current_rr_it;

    static void save_current_context()
    {
        if (current_process != nullptr)
        {
            current_process->pointControl = g_cpu->get_pc();
            for (uint8_t r = 0; r < Config::nregs; r++)
            {
                current_process->gprs[r] = g_cpu->get_gpr(r);
            }
            if (current_process->state == ProcessState::Running)
            {
                current_process->state = ProcessState::Ready;
            }
        }
    }

    static void restore_process_context(Process *proc)
    {
        if (proc == nullptr) return;

        current_process = proc;
        current_process->state = ProcessState::Running;

        g_cpu->set_page_table(&current_process->page_table);

        for (uint8_t r = 0; r < Config::nregs; r++)
        {
            g_cpu->set_gpr(r, current_process->gprs[r]);
        }

        g_cpu->set_pc(current_process->pointControl);
    }

    void init_process_manager()
    {
        process_table.clear();
        next_pid = 0;
        current_rr_it = process_table.end();
    }

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
        proc->id = is_idle ? 0 : ++next_pid;
        proc->pointControl = 1;
        proc->state = ProcessState::Ready;
        proc->num_pages = 0;
        proc->sleep_ticks = 0;

        for (uint8_t r = 0; r < Config::nregs; r++)
            proc->gprs[r] = 0;

        size_t i = 0;
        for (; i < filename.length() && i < 63; i++)
            proc->name[i] = filename[i];
        proc->name[i] = '\0';

        for (size_t p = 0; p < Config::ptes_per_table; p++)
        {
            proc->page_table.at(p).reset();
        }

        process_table.push_back(proc);
        return proc;
    }

    void process_memory_config(Process *proc, const std::vector<uint16_t> &buffer)
    {
        size_t words_count = buffer.size();

        size_t pages_needed = (words_count + (Config::page_size - 1)) >> Config::page_size_bits;

        proc->num_pages = static_cast<uint16_t>(pages_needed);
        g_cpu->set_page_table(&proc->page_table);

        for (size_t p = 0; p < pages_needed && p < Config::ptes_per_table; p++)
        {
            int frame = allocate_physical_frame();
            if (frame == -1) break;

            uint16_t phy_frame = static_cast<uint16_t>(frame);

            configure_hardware_page(p, phy_frame, true, true, true, true);
            
            uint16_t phys_base_addr = phy_frame << Config::page_size_bits;

            for (size_t w = 0; w < Config::page_size; w++)
            {
                size_t global_word_index = (p << Config::page_size_bits) | w;
                if (global_word_index < words_count)
                {
                    g_cpu->pmem_write(phys_base_addr | w, buffer[global_word_index]);
                }
            }
        }
    }

    void free_process_memory(Process *proc)
    {
        if (proc == nullptr) return;

        for (size_t p = 0; p < Config::ptes_per_table; p++)
        {
            auto &entry = proc->page_table.at(p);
            if (entry.get(Arch::Cpu::PteField::Present) == 1)
            {
                uint16_t frame = entry.get(Arch::Cpu::PteField::PhyFrameID);
                free_physical_frame(frame);
                entry.reset();
            }
        }
        proc->num_pages = 0;
        proc->allocations.clear();
    }

    void destroy_process(Process *proc)
    {
        if (proc == nullptr) return;

        free_process_memory(proc);

        for (auto it = process_table.begin(); it != process_table.end(); ++it)
        {
            if (*it == proc)
            {
                if (current_rr_it == it)
                {
                    current_rr_it = process_table.erase(it);
                }
                else
                {
                    process_table.erase(it);
                }
                break;
            }
        }

        delete proc;
    }

    Process* get_process_by_pid(uint16_t pid)
    {
        for (auto *proc : process_table)
        {
            if (proc != nullptr && proc->id == pid)
                return proc;
        }
        return nullptr;
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

        save_current_context();
        restore_process_context(proc);
    }

    void kill_current_process()
    {
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
        if (current_process != nullptr && current_process->id != 0)
        {
            terminal_print_str(g_cpu, Terminal::Kernel, "Processo abortado via comando kill: ");
            terminal_println(g_cpu, Terminal::Kernel, current_process->name);
            
            Process *proc_to_destroy = current_process;
            current_process = nullptr;
            
            destroy_process(proc_to_destroy);

            schedule();
        }
        else
        {
            terminal_println(g_cpu, Terminal::Kernel, "Nenhum processo do usuario em execucao.");
        }
    }

    void update_sleeping_processes()
    {
        for (auto *proc : process_table)
        {
            if (proc != nullptr && proc->state == ProcessState::Blocked && proc->sleep_ticks > 0)
            {
                proc->sleep_ticks--;
                if (proc->sleep_ticks == 0)
                {
                    proc->state = ProcessState::Ready;
                }
            }
        }
    }

    void schedule()
    {
        if (process_table.empty()) return;

        save_current_context();

        Process *next_proc = nullptr;

        if (current_rr_it == process_table.end())
        {
            current_rr_it = process_table.begin();
        }

        auto start_it = current_rr_it;
        do
        {
            ++current_rr_it;
            if (current_rr_it == process_table.end())
            {
                current_rr_it = process_table.begin();
            }

            Process *p = *current_rr_it;
            if (p != nullptr && p->id != 0 && (p->state == ProcessState::Ready || p->state == ProcessState::Running))
            {
                next_proc = p;
                break;
            }
        } while (current_rr_it != start_it);

        if (next_proc == nullptr)
        {
            next_proc = idle_process;
        }

        restore_process_context(next_proc);
    }
}