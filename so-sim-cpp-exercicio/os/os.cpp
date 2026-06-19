#include <stdexcept>
#include <string>
#include <string_view>

#include <cstdint>
#include <cstdlib>
#include <thread>
#include <chrono>

#include "../config.h"
#include "../lib.h"
#include "../arch/arch.h"
#include "os.h"
#include "os-lib.h"


namespace OS {

	struct Process {
		uint16_t id;
		uint16_t registrars[Config::nregs];
		uint16_t pointControl;
		bool active;
		char name[64];
	};

	char cmd_buffer[256];
	uint16_t cmd_length = 0;

	Arch::Cpu *g_cpu = nullptr;
	static Process* current_process = nullptr;

	// ---------------------------------------

	void boot (Arch::Cpu *cpu)
	{
		g_cpu = cpu;
		cmd_length = 0;

		terminal_println(g_cpu, Terminal::Command, "Type commands here");
		terminal_println(g_cpu, Terminal::App, "Apps output here");
		terminal_println(g_cpu, Terminal::Kernel, "Kernel output here");

		terminal_print_str(g_cpu, Terminal::Command, "> ");
	}

	// ---------------------------------------

	void interrupt (const InterruptCode interrupt)
	{
		if (interrupt == InterruptCode::Keyboard) {
			const uint16_t input = g_cpu->read_io(IO_Port::TerminalReadTypedChar);

			if (terminal_is_return(input)) {
				terminal_println(g_cpu, Terminal::Command, "");

				std::string_view command(cmd_buffer, cmd_length);
				if (!command.empty()) {
					if (command == "quit" || command == "exit") {
						terminal_println(g_cpu, Terminal::Kernel, "Desligando o simulador...");
						g_cpu->turn_off();
					}
					else if (command.starts_with("load ")) {
						std::string_view nome_programa = command.substr(5);
						terminal_print_str(g_cpu, Terminal::Kernel, "Carregando arquivo do disco: ");
						terminal_println(g_cpu, Terminal::Kernel, std::string(nome_programa).c_str());
					}
					else {
						terminal_print_str(g_cpu, Terminal::Kernel, "Comando invalido: ");
						terminal_println(g_cpu, Terminal::Kernel, std::string(command).c_str());
					}
				}

				cmd_length = 0;
				terminal_print_str(g_cpu, Terminal::Command, "> ");
			}
			else if (terminal_is_backspace(input)) {
				if (cmd_length > 0) {
					cmd_length--;

					g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Command));
					g_cpu->write_io(IO_Port::TerminalUpload, '\r');
					terminal_print_str(g_cpu, Terminal::Command, "> ");
					for (int i = 0; i < cmd_length; i++) {
						g_cpu->write_io(IO_Port::TerminalUpload, static_cast<uint16_t>(cmd_buffer[i]));
					}
				}
			}
			else if (cmd_length < 256) {
				cmd_buffer[cmd_length++] = static_cast<char>(input);
				g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Command));
				g_cpu->write_io(IO_Port::TerminalUpload, input);
			}
		}
	}

	// ---------------------------------------

	void syscall ()
	{

	}

	// ---------------------------------------

	void main_loop (Arch::Cpu *cpu)
	{
		g_cpu = cpu;

		while (true) {
			if (current_process == nullptr || !current_process->active) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			} else {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}
	}

} // end namespace OS