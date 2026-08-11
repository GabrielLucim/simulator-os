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


    static void reset_cmd_buffer();
    static void process_shell_command();
    static void handle_keyboard_input();
    static void handle_page_fault_or_gpf();


    // syscalls
    static void sys_process_exit();
    static void sys_print_string();
    static void sys_print_newline();
    static void sys_print_integer();


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


        g_cpu->set_vmem_mode(VmemMode::Paging);


        g_cpu->write_io(IO_Port::TimerInterruptCycles, 1000);


        terminal_println(g_cpu, Terminal::Command, "Type commands here");
        terminal_println(g_cpu, Terminal::App, "Apps output here");
        terminal_println(g_cpu, Terminal::Kernel, "Kernel output here");


        Process *idle = load_user_program("idle.bin");
        execute_process(idle);


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
            else if (command == "kill")
            {
                kill_current_process();
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


        if (current_process != nullptr)
        {
            terminal_print_str(g_cpu, Terminal::Kernel, "Processo abortado por acesso indevido: ");
            terminal_println(g_cpu, Terminal::Kernel, current_process->name);
        }


        Process *idle = load_user_program("idle.bin");
        execute_process(idle);
    }


    void syscall()
    {
        const uint16_t syscall_num = g_cpu->get_gpr(0);


        switch (syscall_num)
        {
        case 0: sys_process_exit(); break;
        case 1: sys_print_string(); break;
        case 2: sys_print_newline(); break;
        case 3: sys_print_integer(); break;
        default: break;
        }
    }


    static void sys_process_exit()
    {
        if (current_process != nullptr)
        {
            g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
            terminal_print_str(g_cpu, Terminal::Kernel, "Processo finalizado: ");
            terminal_println(g_cpu, Terminal::Kernel, current_process->name);
        }
       
        Process *idle = load_user_program("idle.bin");
        execute_process(idle);
    }


    static void sys_print_string()
    {
        uint16_t vaddr = g_cpu->get_gpr(1);
        g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::App));
        
        while (true)
        {
            uint16_t paddr = vaddr_to_paddr(vaddr);
            uint16_t val = g_cpu->pmem_read(paddr);
            
            if (val == 0) break;

            char ch1 = static_cast<char>(val & 0xFF);
            if (ch1 == '\0') break;

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
}
