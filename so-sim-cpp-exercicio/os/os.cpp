#include <stdexcept>
#include <string>
#include <string_view>

#include <cstdint>
#include <cstdlib>

#include "../config.h"
#include "../lib.h"
#include "../arch/arch.h"
#include "os.h"
#include "os-lib.h"

namespace OS
{

	Arch::Cpu *g_cpu = nullptr;
	std::string command_buffer = "";

	// ---------------------------------------

	void boot(Arch::Cpu *cpu)
	{
		g_cpu = cpu;
		terminal_println(cpu, Terminal::Command, "Type commands here");
		terminal_println(cpu, Terminal::App, "Apps output here");
		terminal_println(cpu, Terminal::Kernel, "Kernel output here");
	}

	// ---------------------------------------

	void interrupt(const InterruptCode interrupt)
	{
		if (static_cast<int>(interrupt) == 0)
		{

			uint16_t txt_char = g_cpu->read_io(2);

			if (txt_char == '\n')
			{

				if (command_buffer == "fechar" || command_buffer == "exit")
				{
					terminal_println(g_cpu, Terminal::Kernel, "Desligando o simulador...");
					g_cpu->turn_off();
				}
				else
				{
					terminal_println(g_cpu, Terminal::Kernel, ("Comando invalido: " + command_buffer).c_str());
				}

				command_buffer = "";
			}
			else if (txt_char == 8 || txt_char == 127)
			{
				if (!command_buffer.empty())
				{
					command_buffer.pop_back();
				}
			}
			
			else
			{
				command_buffer += (char)txt_char;
			}
		}
	}

	// ---------------------------------------

	void syscall()
	{
	}

	// ---------------------------------------

} // end namespace OS