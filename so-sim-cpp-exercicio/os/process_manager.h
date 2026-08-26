#ifndef __ARQSIM_HEADER_PROCESS_MANAGER_H__
#define __ARQSIM_HEADER_PROCESS_MANAGER_H__

#include <string_view>
#include <vector>
#include <cstdint>
#include "process.h"

namespace OS
{
    extern Process *current_process;
    extern Process *idle_process;

    std::vector<uint16_t> read_program_file(std::string_view filename);
    Process* create_process_struct(std::string_view filename, bool is_idle);
    void process_memory_config(Process *proc, const std::vector<uint16_t> &buffer);
    void free_process_memory(Process *proc);

    Process* load_user_program(std::string_view filename);
    void execute_process(Process *proc);
    void kill_current_process();
}

#endif