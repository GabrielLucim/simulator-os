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


namespace OS {

Arch::Cpu *g_cpu = nullptr;
std::string command_buffer = "";

// ---------------------------------------

void boot (Arch::Cpu *cpu)
{
	terminal_println(cpu, Terminal::Command, "Type commands here");
	terminal_println(cpu, Terminal::App, "Apps output here");
	terminal_println(cpu, Terminal::Kernel, "Kernel output here");
}

// ---------------------------------------

void interrupt (const InterruptCode interrupt)
{

}

// ---------------------------------------

void syscall ()
{

}

// ---------------------------------------

} // end namespace OS
