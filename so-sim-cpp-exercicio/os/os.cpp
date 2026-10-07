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

    constexpr uint16_t PROCESS_QUANTUM_TICKS = 5;

    static void reset_cmd_buffer();
    static void process_shell_command();
    static void handle_keyboard_input();
    static void handle_page_fault_or_gpf();

    static void sys_process_exit();
    static void sys_print_string();
    static void sys_print_newline();
    static void sys_print_integer();
    static void sys_malloc();
    static void sys_free();
    static void sys_sleep();
    static void sys_get_time();

    static void cmd_list_processes();
    static void cmd_kill_process(uint16_t pid);

    static void reset_cmd_buffer()
    {
        for (int i = 0; i < 256; i++)
            cmd_buffer[i] = '\0';
        cmd_length = 0;

        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Command));
        terminal_print_str(g_cpu, Terminal::Command, "> ");
    }

    void boot(Arch::Cpu *cpu)
    {
        g_cpu = cpu;
        cmd_length = 0;

        init_process_manager();

        g_cpu->set_vmem_mode(VmemMode::Paging);
        g_cpu->write_io(IO_Port::TimerInterruptCycles, 1000);

        terminal_println(g_cpu, Terminal::Command, "Type commands here");
        terminal_println(g_cpu, Terminal::App, "Apps output here");
        terminal_println(g_cpu, Terminal::Kernel, "Kernel output here");

        idle_process = load_user_program("idle.bin");
        execute_process(idle_process);

        terminal_print_str(g_cpu, Terminal::Command, "> ");
    }

    void interrupt(const InterruptCode interrupt)
    {
        switch (interrupt)
        {
        case InterruptCode::Keyboard:
            handle_keyboard_input();
            break;

        case InterruptCode::CpuException:
            handle_page_fault_or_gpf();
            break;

        case InterruptCode::Timer:

            update_sleeping_processes();

            if (current_process != nullptr && current_process->id != 0 && current_process->state == ProcessState::Running)
            {
                current_process->quantum_ticks++;
                if (current_process->quantum_ticks >= PROCESS_QUANTUM_TICKS)
                {
                    current_process->quantum_ticks = 0;
                    current_process->state = ProcessState::Ready;
                    schedule();
                }
            }
            else
            {
                schedule();
            }
            break;

        case InterruptCode::Disk:
            break;
        }
    }

    static void handle_keyboard_input()
    {
        uint16_t input = g_cpu->read_io(IO_Port::TerminalReadTypedChar);

        if (terminal_is_return(input))
        {
            process_shell_command();
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

    static void cmd_list_processes()
    {
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
        terminal_println(g_cpu, Terminal::Kernel, "--- TABELA DE PROCESSOS ---");
        terminal_println(g_cpu, Terminal::Kernel, "PID\tNOME\t\tESTADO\t\tMEMORIA");

        for (auto *proc : process_table)
        {
            if (proc == nullptr)
                continue;

            std::string state_str = "Unknown";
            switch (proc->state)
            {
            case ProcessState::Ready:
                state_str = "Ready";
                break;
            case ProcessState::Running:
                state_str = "Running";
                break;
            case ProcessState::Blocked:
                state_str = "Blocked(" + std::to_string(proc->sleep_ticks) + ")";
                break;
            case ProcessState::Terminated:
                state_str = "Terminated";
                break;
            }

            uint32_t mem_words = proc->num_pages << Config::page_size_bits;

            std::string line = std::to_string(proc->id) + "\t" +
                               std::string(proc->name) + "\t\t" +
                               state_str + "\t\t" +
                               std::to_string(mem_words) + " words";

            terminal_println(g_cpu, Terminal::Kernel, line.c_str());
        }
        terminal_println(g_cpu, Terminal::Kernel, "---------------------------");
    }

    static void cmd_kill_process(uint16_t pid)
    {
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));

        if (pid == 0)
        {
            terminal_println(g_cpu, Terminal::Kernel, "ERRO: Nao e possivel matar o processo idle (PID 0).");
            return;
        }

        Process *proc = get_process_by_pid(pid);
        if (proc == nullptr)
        {
            terminal_print_str(g_cpu, Terminal::Kernel, "ERRO: Processo com PID ");
            terminal_print_str(g_cpu, Terminal::Kernel, std::to_string(pid).c_str());
            terminal_println(g_cpu, Terminal::Kernel, " nao encontrado.");
            return;
        }

        terminal_print_str(g_cpu, Terminal::Kernel, "Matando processo PID ");
        terminal_print_str(g_cpu, Terminal::Kernel, std::to_string(pid).c_str());
        terminal_print_str(g_cpu, Terminal::Kernel, " (");
        terminal_print_str(g_cpu, Terminal::Kernel, proc->name);
        terminal_println(g_cpu, Terminal::Kernel, ")...");

        if (proc == current_process)
        {
            current_process = nullptr;
            destroy_process(proc);
            schedule();
        }
        else
        {
            destroy_process(proc);
        }
    }

    static void process_shell_command()
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
            else if (command == "ps")
            {
                cmd_list_processes();
            }
            else if (command.starts_with("kill "))
            {
                std::string pid_str(command.substr(5));
                try
                {
                    uint16_t pid = static_cast<uint16_t>(std::stoi(pid_str));
                    cmd_kill_process(pid);
                }
                catch (...)
                {
                    g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
                    terminal_println(g_cpu, Terminal::Kernel, "Uso incorreto. Exemplo: kill 1");
                }
            }
            else if (command.starts_with("load "))
            {
                Process *proc = load_user_program(command.substr(5));
                if (proc != nullptr)
                {
                    execute_process(proc);
                }
            }
            else
            {
                g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
                terminal_print_str(g_cpu, Terminal::Kernel, "Comando invalido: ");
                terminal_println(g_cpu, Terminal::Kernel, std::string(command).c_str());
            }
        }
        reset_cmd_buffer();
    }

    static void handle_page_fault_or_gpf()
    {
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
        auto exception_info = g_cpu->get_ref_cpu_exception();

        terminal_print_str(g_cpu, Terminal::Kernel, "!!! FALHA DE CPU / GPF DETECTADA (");
        terminal_print_str(g_cpu, Terminal::Kernel, Arch::enum_class_to_str(exception_info.type));
        terminal_println(g_cpu, Terminal::Kernel, ") !!!");

        if (current_process == idle_process || current_process == nullptr)
        {
            terminal_println(g_cpu, Terminal::Kernel, "PANIC: Falha fatal de CPU no idle.bin! Desligando...");
            g_cpu->turn_off();
            return;
        }

        if (current_process != nullptr)
        {
            terminal_print_str(g_cpu, Terminal::Kernel, "Processo abortado por acesso indevido: ");
            terminal_println(g_cpu, Terminal::Kernel, current_process->name);
        }

        Process *failed_proc = current_process;
        current_process = nullptr;
        destroy_process(failed_proc);

        schedule();
    }

    void syscall()
    {
        const uint16_t syscall_num = g_cpu->get_gpr(0);

        switch (syscall_num)
        {
        case 0:
            sys_process_exit();
            break;
        case 1:
            sys_print_string();
            break;
        case 2:
            sys_print_newline();
            break;
        case 3:
            sys_print_integer();
            break;
        case 4:
            sys_malloc();
            break;
        case 5:
            sys_free();
            break;
        case 6:
            sys_sleep();
            break;
        case 7:
            sys_get_time();
            break;
        default:
            break;
        }
        // NAO alteramos g_cpu->set_pc() aqui! A instrucao de syscall ja foi consumida pela CPU.
    }

    static void sys_process_exit()
    {
        if (current_process != nullptr)
        {
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
            terminal_print_str(g_cpu, Terminal::Kernel, "Processo finalizado: ");
            terminal_println(g_cpu, Terminal::Kernel, current_process->name);

            Process *proc_to_exit = current_process;
            current_process = nullptr;
            destroy_process(proc_to_exit);
        }

        schedule();
    }

    static void sys_print_string()
    {
        uint16_t vaddr = g_cpu->get_gpr(1);
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::App));

        try
        {
            while (true)
            {
                int32_t paddr = vaddr_to_paddr(vaddr);

                if (paddr < 0)
                {
                    g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
                    terminal_println(g_cpu, Terminal::Kernel, "[ERRO Syscall 1] Endereco invalido. Abortando...");

                    if (current_process != nullptr && current_process->id != 0)
                    {
                        cmd_kill_process(current_process->id);
                    }
                    return;
                }

                uint16_t val = g_cpu->pmem_read(static_cast<uint16_t>(paddr));

                if (val == 0)
                    break;

                char ch1 = static_cast<char>(val & 0xFF);
                if (ch1 == '\0')
                    break;

                const char str1[2] = {ch1, '\0'};
                terminal_print_str(g_cpu, Terminal::App, str1);

                char ch2 = static_cast<char>((val >> 8) & 0xFF);
                if (ch2 != '\0')
                {
                    const char str2[2] = {ch2, '\0'};
                    terminal_print_str(g_cpu, Terminal::App, str2);
                }

                vaddr++;
            }
        }
        catch (...)
        {
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
            terminal_println(g_cpu, Terminal::Kernel, "[ERRO Fatal] Excecao na Syscall 1. Abortando...");
            if (current_process != nullptr && current_process->id != 0)
            {
                cmd_kill_process(current_process->id);
            }
        }
    }

    static void sys_print_newline()
    {
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::App));
        terminal_print_str(g_cpu, Terminal::App, "\n");
    }

    static void sys_print_integer()
    {
        uint16_t value = g_cpu->get_gpr(1);
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::App));
        terminal_print_str(g_cpu, Terminal::App, std::to_string(value).c_str());
    }

    static void sys_malloc()
    {
        uint16_t num_words = g_cpu->get_gpr(1);

        if (num_words == 0 || current_process == nullptr)
        {
            g_cpu->set_gpr(1, 0);
            return;
        }

        uint16_t pages_needed = (num_words + Config::page_size - 1) / Config::page_size;
        uint16_t allocated_vaddr = current_process->num_pages * Config::page_size;

        current_process->num_pages += pages_needed;

        g_cpu->set_gpr(1, 1);
        g_cpu->set_gpr(2, allocated_vaddr);
    }

    static void sys_free()
    {
        // Implementar se necessario
    }

    static void sys_sleep()
    {
        uint16_t ticks = g_cpu->get_gpr(1);

        if (current_process != nullptr && current_process->id != 0)
        {
            if (ticks > 0)
            {
                current_process->sleep_ticks = ticks;
                current_process->state = ProcessState::Blocked;
            }
            schedule();
        }
    }

    static void sys_get_time()
    {
        static uint16_t system_time = 0;
        system_time += 5;
        g_cpu->set_gpr(1, system_time);
    }
}