#ifndef __ARQSIM_HEADER_PROCESS_MANAGER_H__
#define __ARQSIM_HEADER_PROCESS_MANAGER_H__

#include <string_view>
#include "process.h"

namespace OS
{
    extern Process *current_process;

    void load_idle_program();
    void load_user_program(std::string_view filename);
    void kill_current_process();
}

#endif