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

namespace OS
{
	char cmd_buffer[256];
	uint16_t cmd_length = 0;

	Arch::Cpu *g_cpu = nullptr;
	static Process *current_process = nullptr;
	static uint16_t next_free_physical_frame = 2;

	static Arch::Cpu::PageTable g_kernel_page_table;

	void configure_hardware_page(uint16_t vpage, uint16_t phy_frame, bool present, bool readable, bool writeable, bool executable)
	{
		if (g_cpu == nullptr || g_cpu->get_page_table() == nullptr) return;

		auto &entry = g_cpu->get_page_table()->at(vpage);
		entry.reset();
		
		entry.set(Arch::Cpu::PteField::PhyFrameID, phy_frame);
		entry.set(Arch::Cpu::PteField::Present, present ? 1 : 0);
		entry.set(Arch::Cpu::PteField::Readable, readable ? 1 : 0);
		entry.set(Arch::Cpu::PteField::Writable, writeable ? 1 : 0);
		entry.set(Arch::Cpu::PteField::Executable, executable ? 1 : 0);
	}

	void load_idle_program()
	{
		try
		{
			std::vector<uint16_t> buffer_idle = Lib::load_from_disk_to_16bit_buffer("idle.bin");
			if (!buffer_idle.empty())
			{
				// Vincula a tabela de páginas à CPU e ativa a paginação
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

	void boot(Arch::Cpu *cpu)
	{
		g_cpu = cpu;
		cmd_length = 0;

		g_cpu->set_page_table(&g_kernel_page_table);

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

				g_cpu->write_io(static_cast<IO_Port>(5), 1); 
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

			for (size_t i = next_free_physical_frame; i < Config::ptes_per_table; i++)
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

		default:
			break;
		}
	}
}