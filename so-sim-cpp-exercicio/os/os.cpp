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

namespace OS
{
	struct Process
	{
		uint16_t id;
		uint16_t registrars[Config::nregs];
		uint16_t pointControl;
		bool active;
		char name[64];
	};

	char cmd_buffer[256];
	uint16_t cmd_length = 0;

	Arch::Cpu *g_cpu = nullptr;
	static Process *current_process = nullptr;

	void load_idle_program()
	{
		try
		{
			std::vector<uint16_t> buffer_idle = Lib::load_from_disk_to_16bit_buffer("idle.bin");
			if (!buffer_idle.empty())
			{
				uint16_t addr = 0;
				for (uint16_t instr : buffer_idle)
				{
					g_cpu->pmem_write(addr++, instr);
				}
				g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
				terminal_println(g_cpu, Terminal::Kernel, "System Idle. Executando idle.bin...");

				for (uint8_t r = 0; r < Config::nregs; r++)
					g_cpu->set_gpr(r, 0);
				g_cpu->set_pc(1);
			}
		}
		catch (...)
		{
			g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
			terminal_println(g_cpu, Terminal::Kernel, "Aviso: idle.bin nao encontrado. Usando loop de seguranca.");
			g_cpu->pmem_write(0, 0xE000);
			g_cpu->pmem_write(1, 0x8000);
			g_cpu->set_pc(1);
		}
	}

	void boot(Arch::Cpu *cpu)
	{
		g_cpu = cpu;
		cmd_length = 0;

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
						g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
						if (current_process != nullptr)
						{
							terminal_print_str(g_cpu, Terminal::Kernel, "Processo abortado: ");
							terminal_println(g_cpu, Terminal::Kernel, current_process->name);
							delete current_process;
							current_process = nullptr;
							load_idle_program();
						}
						else
						{
							terminal_println(g_cpu, Terminal::Kernel, "Nenhum processo em execucao.");
						}
					}
					else if (command.starts_with("load "))
					{
						std::string_view nome_programa = command.substr(5);
						std::string arquivo_str(nome_programa);

						g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
						terminal_print_str(g_cpu, Terminal::Kernel, "Buscando arquivo: ");
						terminal_println(g_cpu, Terminal::Kernel, arquivo_str.c_str());

						std::vector<uint16_t> buffer_programa;
						try
						{
							buffer_programa = Lib::load_from_disk_to_16bit_buffer(arquivo_str);
						}
						catch (const std::exception &e)
						{
							g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
							terminal_println(g_cpu, Terminal::Kernel, "ERRO: Falha critica ao ler o disco.");
							buffer_programa.clear();
						}

						if (buffer_programa.empty())
						{
							g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
							terminal_println(g_cpu, Terminal::Kernel, "ERRO: Arquivo corrompido ou inexistente!");
						}
						else
						{
							uint16_t endereco_carga = 0;
							for (uint16_t instrucao : buffer_programa)
							{
								g_cpu->pmem_write(endereco_carga, instrucao);
								endereco_carga++;
							}

							g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
							terminal_println(g_cpu, Terminal::Kernel, "Programa carregado com sucesso!");

							if (current_process != nullptr)
								delete current_process;

							current_process = new Process();
							current_process->id = 1;
							current_process->pointControl = 1;
							current_process->active = true;

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
		else if (interrupt == InterruptCode::CpuException)
		{
			g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::Kernel));
			terminal_println(g_cpu, Terminal::Kernel, "EXCECAO DE CPU ENCONTRADA (GPF)");

			if (current_process != nullptr)
			{
				terminal_print_str(g_cpu, Terminal::Kernel, "Matando processo causador: ");
				terminal_println(g_cpu, Terminal::Kernel, current_process->name);
				delete current_process;
				current_process = nullptr;
			}
			load_idle_program();
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
			uint16_t addr = g_cpu->get_gpr(1);
			uint16_t ch;
			g_cpu->write_io(IO_Port::TerminalSet, static_cast<uint16_t>(Terminal::App));
			while ((ch = g_cpu->pmem_read(addr)) != 0)
			{
				const char str[2] = {static_cast<char>(ch), '\0'};
				terminal_print_str(g_cpu, Terminal::App, str);
				addr++;
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
			terminal_println(g_cpu, Terminal::App, std::to_string(value).c_str());
		}
		break;

		default:
			break;
		}
	}
}